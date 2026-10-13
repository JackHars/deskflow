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
