// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import test from 'node:test';
import {
    ProviderState, STARTUP_MS, UINT32_MAX, samePointerPosition, supportedVersion,
    unavailableReason, validLaunch, validRect, validRequest, validRoutedPointRequest, validViewportHandle,
    StageUpdateSequence, InputDrainSequence, validInputDrainBinding,
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

test('exact routed point refuses float aliases, oversized bounds and invalid revisions', () => {
    assert.equal(validRoutedPointRequest('p_4', 425.25, -325.5, 1), true);
    for (const x of [0.1, 10000001, Infinity, NaN])
        assert.equal(validRoutedPointRequest('p_4', x, 300, 1), false);
    assert.equal(validRoutedPointRequest('p_4', 425, 0.1, 1), false);
    assert.equal(validRoutedPointRequest('p_4', 425, 325, 0), false);
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

test('stage observation requires two paired later updates on one exact physical view', () => {
    const view = {}, unrelated = {};
    const selected = {x: -1920, y: 0, width: 1920, height: 1080};
    const elsewhere = {x: 0, y: 0, width: 1920, height: 1080};
    const seq = new StageUpdateSequence('p_12', -100, 250, 8, 100);
    assert.equal(seq.before(unrelated, elsewhere, 101), false);
    assert.equal(seq.after(unrelated, 102), false);
    assert.equal(seq.count, 0);
    assert.equal(seq.before(view, selected, 103), true);
    assert.equal(seq.after(view, 104), false);
    assert.equal(seq.count, 1);
    assert.equal(seq.before(view, {...selected}, 118), true);
    assert.equal(seq.after(view, 119), true);
    assert.equal(seq.complete, true);
    assert.equal(seq.count, 2);
    assert.equal(seq.before(view, selected, 120), false);
});

test('boxed non-enumerable native layouts retain exact stable stage coordinates', () => {
    const view = {}, rect = {};
    for (const [key, value] of Object.entries({x: -1920, y: 0, width: 1920, height: 1080}))
        Object.defineProperty(rect, key, {get: () => value, enumerable: false});
    assert.deepEqual({...rect}, {}, 'The fixture must model a non-enumerable native boxed layout');
    const witness = new StageUpdateSequence('box', -500, 300, 1, 100);
    assert.equal(witness.before(view, rect, 101), true);
    assert.equal(witness.after(view, 102), false);
    assert.equal(witness.before(view, rect, 103), true);
    assert.equal(witness.after(view, 104), true);
    assert.equal(witness.complete, true);
});

test('stage updates refuse deadline, reentry, view changes, layout drift and unpaired callbacks', () => {
    const rect = {x: 0, y: -1080, width: 2560, height: 1440};
    const view = {}, replaced = {};
    const fresh = () => new StageUpdateSequence('p', 125, -200, 4, 100);
    const timeout = fresh();
    assert.equal(timeout.before(view, rect, 220), false);
    assert.equal(timeout.reason, 'stage-update-timeout');
    const unpaired = fresh();
    unpaired.before(view, rect, 101);
    unpaired.after(view, 102);
    unpaired.after(view, 103);
    assert.equal(unpaired.reason, 'unpaired-stage-update');
    const reentrant = fresh();
    reentrant.before(view, rect, 101);
    reentrant.before(view, rect, 102);
    assert.equal(reentrant.reason, 'stage-update-reentered');
    const switched = fresh();
    switched.before(view, rect, 101);
    switched.after(view, 102);
    switched.before(replaced, rect, 103);
    assert.equal(switched.reason, 'stage-view-replaced');
    const shifted = fresh();
    shifted.before(view, rect, 101);
    shifted.after(view, 102);
    shifted.before(view, {...rect, x: 1}, 103);
    assert.equal(shifted.reason, 'stage-layout-changed');
    for (const bad of [null, {x: 0, y: 0, width: 0, height: 100}, {x: Infinity, y: 0, width: 2, height: 3}]) {
        const denied = fresh();
        denied.before(view, bad, 101);
        assert.equal(denied.reason, 'invalid-stage-view');
    }
    assert.throws(() => new StageUpdateSequence('p', 1, 2, 1, 0, 500));
    assert.throws(() => new StageUpdateSequence('p', 1, 2, 0, 0));
});

test('input drain waits for a nonrecursive empty-queue idle and a genuinely later same-view frame', () => {
    const view = {}, other = {};
    const rect = {x: -1920, y: 0, width: 1920, height: 1080};
    const elsewhere = {x: 0, y: 0, width: 1920, height: 1080};
    const seq = new InputDrainSequence('cutoff_1', -100, 200, 8, 100, 80);
    assert.equal(seq.before(view, rect, 1, 101), false);
    assert.equal(seq.after(view, rect, 1, 102), false);
    assert.equal(seq.queueEmpty(true, 1, 103), false);
    assert.equal(seq.before(view, rect, 1, 104), false);
    assert.equal(seq.queueEmpty(false, 1, 105), true);
    assert.equal(seq.after(view, rect, 1, 106), false);
    assert.equal(seq.before(other, elsewhere, 1, 107), false);
    assert.equal(seq.before(view, rect, 1, 108), true);
    assert.equal(seq.after(other, elsewhere, 1, 109), false);
    assert.equal(seq.after(view, {...rect}, 1, 110), true);
    assert.equal(seq.complete, true);
    assert.equal(seq.after(view, rect, 1, 111), false);
});

test('input drain correlation is closed, exact All, and retains full canonical cutoff integers', () => {
    const binding = {cutoffRequest: '18446744073709551615', coreConnectionGeneration: UINT32_MAX,
        captureEpoch: '18446744073709551615', captureSequence: '18446744073709551615', capturePart: 16,
        viewerConnection: '18446744073709551615', ownerSession: '1', ownerEpoch: '2',
        forwardingGeneration: '3', focusRequest: '4', focusRevision: '5', geometry: '9007199254740991', scope: 'all'};
    assert.equal(validInputDrainBinding(binding), true);
    for (const [key, value] of [['cutoffRequest', '18446744073709551616'], ['captureEpoch', '01'],
        ['captureSequence', '0'], ['ownerSession', 1], ['geometry', '9007199254740992'],
        ['scope', 'keyboard'], ['capturePart', 17], ['coreConnectionGeneration', 0]])
        assert.equal(validInputDrainBinding({...binding, [key]: value}), false, key);
    assert.equal(validInputDrainBinding({...binding, unexpected: '1'}), false);
    const missing = {...binding};
    delete missing.viewerConnection;
    assert.equal(validInputDrainBinding(missing), false);
});

test('input drain refuses nested dispatch even when a popped or stolen queue appears empty', () => {
    const rect = {x: 0, y: 0, width: 1000, height: 700}, view = {};
    for (const callback of ['idle', 'before', 'after']) {
        const seq = new InputDrainSequence('cutoff_2', 100, 200, 8, 100, 80);
        if (callback === 'idle')
            seq.queueEmpty(false, 2, 101);
        else {
            seq.queueEmpty(false, 1, 101);
            if (callback === 'before')
                seq.before(view, rect, 2, 102);
            else {
                seq.before(view, rect, 1, 102);
                seq.after(view, rect, 2, 103);
            }
        }
        assert.equal(seq.complete, false);
        assert.equal(seq.reason, 'nested-main-dispatch');
        assert.equal(seq.queueEmpty(false, 1, 104), false);
    }
});

test('input drain keeps its inherited budget and cannot revive after invalidation, layout drift or late frames', () => {
    const rect = {x: 0, y: 0, width: 1000, height: 700}, view = {};
    const fresh = () => new InputDrainSequence('cutoff_3', 100, 200, 8, 100, 20);
    const lateIdle = fresh();
    lateIdle.queueEmpty(false, 1, 120);
    assert.equal(lateIdle.reason, 'input-drain-timeout');
    const lateFrame = fresh();
    lateFrame.queueEmpty(false, 1, 101);
    lateFrame.before(view, rect, 1, 102);
    assert.equal(lateFrame.after(view, rect, 1, 120), false);
    assert.equal(lateFrame.reason, 'input-drain-timeout');
    const invalid = fresh();
    invalid.queueEmpty(false, 1, 101);
    invalid.before(view, rect, 1, 102);
    invalid.deny('native-state-invalidated');
    assert.equal(invalid.after(view, rect, 1, 103), false);
    assert.equal(invalid.complete, false);
    const moved = fresh();
    moved.queueEmpty(false, 1, 101);
    moved.before(view, rect, 1, 102);
    assert.equal(moved.after(view, {...rect, width: 900}, 1, 103), false);
    assert.equal(moved.reason, 'stage-layout-changed');
    const repeated = fresh();
    repeated.queueEmpty(false, 1, 101);
    repeated.before(view, rect, 1, 102);
    repeated.before(view, rect, 1, 103);
    assert.equal(repeated.reason, 'stage-update-reentered');
    for (const budget of [0, -1, 121, 1.5, Infinity])
        assert.throws(() => new InputDrainSequence('c', 100, 200, 8, 100, budget));
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
