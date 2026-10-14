// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT

// Pure policy only. Native identities and hit results are supplied by the real
// compositor adapter; these helpers cannot mint an input-delivery capability.
export const BUS_NAME = 'org.deskflow.Seamless.Gnome50';
export const OBJECT_PATH = '/org/deskflow/Seamless/Gnome50';
export const UINT32_MAX = 0xffffffff;
export const STARTUP_MS = 10000;
export const MAX_WINDOWS = 256;
export const MAX_ACTORS = 256;
export const MAX_COORDINATE = 0x1000000;

// The source/build/binary provenance for this closed list is reproduced by
// verify-ordering-build.py. An environment variable or installed manifest cannot
// add a build. A trusted local desktop (including its loader/system libraries)
// is required; this is not attestation against root or in-process modification.
export const ORDERING_LIMIT = 'mutter-eis-sync-build-unverified';
export const ORDERING_MAIN = '/usr/lib/x86_64-linux-gnu/libmutter-18.so.0.0.0';
export const ORDERING_CLUTTER = '/usr/lib/x86_64-linux-gnu/mutter-18/libmutter-clutter-18.so.0.0.0';
export const ORDERING_MAPS_MAX = 2 * 1024 * 1024;
export const ORDERING_FILE_MAX = 8 * 1024 * 1024;
export const ORDERING_BUILDS = Object.freeze([
    Object.freeze({package: '50.1-0ubuntu2.2',
        main: 'fec9023139ac8eff93fcc1af90413613cdb526e7f02389dc9d8fe90f7abc81e5',
        clutter: '1c15e51d1b1042b178e3a17d8e194894193e585a272bd56620a7cf52823ac56c'}),
    Object.freeze({package: '50.1-0ubuntu2.4',
        main: '4d7ece1588279dbd3b66de3a28d1bf1098e7905b705d8e44743192b925d66b83',
        clutter: 'f1823cc61eeae023cbf8c56b571316f41ceff8219096da14ecf31fdc2671b1b0'}),
]);

export function mappedOrderingModules(maps) {
    if (typeof maps !== 'string' || maps.length > ORDERING_MAPS_MAX || maps.includes('\0'))
        throw new Error('unsupported-mutter-mappings');
    const modules = new Map();
    for (const line of maps.split('\n')) {
        if (!/libmutter-(?:clutter-)?18\.so/.test(line))
            continue;
        const match = /^([0-9a-f]+)-([0-9a-f]+) ([r-][w-][x-][ps]) ([0-9a-f]+) ([0-9a-f]+):([0-9a-f]+) ([0-9]+)\s+(\/[^\n]+)$/.exec(line);
        if (!match)
            throw new Error('unsupported-mutter-mapping');
        const [, start, end, permissions, offset, major, minor, inode, path] = match;
        if (![ORDERING_MAIN, ORDERING_CLUTTER].includes(path) || BigInt(`0x${start}`) >= BigInt(`0x${end}`) ||
            BigInt(inode) === 0n || permissions[3] !== 'p' || permissions.startsWith('-') ||
            (permissions.includes('w') && permissions.includes('x')))
            throw new Error('unsupported-mutter-mapping');
        const identity = {path, major: BigInt(`0x${major}`).toString(),
            minor: BigInt(`0x${minor}`).toString(), inode: BigInt(inode).toString()};
        const old = modules.get(path);
        if (old && (old.major !== identity.major || old.minor !== identity.minor || old.inode !== identity.inode))
            throw new Error('ambiguous-mutter-mapping');
        const entry = old ?? {...identity, executable: false, mappings: []};
        entry.executable ||= permissions.includes('x');
        entry.mappings.push(`${start}-${end} ${permissions} ${offset}`);
        modules.set(path, entry);
    }
    const result = [ORDERING_MAIN, ORDERING_CLUTTER].map(path => modules.get(path));
    if (result.some(entry => !entry?.executable))
        throw new Error('missing-mutter-mapping');
    return result;
}

export function matchesMappedOrderingFile(mapped, file) {
    if (!file || ![file.device, file.inode, file.size, file.mode, file.uid].every(Number.isSafeInteger) ||
        file.device < 0 || file.inode <= 0 || file.size <= 0 || file.size > ORDERING_FILE_MAX ||
        file.uid !== 0 || (file.mode & 0o170000) !== 0o100000 || (file.mode & 0o022) !== 0)
        return false;
    // Linux gnu_dev_major/minor, evaluated exactly rather than with JS's
    // truncating 32-bit bitwise operators. GIO supplies unix::device/inode.
    const dev = BigInt(file.device);
    const major = ((dev >> 8n) & 0xfffn) | ((dev >> 32n) & 0xfffff000n);
    const minor = (dev & 0xffn) | ((dev >> 12n) & 0xffffff00n);
    return major.toString() === mapped.major && minor.toString() === mapped.minor &&
        BigInt(file.inode).toString() === mapped.inode;
}

