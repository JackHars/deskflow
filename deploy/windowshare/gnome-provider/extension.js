// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT

import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Meta from 'gi://Meta';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Config from 'resource:///org/gnome/shell/misc/config.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

import {
    BUS_NAME, OBJECT_PATH, MAX_ACTORS, MAX_WINDOWS, STARTUP_MS, ProviderState,
    samePointerPosition, supportedVersion, unavailableReason, validRect,
    validRequest, validRoutedPointRequest, validRoutedPoints, validWindowAssociation, associatedWindowIndices,
    duplicateWindowAssociationNotification,
    StageUpdateSequence, InputDrainSequence, PreparationActivationSequence,
    INPUT_DRAIN_BINDING_TYPES, validInputDrainBinding, ORDERING_LIMIT,
    INPUT_PREPARATION_BINDING_TYPES, validInputPreparationBinding, sameInputPreparationBinding,
} from './logic.js';
import {closeDescriptorList, withViewerLauncher} from './launcher.js';
import {orderingStillMapped, verifyMappedOrdering} from './ordering.js';

const REMOTE_NAME = 'org.gnome.Mutter.RemoteDesktop';
const REMOTE_PATH = '/org/gnome/Mutter/RemoteDesktop';
const REMOTE_SESSION = `${REMOTE_NAME}.Session`;
const ERROR_NAME = `${BUS_NAME}.Error`;
const nowMs = () => GLib.get_monotonic_time() / 1000;
const uuid = () => GLib.uuid_string_random();
const typeName = object => GObject.type_name_from_instance(object);
const rectArray = rect => [rect.x, rect.y, rect.width, rect.height];

function removeSource(id) {
    if (id)
        GLib.source_remove(id);
}

function inputDrainBinding(invocation, preparation = false) {
    // Read the original dictionary, not deep_unpack's object: duplicate D-Bus
    // keys must not disappear before the closed schema is checked.
    const raw = invocation.get_parameters().get_child_value(7);
    const types = preparation ? INPUT_PREPARATION_BINDING_TYPES : INPUT_DRAIN_BINDING_TYPES;
    const keys = Object.keys(types);
    if (raw.get_type_string() !== 'a{sv}' || raw.n_children() !== keys.length)
        throw new Error('Malformed input-drain correlation');
    const binding = {};
    for (let i = 0; i < raw.n_children(); i++) {
        const entry = raw.get_child_value(i);
        const key = entry.get_child_value(0).deep_unpack();
        const value = entry.get_child_value(1).get_variant();
        if (!Object.hasOwn(types, key) || Object.hasOwn(binding, key) ||
            value.get_type_string() !== types[key])
            throw new Error('Malformed input-drain correlation');
        binding[key] = value.deep_unpack();
    }
    if (!(preparation ? validInputPreparationBinding(binding) : validInputDrainBinding(binding)))
        throw new Error('Invalid input-drain correlation');
    return Object.freeze(binding);
}

function disconnect(signals) {
    for (const [object, id] of signals.splice(0)) {
        try {
            object.disconnect(id);
        } catch (_) {
            // A destroyed native object has already removed its handlers.
        }
    }
}

function closeConnection(connection) {
    if (!connection || connection.is_closed())
        return;
    connection.close(null, (source, result) => {
        try {
            source.close_finish(result);
        } catch (_) {
            // The peer may already have closed it.
        }
    });
}

function call(connection, destination, path, iface, method, parameters, replyType, cancellable = null) {
    return new Promise((resolve, reject) => {
        connection.call_with_unix_fd_list(destination, path, iface, method,
            parameters, new GLib.VariantType(replyType), Gio.DBusCallFlags.NO_AUTO_START,
            STARTUP_MS, null, cancellable, (source, result) => {
                try {
                    resolve(source.call_with_unix_fd_list_finish(result));
                } catch (error) {
                    reject(error);
                }
            });
    });
}

function privateConnection(address, cancellable) {
    return new Promise((resolve, reject) => {
        Gio.DBusConnection.new_for_address(address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
            Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            null, cancellable, (_source, result) => {
                try {
                    const connection = Gio.DBusConnection.new_for_address_finish(result);
                    connection.set_exit_on_close(false);
                    resolve(connection);
                } catch (error) {
                    reject(error);
                }
            });
    });
}

function errorReply(invocation, error) {
    invocation.return_dbus_error(ERROR_NAME, String(error.message ?? error).slice(0, 512));
}

function variants(values) {
    const result = {};
    for (const [name, [signature, value]] of Object.entries(values))
        result[name] = new GLib.Variant(signature, value);
    return result;
}

class Provider {
    constructor(path) {
        this._path = path;
        this._state = new ProviderState();
        this._signals = [];
        this._cancellable = new Gio.Cancellable();
        this._connection = Gio.DBus.session;
        this._nameId = 0;
        this._ownerSubscription = 0;
        this._object = null;
        this._native = false;
        this._stageCapable = false;
        this._drainCapable = false;
        this._ordering = null;
    }

