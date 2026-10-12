// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
// Run only through launcher-native.py, which supplies disposable inherited FDs.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {closeDescriptorList, withViewerLauncher, VIEWPORT_ARGUMENT} from '../launcher.js';

function assert(value, message) {
    if (!value)
        throw new Error(message);
}

function fdCount() {
    const directory = Gio.File.new_for_path('/proc/self/fd').enumerate_children(
        'standard::name', Gio.FileQueryInfoFlags.NONE, null);
    let count = 0;
    try {
        while (directory.next_file(null))
            count++;
    } finally {
        directory.close(null);
    }
    return count;
}

const [mode, python, fixture, ...rawDescriptors] = ARGV;
const descriptors = rawDescriptors.map(Number);
const list = new Gio.UnixFDList();
for (const descriptor of descriptors) {
    list.append(descriptor);
    GLib.close(descriptor);
}
const viewport = {fdList: list, handle: 0, declaredCount: list.get_length()};
if (mode === 'wrong-handle')
    viewport.handle = 1;
if (mode === 'process-number')
    viewport.handle = 4;
if (mode === 'missing')
    viewport.fdList = null;
if (mode === 'header-count')
    viewport.declaredCount = 2;
const args = [fixture, '--child'];
const childRejects = ['non-socket', 'datagram', 'unconnected', 'inet'].includes(mode);
if (childRejects)
    args[1] = '--child-reject';
if (mode === 'reserved')
    args.push('--gnome-viewport-fd=7');
if (mode === 'reserved-separate')
    args.push('--gnome-viewport-fd', '4');
if (mode === 'qt-override')
    args.push('-platform', 'xcb');
if (mode === 'end-options')
    args.push('--');

let invoked = false;
let process = null;
let thrown = false;
const success = mode === 'forward' || mode === 'legacy' || childRejects;
try {
    process = withViewerLauncher(python, args, mode === 'legacy' ? null : viewport, (launcher, argv) => {
        invoked = true;
        assert((mode === 'legacy') !== argv.includes(VIEWPORT_ARGUMENT), 'Unexpected child viewport argument');
        if (mode === 'callback-failure')
            throw new Error('Fixture failure after take_fd');
        if (mode === 'spawn-failure')
            return launcher.spawnv(['/deskflow-fixture-does-not-exist']);
        return launcher.spawnv(argv);
    });
} catch (error) {
    thrown = true;
    if (success)
        throw error;
}
assert(success !== thrown, `Unexpected ${mode} launch result`);
assert(list.get_length() === 0, 'Invocation FD list retained descriptors');
if (!success)
    assert(invoked === ['callback-failure', 'spawn-failure'].includes(mode), 'Invalid input reached spawn');
if (process) {
    process.wait_check(null);
    assert(process.get_successful(), 'Child exchange failed');
    // The outer fixture must observe EOF while this process remains alive;
    // eventual process exit/garbage collection must not hide a retained FD.
    if (mode === 'forward')
        GLib.usleep(2000000);
}

if (mode === 'leak') {
    // Each iteration transfers a genuine CLOEXEC duplicate of stdin to the
    // launcher, then fails after take_fd. No GC runs between attempts.
    const before = fdCount();
    for (let attempt = 0; attempt < 128; attempt++) {
        const rejected = new Gio.UnixFDList();
        rejected.append(0);
        let failed = false;
        try {
            withViewerLauncher(python, args, {fdList: rejected, handle: 0, declaredCount: 1},
                () => { throw new Error('Fixture failure after take_fd'); });
        } catch (_) {
            failed = true;
        }
        assert(failed, 'Expected callback failure');
        assert(rejected.get_length() === 0, 'Rejected descriptor was retained');
    }
    assert(fdCount() === before, 'Failed launch leaked native descriptors');
}
closeDescriptorList(list);
print(`PASS ${mode}`);