export function orderingContractFor(files) {
    if (!Array.isArray(files) || files.length !== 2 || files[0].path !== ORDERING_MAIN ||
        files[1].path !== ORDERING_CLUTTER || files[0].size !== 2755048 || files[1].size !== 1104608)
        return '';
    const build = ORDERING_BUILDS.find(item => item.main === files[0].sha256 && item.clutter === files[1].sha256);
    return build ? `sha256:${build.main}` : '';
}

export function supportedVersion(version) {
    return typeof version === 'string' && /^50(?:\.\d+)*$/.test(version);
}

export function validLaunch(executable, args) {
    return typeof executable === 'string' && executable.startsWith('/') &&
        executable.length <= 4096 && !executable.includes('\0') &&
        Array.isArray(args) && args.length <= 64 &&
        args.every(a => typeof a === 'string' && a.length <= 4096 && !a.includes('\0') &&
            !/^-{1,2}(platform|platformpluginpath|plugin)(=|$)/i.test(a) &&
            !/^-{1,2}gnome-viewport-fd(=|$)/i.test(a) &&
            !/^QT_(QPA_PLATFORM|PLUGIN_PATH)=/.test(a)) &&
        args.reduce((n, a) => n + a.length, 0) <= 16384;
}

export function validViewportHandle(handle, count, declaredCount) {
    return handle === 0 && count === 1 && declaredCount === 1;
}

export function validRequest(request, x, y, revision) {
    return typeof request === 'string' && /^[A-Za-z0-9_-]{1,64}$/.test(request) &&
        [x, y].every(n => Number.isFinite(n) && Math.abs(n) <= MAX_COORDINATE) &&
        Number.isInteger(revision) && revision > 0 && revision <= UINT32_MAX;
}

// Native Clutter hit/transform APIs use float stage coordinates. Reject a
// non-representable double instead of silently validating its rounded neighbor.
export function validRoutedPointRequest(request, x, y, revision) {
    return validRequest(request, x, y, revision) && Math.abs(x) <= 10000000 &&
        Math.abs(y) <= 10000000 && Math.fround(x) === x && Math.fround(y) === y;
}

export function validRoutedPoints(request, epoch, points, revision) {
    const positive = value => typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value) &&
        BigInt(value) <= 0xffffffffffffffffn;
    if (!positive(request) || !positive(epoch) || !Array.isArray(points) || points.length < 1 || points.length > 16)
        return false;
    let lastRequest = 0n;
    let lastSequence = 0n;
    let lastPart = 0;
    for (const point of points) {
        if (!Array.isArray(point) || point.length !== 5)
            return false;
        const [cutoff, sequence, part, x, y] = point;
        if (!positive(cutoff) || !positive(sequence) || !Number.isInteger(part) || part < 1 || part > 16 ||
            !validRoutedPointRequest(cutoff, x, y, revision) || BigInt(cutoff) <= lastRequest ||
            BigInt(sequence) < lastSequence || (BigInt(sequence) === lastSequence && part <= lastPart))
            return false;
        lastRequest = BigInt(cutoff);
        lastSequence = BigInt(sequence);
        lastPart = part;
    }
    return true;
}
export function validWindowAssociation(value) {
    return typeof value === 'string' && /^org\.deskflow\.window\.[0-9a-f]{64}$/.test(value);
}
export function associatedWindowIndices(windows, association) {
    if (!validWindowAssociation(association) || !Array.isArray(windows) || windows.length > MAX_WINDOWS)
        return [];
    return windows.flatMap((window, index) => window?.wmClass === association && window?.instance === association ? [index] : []);
}
export function duplicateWindowAssociationNotification(binding, previous, current) {
    const object = value => value !== null && typeof value === 'object';
    if (!object(binding) || !object(previous) || !object(current))
        return false;
    // Mutter notifies wm-class even when both values are unchanged. This only
    // suppresses that no-op for the already retained exact native incarnation;
    // it does not bind a replacement or restore any previously revoked proof.
    return object(binding.session) && object(binding.viewer) && object(binding.window) &&
        binding.session === current.session && binding.viewer === current.viewer && binding.window === current.window &&
        typeof binding.windowToken === 'string' && binding.windowToken.length > 0 &&
        binding.windowToken === current.windowToken && current.live === true && current.viewerExited === false &&
        current.owned === true && current.wayland === true && validWindowAssociation(binding.association) &&
        binding.association === current.association && previous.wmClass === binding.association &&
        previous.instance === binding.association && current.wmClass === binding.association &&
        current.instance === binding.association;
}