    async start() {
        if (!supportedVersion(Config.PACKAGE_VERSION))
            throw new Error('This provider supports stable GNOME Shell 50 only');
        this._backend = typeName(global.backend);
        // Mutter 50 always creates its Wayland compositor and removed the old
        // is_wayland_compositor() API. The actual backend type still excludes
        // nested backends; neither session environment nor a display name is
        // used as evidence here.
        this._native = this._backend === 'MetaBackendNative';
        if (!this._native || typeof Meta.WaylandClient.new_subprocess !== 'function')
            throw new Error('A Mutter 50 native Wayland backend is required');
        // Only claim the optional read-only compositor frame observation when
        // this exact Shell exposes its real Clutter StageView layout method.
        // This does not enable the compositor input-consumption drain.
        this._stageCapable = typeof global.stage.schedule_update === 'function' &&
            typeof Clutter.StageView?.prototype?.get_layout === 'function';
        this._drainCapable = this._stageCapable && typeof Clutter.events_pending === 'function' &&
            typeof GLib.main_depth === 'function';
        this._ordering = await verifyMappedOrdering();
        if (this._state.closed)
            return;
        this._address = GLib.getenv('DBUS_SESSION_BUS_ADDRESS');
        if (!this._address)
            throw new Error('An explicit session bus address is required');
        const names = await Promise.all(['org.gnome.Shell', REMOTE_NAME].map(async name => {
            const [reply] = await call(this._connection, 'org.freedesktop.DBus',
                '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'GetNameOwner',
                new GLib.Variant('(s)', [name]), '(s)', this._cancellable);
            return reply.deep_unpack()[0];
        }));
        if (this._state.closed)
            return;
        this._shellOwner = this._connection.get_unique_name();
        if (names.some(name => name !== this._shellOwner))
            throw new Error('Shell and Mutter service owners must be this exact connection');
        const [ok, data] = GLib.file_get_contents(`${this._path}/interface.xml`);
        if (!ok)
            throw new Error('Provider interface is missing');
        this._object = Gio.DBusExportedObject.wrapJSObject(new TextDecoder().decode(data), this);
        this._object.export(this._connection, OBJECT_PATH);
        this._nameId = Gio.bus_own_name_on_connection(this._connection, BUS_NAME,
            Gio.BusNameOwnerFlags.DO_NOT_QUEUE, null, () => this.stop());
        this._ownerSubscription = this._connection.signal_subscribe('org.freedesktop.DBus',
            'org.freedesktop.DBus', 'NameOwnerChanged', '/org/freedesktop/DBus', null,
            Gio.DBusSignalFlags.NONE, (_c, _s, _p, _i, _n, parameters) => {
                const [name, _old, current] = parameters.deep_unpack();
                if (['org.gnome.Shell', REMOTE_NAME].includes(name) && current !== this._shellOwner)
                    this.stop();
                const session = this._state.current;
                if (session && name === session.sender && !current)
                    this._close(session, 'caller-disconnected');
            });
        this._watch(global.display, 'window-created', 'window-created');
        this._watch(global.display, 'restacked', 'restacked');
        this._watch(global.display, 'notify::focus-window', 'focus-changed');
        this._watch(global.stage, 'notify::is-grabbed', 'grab-changed');
        this._watch(global.backend.get_monitor_manager(), 'monitors-changed', 'layout-changed');
        this._watch(global.workspace_manager, 'active-workspace-changed', 'workspace-changed');
        this._watch(Main.sessionMode, 'updated', 'session-mode-changed');
        this._watch(Main.overview, 'showing', 'overview');
        this._watch(Main.overview, 'hiding', 'overview');
    }

    _watch(object, signal, reason, list = this._signals, session = null) {
        const id = object.connect(signal, () => {
            const current = session ?? this._state.current;
            if (current)
                this._invalidate(current, reason);
        });
        list.push([object, id]);
    }

    _invalidate(session, reason) {
        if (!this._state.matches(session))
            return;
        if (!this._state.invalidate(session)) {
            this._close(session, 'revision-exhausted');
            return;
        }
        const activation = session.preparationActivation;
        if (activation)
            activation.lastInvalidation = reason;
        if (activation && !activation.sequence.invalidated(reason,
            global.display.focus_window === activation.window, nowMs()))
            this._finishPreparationActivation(session, activation, activation.sequence.reason || 'activation-invalidated');
        this._finishStageCheck(session, session.stageCheck, 'native-state-invalidated');
        this._finishInputDrain(session, session.inputDrain, 'native-state-invalidated');
        this._object?.emit_signal('Invalidated',
            new GLib.Variant('(suus)', [session.id, session.generation, session.revision, reason]));
    }

    GetCapabilitiesAsync(_args, invocation) {
        invocation.return_value(new GLib.Variant('(usa{sv})', [1, Config.PACKAGE_VERSION, variants({
            providerApi: ['u', 1], backend: ['s', this._backend],
            nativeHitTesting: ['b', this._native], exactWaylandClientBinding: ['b', true],
            eisSessionOwned: ['b', true], trustedDesktopOnly: ['b', true],
            viewportChannel: ['b', true], routedPointObservation: ['b', true],
            routedPointsObservation: ['b', true],
            perWindowAssociation: ['b', true],
            inputPreparation: ['b', this._drainCapable && Boolean(this._ordering) &&
                typeof Meta.Window.prototype.activate === 'function' &&
                typeof global.display.get_current_time_roundtrip === 'function'],
            stageUpdateSequence: ['b', this._stageCapable],
            inputDrainSegment: ['b', this._drainCapable],
            inputDrainOrderingLimit: ['s', this._ordering ? '' : ORDERING_LIMIT],
            inputDrainOrderingContract: ['s', this._ordering?.contract ?? ''],
            viewportSocketValidated: ['b', false],
            syncFenceVerified: ['b', false], pointerDeviceAttribution: ['b', false],
            qtViewportMapping: ['b', false], deliveryReady: ['b', false],
        })]));
    }

