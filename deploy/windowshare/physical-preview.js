// SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
// SPDX-License-Identifier: MIT
// Installed only by physical-preview.py in its disposable GNOME profile.
// A view-only observer: no event injection, focus changes or provider calls.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

export default class PhysicalPreview extends Extension {
    enable() {
        const path = GLib.getenv('DESKFLOW_PRIVATE_PREVIEW');
        if (!path?.startsWith('/tmp/df-physical-') || !path.endsWith('/preview/frame'))
            throw new Error('Preview requires the disposable physical fixture');
        this._file = Gio.File.new_for_path(path);
        this._active = true;
        this._busy = false;
        this._sequence = 0;
        this._deadline = GLib.get_monotonic_time() + 270_000_000;
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 200, () => {
            if (!this._active || GLib.get_monotonic_time() >= this._deadline) {
                this._timer = 0;
                this._active = false;
                return GLib.SOURCE_REMOVE;
            }
            this._capture();
            return GLib.SOURCE_CONTINUE;
        });
    }

    async _capture() {
        if (this._busy)
            return;
        this._busy = true;
        const stream = Gio.MemoryOutputStream.new_resizable();
        try {
            const screenshot = new Shell.Screenshot();
            await new Promise((resolve, reject) => {
                screenshot.screenshot(true, stream, (source, result) => {
                    try {
                        source.screenshot_finish(result);
                        resolve();
                    } catch (error) {
                        reject(error);
                    }
                });
            });
            stream.close(null);
            const png = stream.steal_as_bytes().get_data();
            if (!this._active)
                return;
            if (png.length < 33 || png.length > 4 * 1024 * 1024 || this._sequence >= 1350)
                throw new Error('Preview frame exceeded its fixed bound');
            const frame = new Uint8Array(8 + png.length);
            frame.set([68, 70, 80, 49]); // DFP1
            new DataView(frame.buffer).setUint32(4, ++this._sequence);
            frame.set(png, 8);
            this._file.replace_contents(frame, null, false, Gio.FileCreateFlags.PRIVATE, null);
        } catch (error) {
            this._active = false; // A stale image is not a live desktop.
            console.error(`Deskflow physical preview stopped: ${error.message}`);
        } finally {
            if (!stream.is_closed())
                stream.close(null);
            this._busy = false;
        }
    }

    disable() {
        this._active = false;
        if (this._timer)
            GLib.source_remove(this._timer);
        this._timer = 0;
    }
}