export const INPUT_DRAIN_BINDING_TYPES = Object.freeze({
    cutoffRequest: 's', coreConnectionGeneration: 'u', captureEpoch: 's', captureSequence: 's',
    capturePart: 'u', viewerConnection: 's', ownerSession: 's', ownerEpoch: 's',
    forwardingGeneration: 's', focusRequest: 's', focusRevision: 's', geometry: 's', scope: 's',
});

export const INPUT_PREPARATION_BINDING_TYPES = Object.freeze({
    preparationRequest: 's', coreConnectionGeneration: 'u', captureEpoch: 's',
    captureSequence: 's', capturePart: 'u',
});
export function validInputPreparationBinding(binding) {
    const keys = Object.keys(INPUT_PREPARATION_BINDING_TYPES);
    return binding && typeof binding === 'object' && Object.keys(binding).length === keys.length &&
        keys.every(key => Object.hasOwn(binding, key)) &&
        Number.isInteger(binding.coreConnectionGeneration) && binding.coreConnectionGeneration > 0 &&
        binding.coreConnectionGeneration <= UINT32_MAX && Number.isInteger(binding.capturePart) &&
        binding.capturePart >= 1 && binding.capturePart <= 16 &&
        ['preparationRequest', 'captureEpoch', 'captureSequence'].every(key =>
            typeof binding[key] === 'string' && /^[1-9][0-9]{0,19}$/.test(binding[key]) &&
            BigInt(binding[key]) <= 0xffffffffffffffffn);
}
export function sameInputPreparationBinding(first, second) {
    return validInputPreparationBinding(first) && validInputPreparationBinding(second) &&
        Object.keys(INPUT_PREPARATION_BINDING_TYPES).every(key => first[key] === second[key]);
}

export function validInputDrainBinding(binding) {
    const keys = Object.keys(INPUT_DRAIN_BINDING_TYPES);
    if (!binding || typeof binding !== 'object' || Object.keys(binding).length !== keys.length ||
        !keys.every(k => Object.hasOwn(binding, k)) || binding.scope !== 'all' ||
        !Number.isInteger(binding.coreConnectionGeneration) || binding.coreConnectionGeneration <= 0 ||
        binding.coreConnectionGeneration > UINT32_MAX || !Number.isInteger(binding.capturePart) ||
        binding.capturePart <= 0 || binding.capturePart > 16)
        return false;
    const host = new Set(['ownerSession', 'ownerEpoch', 'forwardingGeneration',
        'focusRequest', 'focusRevision', 'geometry']);
    return keys.filter(k => INPUT_DRAIN_BINDING_TYPES[k] === 's' && k !== 'scope').every(k => {
        const value = binding[k];
        if (typeof value !== 'string' || !/^[1-9][0-9]{0,19}$/.test(value))
            return false;
        const maximum = host.has(k) ? '9007199254740991' : '18446744073709551615';
        return value.length < maximum.length || (value.length === maximum.length && value <= maximum);
    });
}

export function validRect(rect) {
    return rect && ['x', 'y', 'width', 'height'].every(k =>
        Number.isFinite(rect[k]) && Math.abs(rect[k]) <= MAX_COORDINATE) &&
        rect.width > 0 && rect.height > 0;
}

// Read-only bounded observation of two *different* Clutter stage updates on
// one stable output view containing the exact previously retained stage point.
// Stage's before/after-update pair does not prove compositor event consumption
// or bind an EIS input device. This is one prerequisite observation only.
export class StageUpdateSequence {
    constructor(request, x, y, revision, startedMs, budgetMs = 120) {
        if (!validRequest(request, x, y, revision) || !Number.isFinite(startedMs) ||
            !Number.isFinite(budgetMs) || budgetMs <= 0 || budgetMs > 120)
            throw new Error('Invalid bounded stage observation');
        this.point = {x, y};
        this.revision = revision;
        this.deadline = startedMs + budgetMs;
        this.view = null;
        this.layout = null;
        this.pending = false;
        this.count = 0;
        this.complete = false;
        this.reason = '';
    }

    deny(reason) {
        if (!this.complete && !this.reason)
            this.reason = reason;
    }