    async CreateSessionAsync(_args, invocation) {
        let session;
        try {
            session = this._state.begin(invocation.get_sender(), uuid(), nowMs());
            Object.assign(session, {
                cancel: new Gio.Cancellable(), invocation, signals: [], windowSignals: [], connection: null,
                viewer: null, window: null, surface: null, signature: '', stageCheck: null, inputDrain: null,
            });
            session.timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, STARTUP_MS, () => {
                session.timeout = 0;
                this._close(session, 'startup-timeout');
                return GLib.SOURCE_REMOVE;
            });
            // A separate owner connection is essential. Closing it retires a
            // session even if an asynchronous CreateSession reply is lost.
            const connection = await privateConnection(this._address, session.cancel);
            if (!this._state.matches(session)) {
                closeConnection(connection);
                return;
            }
            session.connection = connection;
            session.signals.push([connection, connection.connect('closed', () =>
                this._close(session, 'mutter-connection-closed'))]);
            const [created] = await call(connection, this._shellOwner, REMOTE_PATH,
                REMOTE_NAME, 'CreateSession', null, '(o)', session.cancel);
            if (!this._state.matches(session))
                return;
            session.remotePath = created.deep_unpack()[0];
            session.closedSignal = connection.signal_subscribe(this._shellOwner,
                REMOTE_SESSION, 'Closed', session.remotePath, null, Gio.DBusSignalFlags.NONE,
                () => this._close(session, 'mutter-session-closed'));
            await call(connection, this._shellOwner, session.remotePath, REMOTE_SESSION,
                'Start', null, '()', session.cancel);
            if (!this._state.matches(session))
                return;
            const [connected, fdList] = await call(connection, this._shellOwner,
                session.remotePath, REMOTE_SESSION, 'ConnectToEIS',
                new GLib.Variant('(a{sv})', [{ 'device-types': new GLib.Variant('u', 3) }]),
                '(h)', session.cancel);
            const handle = connected.deep_unpack()[0];
            if (!fdList || fdList.get_length() !== 1 || handle !== 0)
                throw new Error('Mutter did not return one EIS descriptor');
            if (!this._state.ready(session, nowMs())) {
                this._close(session, 'startup-expired');
                return;
            }
            removeSource(session.timeout);
            session.timeout = 0;
            const pending = session.invocation;
            session.invocation = null;
            // Forward the real GUnixFDList; no fd duplication or numeric-fd
            // substitution. GDBus attaches SCM_RIGHTS to this reply.
            pending.return_value_with_unix_fd_list(new GLib.Variant('(suh)',
                [session.id, session.generation, handle]), fdList);
        } catch (error) {
            if (session)
                this._close(session, 'startup-failed');
            else
                errorReply(invocation, error);
        }
    }

    _lookup(invocation, id, generation) {
        return this._state.lookup(invocation.get_sender(), id, generation);
    }

    CloseSessionAsync([id, generation], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            this._close(session, 'caller-closed');
            invocation.return_value(null);
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    LaunchViewerAsync([id, generation, executable, args], invocation) {
        this._launchViewer(id, generation, executable, args, invocation, null);
    }

    LaunchViewerWithViewportAsync([id, generation, executable, args, handle], invocation) {
        const message = invocation.get_message();
        this._launchViewer(id, generation, executable, args, invocation, {
            handle, fdList: message.get_unix_fd_list(), declaredCount: message.get_num_unix_fds(),
        });
    }

    _launchViewer(id, generation, executable, args, invocation, viewport) {
        try {
            const session = this._lookup(invocation, id, generation);
            if (session.viewer)
                throw new Error('One viewer is allowed per session');
            const client = withViewerLauncher(executable, args, viewport,
                (launcher, argv) => Meta.WaylandClient.new_subprocess(global.context, launcher, argv));
            const process = client.get_subprocess();
            session.viewer = {token: uuid(), client, process, exited: false, killSource: 0};
            const viewer = session.viewer;
            session.signals.push([client, client.connect('client-destroyed', () =>
                this._close(session, 'viewer-disconnected'))]);
            process.wait_async(null, (child, result) => {
                try {
                    child.wait_finish(result);
                    viewer.exited = true;
                } catch (_) {
                    // An owned process must still be retired on wait failure.
                }
                removeSource(viewer.killSource);
                viewer.killSource = 0;
                this._close(session, 'viewer-exited');
            });
            this._invalidate(session, 'viewer-launched');
            invocation.return_value(new GLib.Variant('(s)', [viewer.token]));
        } catch (error) {
            errorReply(invocation, error);
        } finally {
            // Also covers wrong caller/stale session/already-launched rejection
            // before the launcher takes ownership. An emptied list is harmless.
            closeDescriptorList(viewport?.fdList);
        }
    }

    _viewer(session, token) {
        if (!session.viewer || session.viewer.token !== token || session.viewer.exited)
            throw new Error('Stale viewer');
        return session.viewer;
    }

    _base(session, token, reason = '') {
        return {
            available: ['b', !reason], reason: ['s', reason],
            observationOnly: ['b', true], deliveryReady: ['b', false],
            session: ['s', session.id], generation: ['u', session.generation],
            revision: ['u', session.revision], viewer: ['s', token],
            windowAssociation: ['s', session.association ?? ''],
            sampledAtUsec: ['s', String(GLib.get_monotonic_time())],
        };
    }

    _observe(session, token, allowUnfocused = false) {
        const viewer = this._viewer(session, token);
        const actors = global.get_window_actors();
        if (actors.length > MAX_WINDOWS)
            return {values: this._base(session, token, 'too-many-windows')};
        const owned = actors.filter(actor => {
            const window = actor.get_meta_window();
            return !actor.is_destroyed() && window.get_client_type() === Meta.WindowClientType.WAYLAND &&
                viewer.client.owns_window(window);
        });
        const selected = session.association ? associatedWindowIndices(owned.map(actor => {
            const window = actor.get_meta_window();
            return {wmClass: window.get_wm_class(), instance: window.get_wm_class_instance()};
        }), session.association).map(index => owned[index]) : owned;
        const windowActor = selected.length === 1 ? selected[0] : null;
        const window = windowActor?.get_meta_window();
        if (window && session.window && session.window !== window) {
            this._close(session, 'viewer-window-replaced');
            throw new Error('The owned viewer window was replaced');
        }
        if (window && !session.window) {
            session.window = window;
            session.windowToken = uuid();
            this._watch(window, 'position-changed', 'window-position', session.windowSignals, session);
            this._watch(window, 'size-changed', 'window-size', session.windowSignals, session);
            this._watch(window, 'notify::minimized', 'window-minimized', session.windowSignals, session);
            const association = () => ({wmClass: window.get_wm_class(), instance: window.get_wm_class_instance()});
            const associationBinding = {session, viewer, window, windowToken: session.windowToken,
                association: session.association};
            let previousAssociation = association();
            session.windowSignals.push([window, window.connect('notify::wm-class', () => {
                let duplicate = false;
                try {
                    const currentAssociation = association();
                    duplicate = duplicateWindowAssociationNotification(associationBinding, previousAssociation, {
                        ...currentAssociation, session: this._state.current, viewer: session.viewer,
                        window: session.window, windowToken: session.windowToken, association: session.association,
                        live: this._state.matches(session), viewerExited: viewer.exited,
                        owned: viewer.client.owns_window(window),
                        wayland: window.get_client_type() === Meta.WindowClientType.WAYLAND,
                    });
                    const diagnostic = value => [value.wmClass, value.instance]
                        .map(text => String(text ?? '').slice(0, 128));
                    console.log(`Deskflow native association notification ${JSON.stringify({
                        nativeWindow: window.get_stable_sequence(), window: session.windowToken,
                        selected: session.window === window, revision: session.revision,
                        old: diagnostic(previousAssociation), current: diagnostic(currentAssociation), duplicate,
                    })}`);
                    previousAssociation = currentAssociation;
                } catch (_) {
                    // Diagnostics cannot turn a native invalidation into success.
                    duplicate = false;
                }
                if (!duplicate)
                    this._invalidate(session, 'window-association-changed');
            })]);
            session.windowSignals.push([window, window.connect('unmanaged', () => {
                this._invalidate(session, 'viewer-window-unmanaged');
                session.window = null;
                session.surface = null;
                session.windowToken = '';
                session.surfaceToken = '';
            })]);
            this._invalidate(session, 'viewer-window-bound');
        }
        // Only one native surface is supported. Do not treat a popup,
        // subsurface, decoration actor, or matching rectangle as that surface.
        const surfaces = [];
        const pending = windowActor ? [windowActor] : [];
        let visited = 0;
        while (pending.length && visited++ < MAX_ACTORS) {
            const actor = pending.pop();
            if (typeName(actor) === 'MetaSurfaceActorWayland')
                surfaces.push(actor);
            const children = actor.get_children();
            if (visited + pending.length + children.length > MAX_ACTORS) {
                pending.push(actor);
                break;
            }
            pending.push(...children);
        }
        const surface = surfaces.length === 1 && pending.length === 0 ? surfaces[0] : null;
        if (surface !== session.surface) {
            session.surface = surface;
            session.surfaceToken = surface ? uuid() : '';
            this._invalidate(session, 'surface-changed');
        }
        const [pointer, modifiers] = global.backend.get_cursor_tracker().get_pointer();
        const pointerX = pointer.x;
        const pointerY = pointer.y;
        const state = {
            nativeBackend: this._native,
            locked: Main.sessionMode.isLocked || Main.sessionMode.isGreeter ||
                !Main.sessionMode.allowScreencast,
            overview: Main.overview.visible || Main.overview.animationInProgress,
            grabbed: Boolean(global.stage.get_grab_actor()), modal: Main.modalCount !== 0,
            buttonsHeld: Boolean(modifiers & (Clutter.ModifierType.BUTTON1_MASK |
                Clutter.ModifierType.BUTTON2_MASK | Clutter.ModifierType.BUTTON3_MASK |
                Clutter.ModifierType.BUTTON4_MASK | Clutter.ModifierType.BUTTON5_MASK)),
            windowCount: selected.length,
            normalWindow: window?.get_window_type() === Meta.WindowType.NORMAL,
            transient: Boolean(window?.get_transient_for()),
            visible: Boolean(window && !window.minimized && !window.is_hidden() &&
                window.showing_on_its_workspace() && windowActor.is_visible() && windowActor.is_mapped()),
            focused: Boolean(window && global.display.focus_window === window),
            oneSurface: Boolean(surface),
        };
        let reason = unavailableReason(state, allowUnfocused);
        if (![pointerX, pointerY].every(Number.isFinite))
            reason = 'unsupported-pointer-state';
        let frame;
        let buffer;
        if (window) {
            frame = window.get_frame_rect();
            buffer = window.get_buffer_rect();
            if (!validRect(frame) || !validRect(buffer))
                reason = 'unsupported-geometry';
        }
        // Query-time comparison catches changes even if their signal is not
        // exposed to extensions. Retained observations are never renewed.
        const signature = JSON.stringify([state, frame && rectArray(frame), buffer && rectArray(buffer)]);
        if (signature !== session.signature) {
            session.signature = signature;
            this._invalidate(session, 'native-state-changed');
        }
        if (!this._state.matches(session))
            throw new Error('Session retired during observation');
        const values = this._base(session, token, reason);
        if (window) {
            Object.assign(values, {
                window: ['s', session.windowToken], surface: ['s', session.surfaceToken ?? ''],
                pointerX: ['d', pointerX], pointerY: ['d', pointerY],
                focused: ['b', state.focused],
            });
            if (validRect(frame) && validRect(buffer)) {
                values.frameRect = ['(dddd)', rectArray(frame)];
                values.bufferRect = ['(dddd)', rectArray(buffer)];
            }
        }
        return {values, surface, pointerX, pointerY};
    }

    _finishStageCheck(session, check, reason = '') {
        if (!check || session.stageCheck !== check)
            return; // Terminal, late and reentrant callbacks are harmless.
        session.stageCheck = null; // Clear ownership before signals/DBus callbacks.
        removeSource(check.timeout);
        removeSource(check.nextUpdate);
        disconnect(check.signals);
        const valid = !reason && check.sequence.complete &&
            this._state.matches(session) && session.revision === check.revision;
        const result = {
            available: ['b', valid], reason: ['s', valid ? '' : (reason || 'stage-incomplete')],
            observationOnly: ['b', true], deliveryReady: ['b', false],
            session: ['s', session.id], generation: ['u', session.generation],
            viewer: ['s', check.viewer], request: ['s', check.request],
            revision: ['u', session.revision], stageUpdates: ['u', valid ? 2 : check.sequence.count],
            sameStageView: ['b', valid], stageX: ['d', check.x], stageY: ['d', check.y],
        };
        try {
            check.invocation.return_value(new GLib.Variant('(a{sv})', [variants(result)]));
        } catch (_) {
            // The private caller may already have closed; a late reply never
            // grants ownership or survives the invalidated session.
        }
    }

    // A conservative two-cycle Clutter observation, never a native-input
    // barrier: before/after-update signals are not tied to one sender EIS
    // event, and the queue may be re-filled by reentrant dispatch. This
    // request is bound to the actual focused Viewer and frozen stage point.
    ObserveStageFramesAsync([id, generation, token, request, x, y, revision], invocation) {
        let session, check;
        try {
            session = this._lookup(invocation, id, generation);
            if (!this._stageCapable || !validRequest(request, x, y, revision) ||
                session.stageCheck || session.inputDrain)
                throw new Error('Stage update observation unsupported or busy');
            const initial = this._observe(session, token);
            if (initial.values.reason[1] || initial.values.revision[1] !== revision ||
                !initial.surface ||
                global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== initial.surface)
                throw new Error('Frozen point is not on the current owned Viewer');
            const sequence = new StageUpdateSequence(request, x, y, revision, nowMs());
            check = {request, revision, viewer: token, invocation, sequence, x, y, surface: initial.surface,
                signals: [], timeout: 0, nextUpdate: 0};
            session.stageCheck = check; // Before registering any reentrant callbacks.
            const terminate = why => this._finishStageCheck(session, check, why);
            check.signals.push([global.stage, global.stage.connect('before-update', (_stage, view) => {
                if (session.stageCheck !== check)
                    return;
                try {
                    // Mutter 50 exposes the StageView geometry as the
                    // boxed `layout` GObject property. get_layout() has a
                    // native out parameter and is not a callable zero-argument
                    // GJS accessor. Sequence.before copies four scalars so
                    // this borrowed boxed rectangle is never retained.
                    sequence.before(view, view?.layout, nowMs());
                    if (sequence.reason)
                        terminate(sequence.reason);
                } catch (_) {
                    terminate('unsupported-stage-view-layout');
                }
            })]);
            check.signals.push([global.stage, global.stage.connect('after-update', (_stage, view) => {
                if (session.stageCheck !== check)
                    return;
                try {
                    const completed = sequence.after(view, nowMs());
                    if (sequence.reason) {
                        terminate(sequence.reason);
                        return;
                    }
                    if (view !== sequence.view)
                        return;
                    const current = this._observe(session, token);
                    if (session.stageCheck !== check)
                        return; // Observation invalidated itself via native signal.
                    if (current.values.reason[1] || current.values.revision[1] !== revision ||
                        current.surface !== check.surface ||
                        global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== check.surface) {
                        terminate('stage-hit-or-focus-changed');
                        return;
                    }
                    if (completed) {
                        this._finishStageCheck(session, check);
                    } else if (!check.nextUpdate) {
                        // Schedule the second frame only after the first
                        // after-update unwinds and Mutter clears its update flag.
                        check.nextUpdate = GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                            check.nextUpdate = 0;
                            if (session.stageCheck === check)
                                global.stage.schedule_update();
                            return GLib.SOURCE_REMOVE;
                        });
                    }
                } catch (_) {
                    terminate('native-stage-check-failed');
                }
            })]);
            check.timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 120, () => {
                check.timeout = 0;
                terminate('stage-update-deadline');
                return GLib.SOURCE_REMOVE;
            });
            global.stage.schedule_update(); // No input posting or new display connection.
        } catch (error) {
            if (check && session?.stageCheck === check)
                this._finishStageCheck(session, check, 'stage-start-failed');
            else
                errorReply(invocation, error);
        }
    }

    _finishInputDrain(session, check, reason = '') {
        if (!check || session.inputDrain !== check)
            return;
        session.inputDrain = null;
        removeSource(check.timeout);
        removeSource(check.idle);
        disconnect(check.signals);
        const valid = !reason && check.sequence.complete && this._state.matches(session) &&
            session.revision === check.revision && nowMs() < check.sequence.deadline;
        const ordering = check.ordering && orderingStillMapped(check.ordering) ? check.ordering.contract : '';
        const binding = Object.fromEntries(Object.entries(check.binding).map(([key, value]) =>
            [key, [(check.preparation ? INPUT_PREPARATION_BINDING_TYPES : INPUT_DRAIN_BINDING_TYPES)[key], value]]));
        if (valid && check.preparation && !check.cleanup && session.preparation &&
            sameInputPreparationBinding(session.preparation.binding, check.binding))
            session.preparation.drained = true;
        try {
            check.invocation.return_value(new GLib.Variant('(a{sv})', [variants({
                available: ['b', valid], reason: ['s', valid ? '' : (reason || 'input-drain-incomplete')],
                session: ['s', session.id], generation: ['u', session.generation],
                viewer: ['s', check.viewer], request: ['s', check.request], revision: ['u', check.revision],
                stageX: ['d', check.x], stageY: ['d', check.y], binding: ['a{sv}', variants(binding)],
                segment: ['s', 'clutter-queue-and-stage'],
                orderingLimit: ['s', ordering ? '' : ORDERING_LIMIT],
                orderingContract: ['s', ordering],
            })]));
        } catch (_) {
            // Retired before replying. A vanished caller cannot retain a result.
        }
    }

    // The caller orders this operation after its exact owned sender PONG. GI
    // itself has no HAVE_EIS_EVENT_REF introspection. The separately verified
    // mapped-build contract proves that prerequisite on two supported builds;
    // unknown builds retain a compositor segment without sender ordering. It does
    // not validate the caller's All fence; the subsequent private Qt operation
    // must verify the same binding before the caller may use this segment.
    DrainInputPrefixAsync([id, generation, token, request, x, y, revision, _binding, budgetMs], invocation) {
        this._beginInputDrain([id, generation, token, request, x, y, revision, _binding, budgetMs], invocation, false);
    }

    DrainInputPreparationAsync(args, invocation) {
        this._beginInputDrain(args, invocation, true);
    }

    _beginInputDrain([id, generation, token, request, x, y, revision, _binding, budgetMs, cleanup = false], invocation, preparation) {
        let session, check;
        try {
            session = this._lookup(invocation, id, generation);
            const binding = inputDrainBinding(invocation, preparation);
            if (!this._drainCapable || session.stageCheck || session.inputDrain || session.preparationActivation ||
                !validRoutedPointRequest(request, x, y, revision) || !Number.isInteger(budgetMs) ||
                budgetMs <= 0 || budgetMs > 120)
                throw new Error('Input drain unsupported, malformed or busy');
            if (preparation && (!session.preparation ||
                !sameInputPreparationBinding(session.preparation.binding, binding) ||
                session.preparation.x !== x || session.preparation.y !== y ||
                (!cleanup && nowMs() >= session.preparation.deadline)))
                throw new Error('Input preparation changed or expired');
            const previous = preparation ? null : session.lastDrainCutoff;
            if (previous && (binding.cutoffRequest.length < previous.length ||
                (binding.cutoffRequest.length === previous.length && binding.cutoffRequest <= previous)))
                throw new Error('Input cutoff request was already retired');
            const initial = this._observe(session, token, preparation);
            if (initial.values.reason[1] || initial.values.revision[1] !== revision || !initial.surface ||
                (!cleanup && global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== initial.surface))
                throw new Error('Frozen point is not on the current owned Viewer');
            const sequence = new InputDrainSequence(request, x, y, revision, nowMs(), budgetMs);
            check = {request, revision, viewer: token, invocation, binding, sequence, x, y, preparation, cleanup,
                surface: initial.surface, signals: [], timeout: 0, idle: 0,
                ordering: orderingStillMapped(this._ordering) ? this._ordering : null};
            if (!preparation)
                session.lastDrainCutoff = binding.cutoffRequest;
            session.inputDrain = check;
            const terminate = reason => this._finishInputDrain(session, check, reason);
            const current = () => {
                const observed = this._observe(session, token, preparation);
                return session.inputDrain === check && !observed.values.reason[1] &&
                    observed.values.revision[1] === revision && observed.surface === check.surface &&
                    (cleanup || global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) === check.surface);
            };
            check.signals.push([global.stage, global.stage.connect('before-update', (_stage, view) => {
                if (session.inputDrain !== check)
                    return;
                try {
                    sequence.before(view, view?.layout, GLib.main_depth(), nowMs());
                    if (sequence.reason)
                        terminate(sequence.reason);
                    else if (sequence.view === view && !current())
                        terminate('input-drain-hit-or-focus-changed');
                } catch (_) {
                    terminate('unsupported-input-drain-stage');
                }
            })]);
            check.signals.push([global.stage, global.stage.connect('after-update', (_stage, view) => {
                if (session.inputDrain !== check)
                    return;
                try {
                    const completed = sequence.after(view, view?.layout, GLib.main_depth(), nowMs());
                    if (sequence.reason)
                        terminate(sequence.reason);
                    else if (completed) {
                        if (!current())
                            terminate('input-drain-hit-or-focus-changed');
                        else
                            this._finishInputDrain(session, check);
                    }
                } catch (_) {
                    terminate('unsupported-input-drain-stage');
                }
            })]);
            check.timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, budgetMs, () => {
                check.timeout = 0;
                terminate('input-drain-timeout');
                return GLib.SOURCE_REMOVE;
            });
            // Mutter's global Clutter event source has default priority. This
            // lower-priority idle cannot jump ahead of queued events, and the
            // depth check excludes events already popped by recursive dispatch.
            check.idle = GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
                if (session.inputDrain !== check)
                    return GLib.SOURCE_REMOVE;
                try {
                    const ready = sequence.queueEmpty(Clutter.events_pending(), GLib.main_depth(), nowMs());
                    if (sequence.reason) {
                        check.idle = 0;
                        terminate(sequence.reason);
                        return GLib.SOURCE_REMOVE;
                    }
                    if (!ready)
                        return GLib.SOURCE_CONTINUE;
                    check.idle = 0;
                    if (!current())
                        terminate('input-drain-hit-or-focus-changed');
                    else
                        global.stage.schedule_update();
                    return GLib.SOURCE_REMOVE;
                } catch (_) {
                    check.idle = 0;
                    terminate('unsupported-input-drain-queue');
                    return GLib.SOURCE_REMOVE;
                }
            });
        } catch (error) {
            if (check && session?.inputDrain === check)
                this._finishInputDrain(session, check, 'input-drain-start-failed');
            else
                errorReply(invocation, error);
        }
    }

    GetViewerAsync([id, generation, token], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            const {values} = this._observe(session, token);
            invocation.return_value(new GLib.Variant('(a{sv})', [variants(values)]));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    GetViewerWithAssociationAsync([id, generation, token, association], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            this._viewer(session, token);
            if (!validWindowAssociation(association) ||
                (session.preparation && session.preparation.association !== association))
                throw new Error('Malformed or preparation-owned per-window association');
            this._selectAssociation(session, association);
            const {values} = this._observe(session, token);
            invocation.return_value(new GLib.Variant('(a{sv})', [variants(values)]));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    _selectAssociation(session, association) {
        if (session.association === association)
            return;
        disconnect(session.windowSignals);
        session.windowSignals = [];
        session.window = null;
        session.surface = null;
        session.windowToken = '';
        session.surfaceToken = '';
        session.signature = '';
        session.association = association;
        this._invalidate(session, 'viewer-association-selected');
    }

    _preparationReply(session, token, request, binding, x, y, observed, reason = '') {
        const surface = observed?.surface;
        let localX = 0, localY = 0;
        if (surface) {
            const [ok, lx, ly] = surface.transform_stage_point(x, y);
            if (ok && Number.isFinite(lx) && Number.isFinite(ly)) {
                localX = lx;
                localY = ly;
            } else {
                reason ||= 'unsupported-surface-transform';
            }
        }
        const fields = Object.fromEntries(Object.entries(binding).map(([key, value]) =>
            [key, [INPUT_PREPARATION_BINDING_TYPES[key], value]]));
        return new GLib.Variant('(a{sv})', [variants({
            available: ['b', !reason], reason: ['s', reason], session: ['s', session.id],
            generation: ['u', session.generation], viewer: ['s', token], request: ['s', request],
            binding: ['a{sv}', variants(fields)], association: ['s', session.association ?? ''],
            window: ['s', session.windowToken ?? ''], surface: ['s', session.surfaceToken ?? ''],
            revision: ['u', session.revision], stageX: ['d', x], stageY: ['d', y],
            localX: ['d', localX], localY: ['d', localY], focused: ['b', Boolean(observed?.values.focused?.[1])],
            orderingContract: ['s', orderingStillMapped(this._ordering) ? this._ordering.contract : ''],
        })]);
    }

    SelectInputPreparationAsync([id, generation, token, request, x, y, associations, _binding, budgetMs], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            const binding = inputDrainBinding(invocation, true);
            this._viewer(session, token);
            if (!this._drainCapable || !orderingStillMapped(this._ordering) || session.preparation ||
                !validRoutedPointRequest(request, x, y, 1) || !Number.isInteger(budgetMs) || budgetMs < 1 || budgetMs > 500 ||
                !Array.isArray(associations) || !associations.length || associations.length > 16 ||
                associations.some(value => !validWindowAssociation(value)) || new Set(associations).size !== associations.length)
                throw new Error('Input preparation unsupported, malformed or busy');
            const deadline = nowMs() + budgetMs;
            let selected = '', anchor = '';
            for (const association of associations) {
                if (nowMs() >= deadline)
                    throw new Error('Input preparation selection expired');
                this._selectAssociation(session, association);
                const observed = this._observe(session, token, true);
                if (observed.values.reason[1] || !observed.surface)
                    continue;
                anchor ||= association;
                if (global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) === observed.surface) {
                    if (selected)
                        throw new Error('Ambiguous input preparation hit');
                    selected = association;
                }
            }
            const association = selected || anchor;
            if (!association)
                throw new Error('No native cleanup anchor for input preparation');
            this._selectAssociation(session, association);
            const observed = this._observe(session, token, true);
            if (observed.values.reason[1] || !observed.surface || nowMs() >= deadline)
                throw new Error('Input preparation target changed');
            session.preparation = {binding, x, y, deadline, association, surface: observed.surface,
                window: session.window, drained: false, activated: false};
            invocation.return_value(this._preparationReply(session, token, request, binding, x, y, observed,
                selected ? '' : 'preparation-no-target'));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    _preparationCurrent(session, token, binding, x, y, cleanup) {
        const preparation = session.preparation;
        if (!preparation || !sameInputPreparationBinding(preparation.binding, binding) ||
            preparation.x !== x || preparation.y !== y || session.association !== preparation.association ||
            (!cleanup && nowMs() >= preparation.deadline))
            throw new Error('Input preparation changed or expired');
        const observed = this._observe(session, token, cleanup || !preparation.activated);
        if (observed.values.reason[1] || observed.surface !== preparation.surface || session.window !== preparation.window ||
            (!cleanup && global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== observed.surface))
            throw new Error('Input preparation native target changed');
        return observed;
    }

    QueryInputPreparationAsync([id, generation, token, request, x, y, _revision, _binding, cleanup], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            const binding = inputDrainBinding(invocation, true);
            if (!validRoutedPointRequest(request, x, y, 1) || session.preparationActivation)
                throw new Error('Malformed input preparation point');
            const observed = this._preparationCurrent(session, token, binding, x, y, cleanup);
            invocation.return_value(this._preparationReply(session, token, request, binding, x, y, observed));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    _finishPreparationActivation(session, check, reason = '') {
        if (!check || session.preparationActivation !== check)
            return;
        session.preparationActivation = null;
        removeSource(check.timeout);
        removeSource(check.nextUpdate);
        disconnect(check.signals);
        try {
            if (reason || !check.sequence.complete || nowMs() >= check.sequence.deadline ||
                !this._state.matches(session) || !orderingStillMapped(this._ordering))
                throw new Error(reason || 'activation-completion-unavailable');
            const observed = this._preparationCurrent(session, check.viewer, check.binding, check.x, check.y, false);
            if (observed.surface !== check.surface || session.window !== check.window || session.signature !== check.signature ||
                session.revision !== check.revision + check.sequence.restacks)
                throw new Error('activation-target-or-geometry-changed');
            console.log(`Deskflow preparation activation completed request=${check.request} ` +
                `revision=${check.revision}->${session.revision} restacks=${check.sequence.restacks}`);
            check.invocation.return_value(this._preparationReply(session, check.viewer, check.request,
                check.binding, check.x, check.y, observed));
        } catch (error) {
            console.log(`Deskflow preparation activation refused request=${check.request} ` +
                `revision=${check.revision}->${session.revision} restacks=${check.sequence.restacks} ` +
                `reason=${String(error.message).slice(0, 96)} signal=${check.lastInvalidation}`);
            errorReply(check.invocation, error);
        }
    }

    ActivateInputPreparationAsync([id, generation, token, request, x, y, revision, _binding, budgetMs], invocation) {
        let session, check;
        try {
            session = this._lookup(invocation, id, generation);
            const binding = inputDrainBinding(invocation, true);
            let observed = this._preparationCurrent(session, token, binding, x, y, false);
            const preparation = session.preparation;
            if (!preparation.drained || preparation.activated || session.preparationActivation ||
                revision !== session.revision || !Number.isInteger(budgetMs) || budgetMs < 1 || budgetMs > 500 ||
                GLib.main_depth() !== 1 || Clutter.events_pending() || !orderingStillMapped(this._ordering))
                throw new Error('Input preparation prefix is not ready for activation');
            // Exact retained MetaWindow, never a title/PID/rectangle lookup.
            // activate() focuses synchronously but its raise queues a later
            // META_LATER_SYNC_STACK. A reply here would retain the pre-restack
            // revision while the next frame may still change it.
            const timestamp = global.display.get_current_time_roundtrip();
            this._preparationCurrent(session, token, binding, x, y, false);
            preparation.window.activate(timestamp);
            preparation.activated = true;
            observed = this._preparationCurrent(session, token, binding, x, y, false);
            const started = nowMs();
            const budget = Math.min(120, budgetMs, preparation.deadline - started);
            const sequence = new PreparationActivationSequence(request, x, y, session.revision, started, budget);
            check = {request, revision: session.revision, viewer: token, invocation, binding, x, y, sequence,
                window: session.window, surface: observed.surface, signature: session.signature,
                signals: [], timeout: 0, nextUpdate: 0, lastInvalidation: ''};
            session.preparationActivation = check;
            const terminate = reason => this._finishPreparationActivation(session, check, reason);
            const current = () => {
                const actual = this._preparationCurrent(session, token, binding, x, y, false);
                return session.preparationActivation === check && actual.surface === check.surface &&
                    session.window === check.window && session.signature === check.signature;
            };
            check.signals.push([global.stage, global.stage.connect('before-update', (_stage, view) => {
                if (session.preparationActivation !== check)
                    return;
                try {
                    sequence.before(view, view?.layout, GLib.main_depth(), nowMs());
                    if (sequence.reason)
                        terminate(sequence.reason);
                    else if (sequence.view === view && !current())
                        terminate('activation-target-or-geometry-changed');
                } catch (_) {
                    terminate('activation-stage-unavailable');
                }
            })]);
            check.signals.push([global.stage, global.stage.connect('after-update', (_stage, view) => {
                if (session.preparationActivation !== check)
                    return;
                try {
                    const completed = sequence.after(view, view?.layout, GLib.main_depth(), nowMs());
                    if (sequence.reason)
                        terminate(sequence.reason);
                    else if (sequence.view === view) {
                        if (!current())
                            terminate('activation-target-or-geometry-changed');
                        else if (completed)
                            this._finishPreparationActivation(session, check);
                        else if (!check.nextUpdate)
                            check.nextUpdate = GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                                check.nextUpdate = 0;
                                if (session.preparationActivation === check)
                                    global.stage.schedule_update();
                                return GLib.SOURCE_REMOVE;
                            });
                    }
                } catch (_) {
                    terminate('activation-stage-unavailable');
                }
            })]);
            check.timeout = GLib.timeout_add(GLib.PRIORITY_DEFAULT, Math.max(1, Math.ceil(budget)), () => {
                check.timeout = 0;
                terminate('activation-completion-deadline');
                return GLib.SOURCE_REMOVE;
            });
            global.stage.schedule_update();
        } catch (error) {
            if (check && session?.preparationActivation === check)
                this._finishPreparationActivation(session, check, 'activation-start-failed');
            else
                errorReply(invocation, error);
        }
    }

    FinishInputPreparationAsync([id, generation, token, _request, _x, _y, _revision, _binding, _budgetMs], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            this._viewer(session, token);
            const binding = inputDrainBinding(invocation, true);
            if (!session.preparation || !sameInputPreparationBinding(session.preparation.binding, binding) ||
                session.inputDrain || session.preparationActivation)
                throw new Error('Input preparation retirement changed its binding');
            session.preparation = null;
            invocation.return_value(new GLib.Variant('()', []));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    ValidateCurrentHitAsync(args, invocation) {
        this._validateHit(args, invocation, true);
    }

    ValidateRoutedPointAsync(args, invocation) {
        this._validateHit(args, invocation, false);
    }

    ValidateRoutedPointsAsync([id, generation, token, request, epoch, points, revision], invocation) {
        try {
            const session = this._lookup(invocation, id, generation);
            if (!validRoutedPoints(request, epoch, points, revision))
                throw new Error('Malformed native-hit batch');
            const observed = this._observe(session, token);
            const {values, surface} = observed;
            let reason = values.reason[1];
            if (revision !== session.revision)
                reason ||= 'stale-revision';
            const mapped = [];
            if (!reason) {
                for (const [cutoff, sequence, part, x, y] of points) {
                    if (global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== surface) {
                        reason = 'not-viewer-surface';
                        break;
                    }
                    const [ok, localX, localY] = surface.transform_stage_point(x, y);
                    if (!ok || !Number.isFinite(localX) || !Number.isFinite(localY)) {
                        reason = 'unsupported-surface-transform';
                        break;
                    }
                    mapped.push([cutoff, sequence, part, x, y, localX, localY]);
                }
            }
            const fresh = this._observe(session, token);
            if (session.revision !== revision || fresh.surface !== surface || fresh.values.reason[1])
                reason ||= 'state-changed-during-query';
            if (!reason) {
                for (const [, , , x, y, localX, localY] of mapped) {
                    const [ok, freshX, freshY] = surface.transform_stage_point(x, y);
                    if (!ok || freshX !== localX || freshY !== localY ||
                        global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== surface) {
                        reason = 'state-changed-during-query';
                        break;
                    }
                }
            }
            if (session.revision !== revision || !this._state.matches(session))
                reason ||= 'state-changed-during-query';
            invocation.return_value(new GLib.Variant('(a{sv})', [variants({
                session: ['s', session.id], generation: ['u', session.generation], viewer: ['s', token],
                revision: ['u', session.revision], request: ['s', request], captureEpoch: ['s', epoch],
                window: ['s', session.windowToken ?? ''], surface: ['s', session.surfaceToken ?? ''],
                available: ['b', !reason], reason: ['s', reason],
                points: ['a(ssudddd)', reason ? [] : mapped],
            })]));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    // Both operations are read-only. A routed point is a requested stage
    // coordinate, never the last sampled pointer position. This validates
    // only the live native hit/identity; it does not order EIS or Qt queues,
    // prove the source coordinate namespace or authorize input delivery.
    _validateHit([id, generation, token, request, x, y, revision], invocation, currentPointer) {
        try {
            const session = this._lookup(invocation, id, generation);
            if (!(currentPointer ? validRequest(request, x, y, revision) :
                    validRoutedPointRequest(request, x, y, revision)))
                throw new Error('Malformed native-hit request');
            const observed = this._observe(session, token);
            const {values, surface, pointerX, pointerY} = observed;
            values.request = ['s', request];
            if (!currentPointer) {
                values.routedX = ['d', x];
                values.routedY = ['d', y];
            }
            let reason = values.reason[1];
            if (revision !== session.revision)
                reason = 'stale-revision';
            if (!reason && currentPointer && !samePointerPosition(x, y, pointerX, pointerY))
                reason = 'pointer-mismatch';
            if (!reason && global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== surface)
                reason = 'not-viewer-surface';
            if (!reason) {
                const [ok, localX, localY] = surface.transform_stage_point(x, y);
                if (!ok || !Number.isFinite(localX) || !Number.isFinite(localY)) {
                    reason = 'unsupported-surface-transform';
                } else {
                    values.localX = ['d', localX];
                    values.localY = ['d', localY];
                }
            }
            // A native call can emit geometry/focus signals synchronously.
            // Recheck identity and the actual input hit after that call; a
            // previously hidden or newly occluded Viewer must not validate.
            const fresh = this._observe(session, token);
            if (session.revision !== revision || fresh.surface !== surface ||
                fresh.values.reason[1] ||
                (currentPointer && !samePointerPosition(x, y, fresh.pointerX, fresh.pointerY)) ||
                (!reason && global.stage.get_actor_at_pos(Clutter.PickMode.REACTIVE, x, y) !== surface))
                reason ||= 'state-changed-during-query';
            values.revision = ['u', session.revision];
            values.sampledAtUsec = fresh.values.sampledAtUsec;
            values.available = ['b', !reason];
            values.reason = ['s', reason];
            if (reason) {
                delete values.localX;
                delete values.localY;
            }
            invocation.return_value(new GLib.Variant('(a{sv})', [variants(values)]));
        } catch (error) {
            errorReply(invocation, error);
        }
    }

    _close(session, reason) {
        if (!session || session.cleaned)
            return;
        session.cleaned = true;
        this._finishPreparationActivation(session, session.preparationActivation, 'viewer-or-provider-closed');
        this._finishStageCheck(session, session.stageCheck, 'viewer-or-provider-closed');
        this._finishInputDrain(session, session.inputDrain, 'viewer-or-provider-closed');
        this._state.detach(session);
        session.cancel?.cancel();
        removeSource(session.timeout);
        session.timeout = 0;
        disconnect(session.signals ?? []);
        disconnect(session.windowSignals ?? []);
        if (session.closedSignal && session.connection)
            session.connection.signal_unsubscribe(session.closedSignal);
        closeConnection(session.connection);
        session.connection = null;
        if (session.invocation) {
            const pending = session.invocation;
            session.invocation = null;
            errorReply(pending, reason);
        }
        const viewer = session.viewer;
        if (viewer && !viewer.exited) {
            // Reap only the process launched by this provider. This remains
            // asynchronous when the extension is disabled or a caller dies.
            viewer.killSource = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 1000, () => {
                viewer.killSource = 0;
                if (!viewer.exited)
                    viewer.process.force_exit();
                return GLib.SOURCE_REMOVE;
            });
            viewer.process.send_signal(15);
        }
        this._object?.emit_signal('Closed',
            new GLib.Variant('(sus)', [session.id, session.generation, reason]));
    }

    stop() {
        if (this._state.closed)
            return;
        const session = this._state.shutdown();
        this._cancellable.cancel();
        this._close(session, 'provider-disabled');
        disconnect(this._signals);
        if (this._ownerSubscription)
            this._connection.signal_unsubscribe(this._ownerSubscription);
        this._ownerSubscription = 0;
        this._object?.unexport();
        this._object = null;
        if (this._nameId)
            Gio.bus_unown_name(this._nameId);
        this._nameId = 0;
    }
}

export default class SeamlessObservationExtension extends Extension {
    enable() {
        const provider = new Provider(this.path);
        this._provider = provider;
        provider.start().catch(error => {
            console.error(`Deskflow GNOME provider unavailable: ${error.message}`);
            provider.stop();
        });
    }

    disable() {
        this._provider?.stop();
        this._provider = null;
    }
}
