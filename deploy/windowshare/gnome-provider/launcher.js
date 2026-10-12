// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {validLaunch, validViewportHandle} from './logic.js';

export const VIEWPORT_FD = 4;
export const VIEWPORT_ARGUMENT = `--gnome-viewport-fd=${VIEWPORT_FD}`;

// The invocation's FD list is consumed only by its own method. Drain it on
// success and failure so a borrowed GJS wrapper cannot delay peer EOF until GC.
export function closeDescriptorList(fdList) {
    if (!fdList)
        return;
    for (const fd of fdList.steal_fds())
        GLib.close(fd);
}

function duplicateViewportDescriptor({fdList, handle, declaredCount}) {
    if (!fdList || !validViewportHandle(handle, fdList.get_length(), declaredCount))
        throw new Error('Viewport channel requires exactly one descriptor at handle zero');
    // Do not inspect it with GSocket.new_from_fd(): on the supported runtime
    // its failed constructor closes the descriptor despite the documented
    // caller-owns-on-failure contract. A retry/close by number can race reuse.
    // Core creates the socketpair; child validates AF_UNIX, STREAM, connected,
    // and FD 4 before adopting it. Shell advertises socket validation as false.
    return fdList.get(handle);
}

// The synchronous spawn callback is either Meta.WaylandClient.new_subprocess
// in production or a real GSubprocessLauncher spawn in headless FD tests.
// This helper never reads/writes the viewport socket or grants delivery.
export function withViewerLauncher(executable, args, viewport, spawn) {
    let launcher = null;
    let descriptor = -1;
    try {
        if (!validLaunch(executable, args) || (viewport && args.includes('--')))
            throw new Error('Use an absolute executable and bounded argv without Qt or reserved overrides');
        const file = Gio.File.new_for_path(executable);
        if (file.query_file_type(Gio.FileQueryInfoFlags.NONE, null) !== Gio.FileType.REGULAR ||
            !GLib.file_test(executable, GLib.FileTest.IS_EXECUTABLE))
            throw new Error('Viewer executable is not a regular executable file');
        if (viewport)
            descriptor = duplicateViewportDescriptor(viewport);
        launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.NONE});
        launcher.setenv('QT_QPA_PLATFORM', 'wayland', true);
        launcher.setenv('GDK_BACKEND', 'wayland', true);
        for (const variable of ['DISPLAY', 'WAYLAND_DISPLAY', 'QT_PLUGIN_PATH',
            'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_WAYLAND_SHELL_INTEGRATION',
            'QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT', 'QT_WAYLAND_RECONNECT'])
            launcher.unsetenv(variable);
        if (viewport) {
            // Mutter reserves descriptor 3 for its own WAYLAND_SOCKET.
            launcher.take_fd(descriptor, VIEWPORT_FD);
            descriptor = -1;
        }
        return spawn(launcher, [executable, ...args, ...(viewport ? [VIEWPORT_ARGUMENT] : [])]);
    } finally {
        if (descriptor >= 0)
            GLib.close(descriptor);
        // take_fd owns the duplicate. close() deterministically closes every
        // parent-side mapping even if spawn throws; it never closes child FDs.
        launcher?.close();
        closeDescriptorList(viewport?.fdList);
    }
}