    _live(at) {
        if (this.reason || this.complete)
            return false;
        if (!Number.isFinite(at) || at >= this.deadline) {
            this.deny('stage-update-timeout');
            return false;
        }
        return true;
    }

    before(view, layout, at) {
        if (!this._live(at))
            return false;
        if (!view || !validRect(layout)) {
            this.deny('invalid-stage-view');
            return false;
        }
        const {x, y} = this.point;
        if (x < layout.x || x >= layout.x + layout.width ||
            y < layout.y || y >= layout.y + layout.height)
            return false; // An unrelated monitor's stage update is not ours.
        if (this.view && this.view !== view) {
            this.deny('stage-view-replaced');
            return false;
        }
        if (this.layout && ['x', 'y', 'width', 'height'].some(k => this.layout[k] !== layout[k])) {
            this.deny('stage-layout-changed');
            return false;
        }
        if (this.pending) {
            this.deny('stage-update-reentered');
            return false;
        }
        this.view = view;
        // GJS exposes native Mtk.Rectangle as a boxed object whose fields
        // need not be enumerable. Copy exactly four primitive coordinates,
        // never retain a borrowed compositor boxed rectangle across frames.
        this.layout = {x: layout.x, y: layout.y, width: layout.width, height: layout.height};
        this.pending = true;
        return true;
    }

    after(view, at) {
        if (!this._live(at))
            return false;
        if (view !== this.view || !this.view)
            return false; // Ignore unrelated outputs, never use them as evidence.
        if (!this.pending) {
            this.deny('unpaired-stage-update');
            return false;
        }
        this.pending = false;
        this.count++;
        if (this.count === 2)
            this.complete = true;
        return this.complete;
    }
}

// activate() may queue one coalesced compositor stack synchronization for
// the next redraw. This is an activation-completion boundary, not a new input
// drain or native event attribution. The adapter must recheck the exact owned
// window, focused surface, frozen hit and unchanged geometry at every phase.
export class PreparationActivationSequence extends StageUpdateSequence {
    constructor(request, x, y, revision, startedMs, budgetMs) {
        super(request, x, y, revision, startedMs, budgetMs);
        this.restacks = 0;
    }

    invalidated(reason, exactFocus, at) {
        if (!this._live(at))
            return false;
        // A second/later restack cannot belong to this bounded completion
        // window. The coalesced signal does not identify an individual raise.
        // All other changes, including a lost-and-returned focus, are terminal.
        if (reason !== 'restacked' || exactFocus !== true || this.count !== 0 || this.restacks !== 0) {
            this.deny('activation-state-invalidated');
            return false;
        }
        this.restacks++;
        return true;
    }

    before(view, layout, depth, at) {
        if (depth !== 1) {
            this.deny('nested-main-dispatch');
            return false;
        }
        return super.before(view, layout, at);
    }

    after(view, layout, depth, at) {
        if (depth !== 1) {
            this.deny('nested-main-dispatch');
            return false;
        }
        if (this._live(at) && view === this.view && (!validRect(layout) ||
            ['x', 'y', 'width', 'height'].some(key => layout[key] !== this.layout[key]))) {
            this.deny('stage-layout-changed');
            return false;
        }
        return super.after(view, at);
    }
}

// The compositor segment of a caller-ordered cutoff. The adapter must call
// queueEmpty from a deferred, nonrecursive main-context dispatch, then request
// a new stage update. In Mutter 50.1 the fresh frame processes the stage event
// queue before before-update. This does not verify Mutter's compiled EIS SYNC
// implementation, the caller's PONG, a Viewer forwarding fence, or Qt delivery.
export class InputDrainSequence {
    constructor(request, x, y, revision, startedMs, budgetMs) {
        if (!validRoutedPointRequest(request, x, y, revision) || !Number.isFinite(startedMs) ||
            !Number.isInteger(budgetMs) || budgetMs <= 0 || budgetMs > 120)
            throw new Error('Invalid bounded input drain');
        this.point = {x, y};
        this.deadline = startedMs + budgetMs;
        this.phase = 'queue';
        this.view = null;
        this.layout = null;
        this.complete = false;
        this.reason = '';
    }

    deny(reason) {
        if (!this.complete && !this.reason) {
            this.reason = reason;
            this.phase = 'denied';
        }
    }

    _live(depth, at) {
        if (this.complete || this.reason)
            return false;
        if (!Number.isFinite(at) || at >= this.deadline)
            this.deny('input-drain-timeout');
        else if (depth !== 1)
            this.deny('nested-main-dispatch');
        return !this.reason;
    }

