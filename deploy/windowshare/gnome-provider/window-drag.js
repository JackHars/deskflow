// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
import {validRect, validWindowAssociation} from './logic.js';

export const DRAG_BUDGET_MS = 30000;
export const DRAG_SILENCE_MS = 350;
const integer = value => Number.isSafeInteger(value) && value > 0;
const coordinate = value => Number.isInteger(value) && value >= -2147483648 && value <= 2147483647;
const sameSize = (a, b) => a.width === b.width && a.height === b.height;
const sameRect = (a, b) => sameSize(a, b) && a.x === b.x && a.y === b.y;
// Meta.Rectangle is a GI boxed object: its fields need not be enumerable.
const copyRect = frame => ({x: frame.x, y: frame.y, width: frame.width, height: frame.height});

// Owns one provisional placement on one retained native window. The adapter
// supplies only a MetaWindow independently resolved through owns_window and
// the exact per-window association. It never accepts a title/PID-only lookup.
export class OwnedDragPlacement {
    constructor({association, drag, window, frame, now, layout}) {
        if (!validWindowAssociation(association) || !integer(drag) || !window || !validRect(frame) ||
            !Number.isFinite(now) || !layout)
            throw new Error('Invalid native drag placement binding');
        Object.assign(this, {association, drag, window, layout, sequence: 0, revision: 0,
            initial: copyRect(frame), actual: copyRect(frame), deadline: now + DRAG_BUDGET_MS,
            lastSeen: now, closed: false});
    }

    live(window, layout, now) {
        return !this.closed && this.window === window && this.layout === layout &&
            Number.isFinite(now) && now >= this.lastSeen && now < this.deadline &&
            now - this.lastSeen < DRAG_SILENCE_MS;
    }

    place(sequence, x, y, now, {current, frame, move}) {
        if (!integer(sequence) || sequence <= this.sequence || !coordinate(x) || !coordinate(y))
            throw new Error('Stale or invalid drag placement');
        if (!this.live(current.window, current.layout, now)) {
            this.close();
            throw new Error('Native drag placement lifetime ended');
        }
        const before = frame(this.window);
        if (!validRect(before) || !sameSize(before, this.initial)) {
            this.close();
            throw new Error('Native preview resized or disappeared');
        }
        this.sequence = sequence; // Reserve before a native/reentrant call.
        this.lastSeen = now;
        let after;
        try {
            move(this.window, x, y);
            after = frame(this.window);
        } catch (error) {
            this.close();
            throw error;
        }
        if (this.closed || !validRect(after) || !sameSize(after, this.initial) || after.x !== x || after.y !== y) {
            this.close();
            throw new Error('Compositor did not place the exact native preview');
        }
        this.actual = copyRect(after);
        ++this.revision;
        return {drag: this.drag, sequence, revision: this.revision, ...this.actual};
    }

    verify(sequence, now, window, layout, frame) {
        if (!this.live(window, layout, now) || sequence !== this.sequence || !validRect(frame) ||
            !sameRect(frame, this.actual)) {
            this.close();
            return false;
        }
        return true;
    }

    hold(now, window, layout, frame) {
        if (!this.verify(this.sequence, now, window, layout, frame))
            return false;
        this.lastSeen = now;
        return true;
    }

    close() {
        this.closed = true;
    }
}

// A native compositor grab observation, not an input lease. Only the exact
// retained Viewer and ordinary move operation may begin one. The adapter must
// independently establish paired Core press ownership before using it for a
// cross-computer handoff. No synthetic button or native grab is created here.
export class OwnedMoveGrab {
    constructor(window, serial, frame, point, now) {
        if (!window || !integer(serial) || !validRect(frame) || !coordinate(point.x) || !coordinate(point.y) ||
            !Number.isFinite(now))
            throw new Error('Invalid owned move grab');
        Object.assign(this, {window, serial, initial: copyRect(frame), point: {x: point.x, y: point.y},
            sequence: 0, deadline: now + DRAG_BUDGET_MS, closed: false});
    }

    sample(window, frame, point, now) {
        if (this.closed || window !== this.window || !validRect(frame) || !sameSize(this.initial, frame) ||
            !coordinate(point.x) || !coordinate(point.y) || !Number.isFinite(now) || now >= this.deadline) {
            this.closed = true;
            return null;
        }
        this.point = {x: point.x, y: point.y};
        return {serial: this.serial, sequence: ++this.sequence, x: point.x, y: point.y, frame: copyRect(frame)};
    }

    end(window, frame, point, now, released) {
        const sample = this.sample(window, frame, point, now);
        this.closed = true;
        return sample && released ? sample : null;
    }
}
