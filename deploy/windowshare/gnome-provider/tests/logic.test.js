// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import test from 'node:test';
import {
    ProviderState, STARTUP_MS, UINT32_MAX, samePointerPosition, supportedVersion,
    unavailableReason, validLaunch, validRect, validRequest, validViewportHandle,
} from '../logic.js';

test('only stable Shell 50 is supported', () => {
    for (const version of ['50', '50.0', '50.2'])
        assert.equal(supportedVersion(version), true);
    for (const version of ['', '49.9', '51', '50.rc', '50.0\n', null])
        assert.equal(supportedVersion(version), false);
});

test('launch accepts bounded argv, never a platform override', () => {
    assert.equal(validLaunch('/opt/deskflow-windowshare', ['--headless', '--connect', 'host:42']), true);
    for (const arg of ['-platform', '--platform=xcb', '-platformpluginpath=/tmp',
        '--plugin=anything', 'QT_QPA_PLATFORM=offscreen', 'nul\0byte',
        '--gnome-viewport-fd', '--gnome-viewport-fd=4', '-gnome-viewport-fd=4',
        '--GNOME-VIEWPORT-FD=5'])
        assert.equal(validLaunch('/opt/viewer', [arg]), false, arg);
    assert.equal(validLaunch('viewer', []), false);
    assert.equal(validLaunch('/opt/viewer', Array(65).fill('x')), false);
    assert.equal(validLaunch('/opt/viewer', Array(5).fill('x'.repeat(4096))), false);
});

test('viewport handles name exactly one attached descriptor, never a process FD', () => {
    assert.equal(validViewportHandle(0, 1, 1), true);
    for (const handle of [-1, 1, 4, 0.5, '0', null, undefined, NaN])
        assert.equal(validViewportHandle(handle, 1, 1), false);
    for (const count of [0, 2, -1, 1.5, '1', null, undefined]) {
        assert.equal(validViewportHandle(0, count, 1), false);
        assert.equal(validViewportHandle(0, 1, count), false);
    }
});

test('requests reject NaN, Infinity, absent revisions and oversized tokens', () => {
    assert.equal(validRequest('epoch_1-seq_23', -23.5, 12.25, 1), true);
    for (const value of [NaN, Infinity, -Infinity, 0x1000001])
        assert.equal(validRequest('request', value, 0, 1), false);
    for (const revision of [0, -1, 1.5, UINT32_MAX + 1])
        assert.equal(validRequest('request', 0, 0, revision), false);
    assert.equal(validRequest('x'.repeat(65), 0, 0, 1), false);
    assert.equal(validRequest('request\n', 0, 0, 1), false);
});

test('fractional native readback compares exact Graphene float coordinates', () => {
    assert.equal(samePointerPosition(10.1, -20.2, Math.fround(10.1), Math.fround(-20.2)), true);
    assert.equal(samePointerPosition(10.1, -20.2, 10, -20), false);
    assert.equal(samePointerPosition(0.25, 0, -0.25, 0), false);
    assert.equal(samePointerPosition(0, 0, NaN, 0), false);
});

test('rectangles preserve nonzero origins and reject empty/invalid geometry', () => {
    assert.equal(validRect({x: -1920, y: 40, width: 1024.5, height: 720}), true);
    assert.equal(Boolean(validRect(null)), false);
    assert.equal(validRect({x: 0, y: 0, width: 0, height: 1}), false);
    assert.equal(validRect({x: Infinity, y: 0, width: 1, height: 1}), false);
});

test('native hit prerequisites deny each unsupported state explicitly', () => {
    const good = {nativeBackend: true, locked: false, overview: false, grabbed: false,
        modal: false, buttonsHeld: false, windowCount: 1, normalWindow: true,
        transient: false, visible: true, focused: true, oneSurface: true};
    assert.equal(unavailableReason(good), '');
    for (const [key, value, expected] of [
        ['nativeBackend', false, 'unsupported-backend'], ['locked', true, 'locked'],
        ['overview', true, 'overview'], ['grabbed', true, 'unsupported-grab-or-modal'],
        ['modal', true, 'unsupported-grab-or-modal'], ['buttonsHeld', true, 'buttons-held'],
        ['windowCount', 0, 'viewer-not-mapped'], ['windowCount', 2, 'ambiguous-viewer'],
        ['normalWindow', false, 'unsupported-window'], ['transient', true, 'unsupported-window'],
        ['visible', false, 'viewer-not-visible'], ['focused', false, 'viewer-not-focused'],
        ['oneSurface', false, 'unsupported-surface-tree'],
    ])
        assert.equal(unavailableReason({...good, [key]: value}), expected);
});

test('sessions require exact caller and generation; readiness cannot be claimed early', () => {
    const model = new ProviderState();
    assert.throws(() => model.lookup(':1.2', 'session', 1));
    const session = model.begin(':1.2', 'session', 100);
    assert.throws(() => model.begin(':1.3', 'other', 101));
    assert.throws(() => model.lookup(':1.2', 'session', 1));
    assert.equal(model.ready(session, 101), true);
    assert.equal(model.lookup(':1.2', 'session', 1), session);
    assert.throws(() => model.lookup(':1.3', 'session', 1));
    assert.throws(() => model.lookup(':1.2', 'session', 2));
    assert.equal(model.ready(session, 102), false);
});

test('startup has an absolute deadline and late completion cannot revive a session', () => {
    const model = new ProviderState();
    const session = model.begin(':1.2', 'session', 20);
    assert.equal(model.ready(session, 20 + STARTUP_MS), false);
    assert.equal(model.detach(session), true);
    assert.equal(model.ready(session, 21), false);
    assert.equal(model.invalidate(session), false);
});

test('retired callbacks cannot detach replacement even with the same string identity', () => {
    const model = new ProviderState();
    const old = model.begin(':1.2', 'session', 0);
    model.ready(old, 1);
    model.detach(old);
    const fresh = model.begin(':1.2', 'session', 2);
    assert.equal(fresh.generation, 2);
    assert.equal(model.detach(old), false);
    assert.equal(model.invalidate(old), false);
    assert.equal(model.ready(old, 3), false);
    assert.equal(model.current, fresh);
});

test('disable is terminal and invalidates pending as well as ready sessions', () => {
    for (const ready of [false, true]) {
        const model = new ProviderState();
        const session = model.begin(':1.2', 'session', 0);
        if (ready)
            model.ready(session, 1);
        assert.equal(model.shutdown(), session);
        assert.equal(model.current, null);
        assert.equal(model.ready(session, 1), false);
        assert.equal(model.invalidate(session), false);
        assert.throws(() => model.begin(':1.2', 'new', 2));
    }
});

test('generation and revision cannot wrap into retained observations', () => {
    const model = new ProviderState();
    model.generation = UINT32_MAX - 1;
    const session = model.begin(':1.2', 'last', 0);
    assert.equal(session.generation, UINT32_MAX);
    session.revision = UINT32_MAX - 1;
    assert.equal(model.invalidate(session), true);
    assert.equal(session.revision, UINT32_MAX);
    assert.equal(model.invalidate(session), false);
    assert.equal(model.current, null);
    assert.throws(() => model.begin(':1.2', 'wrapped', 2));
});