    queueEmpty(pending, depth, at) {
        if (!this._live(depth, at) || this.phase !== 'queue')
            return false;
        if (typeof pending !== 'boolean') {
            this.deny('unsupported-event-queue-state');
            return false;
        }
        if (pending)
            return false;
        this.phase = 'armed';
        return true;
    }

    before(view, layout, depth, at) {
        if (!this._live(depth, at) || this.phase === 'queue')
            return false; // A frame already running before the empty-queue idle is not evidence.
        if (!view || !validRect(layout)) {
            this.deny('invalid-stage-view');
            return false;
        }
        const {x, y} = this.point;
        if (x < layout.x || x >= layout.x + layout.width ||
            y < layout.y || y >= layout.y + layout.height)
            return false;
        if (this.phase !== 'armed') {
            this.deny('stage-update-reentered');
            return false;
        }
        this.view = view;
        this.layout = {x: layout.x, y: layout.y, width: layout.width, height: layout.height};
        this.phase = 'frame';
        return true;
    }

    after(view, layout, depth, at) {
        if (!this._live(depth, at) || this.phase === 'queue' || this.phase === 'armed')
            return false;
        if (view !== this.view)
            return false;
        if (this.phase !== 'frame' || !validRect(layout) ||
            ['x', 'y', 'width', 'height'].some(k => this.layout[k] !== layout[k])) {
            this.deny('stage-layout-changed');
            return false;
        }
        this.complete = true;
        this.phase = 'complete';
        return true;
    }
}

// MetaCursorTracker returns Graphene float coordinates. Compare that exact
// representation, without a pixel-distance tolerance or ShellGlobal's integer
// truncation. This does not attribute a source device or prove EIS ordering.
export function samePointerPosition(x, y, actualX, actualY) {
    return [x, y, actualX, actualY].every(Number.isFinite) &&
        Math.fround(x) === actualX && Math.fround(y) === actualY;
}

export function unavailableReason(state, allowUnfocused = false) {
    if (!state.nativeBackend)
        return 'unsupported-backend';
    if (state.locked)
        return 'locked';
    if (state.overview)
        return 'overview';
    if (state.grabbed || state.modal)
        return 'unsupported-grab-or-modal';
    if (state.buttonsHeld)
        return 'buttons-held';
    if (state.windowCount !== 1)
        return state.windowCount === 0 ? 'viewer-not-mapped' : 'ambiguous-viewer';
    if (!state.normalWindow || state.transient)
        return 'unsupported-window';
    if (!state.visible)
        return 'viewer-not-visible';
    if (!state.focused && !allowUnfocused)
        return 'viewer-not-focused';
    if (!state.oneSurface)
        return 'unsupported-surface-tree';
    return '';
}

export class ProviderState {
    constructor() {
        this.generation = 0;
        this.current = null;
        this.closed = false;
    }

    begin(sender, id, now) {
        if (this.closed || this.current || this.generation === UINT32_MAX ||
            typeof sender !== 'string' || !sender.startsWith(':') ||
            typeof id !== 'string' || !id || !Number.isFinite(now))
            throw new Error('Unavailable or busy');
        const session = {
            sender, id, generation: ++this.generation, revision: 1,
            phase: 'creating', deadline: now + STARTUP_MS,
        };
        this.current = session;
        return session;
    }

    matches(session) {
        return !this.closed && session !== null && this.current === session;
    }

    lookup(sender, id, generation, ready = true) {
        const session = this.current;
        if (!this.matches(session) || session.sender !== sender || session.id !== id ||
            session.generation !== generation || (ready && session.phase !== 'ready'))
            throw new Error('Stale session or wrong caller');
        return session;
    }

    ready(session, now) {
        if (!this.matches(session) || session.phase !== 'creating' ||
            !Number.isFinite(now) || now >= session.deadline)
            return false;
        session.phase = 'ready';
        return true;
    }

    invalidate(session) {
        if (!this.matches(session))
            return false;
        if (session.revision === UINT32_MAX) {
            this.detach(session);
            return false;
        }
        session.revision++;
        return true;
    }

    // Detach before any external cleanup: a late callback cannot clear a new
    // session, even if the old callback carries a reused string identifier.
    detach(session) {
        if (this.current !== session)
            return false;
        this.current = null;
        session.phase = 'closed';
        return true;
    }

    shutdown() {
        const previous = this.current;
        this.closed = true;
        if (previous)
            this.detach(previous);
        return previous;
    }
}
