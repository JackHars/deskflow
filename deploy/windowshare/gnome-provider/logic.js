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
        this.layout = {...layout};
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

// MetaCursorTracker returns Graphene float coordinates. Compare that exact
// representation, without a pixel-distance tolerance or ShellGlobal's integer
// truncation. This does not attribute a source device or prove EIS ordering.
export function samePointerPosition(x, y, actualX, actualY) {
    return [x, y, actualX, actualY].every(Number.isFinite) &&
        Math.fround(x) === actualX && Math.fround(y) === actualY;
}

export function unavailableReason(state) {
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
    if (!state.focused)
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
