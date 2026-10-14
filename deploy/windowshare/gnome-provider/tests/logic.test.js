// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import test from 'node:test';
import {
    ProviderState, STARTUP_MS, UINT32_MAX, samePointerPosition, supportedVersion,
    unavailableReason, validLaunch, validRect, validRequest, validRoutedPointRequest, validRoutedPoints, validViewportHandle,
    validWindowAssociation, associatedWindowIndices, duplicateWindowAssociationNotification,
    StageUpdateSequence, InputDrainSequence, PreparationActivationSequence, validInputDrainBinding,
    validInputPreparationBinding, sameInputPreparationBinding,
    ORDERING_BUILDS, ORDERING_MAIN, ORDERING_CLUTTER, ORDERING_FILE_MAX, ORDERING_MAPS_MAX,
    mappedOrderingModules, matchesMappedOrderingFile, orderingContractFor,
} from '../logic.js';

test('pre-focus binding contains no invented Viewer or All lease and never aliases a capture', () => {
    const binding = {preparationRequest: '91', coreConnectionGeneration: 3,
        captureEpoch: '71', captureSequence: '83', capturePart: 2};
    assert.equal(validInputPreparationBinding(binding), true);
    assert.equal(validInputDrainBinding(binding), false);
    assert.equal(sameInputPreparationBinding(binding, {...binding}), true);
    for (const field of Object.keys(binding)) {
        const changed = {...binding};
        changed[field] = typeof changed[field] === 'number' ? changed[field] + 1 : '92';
        assert.equal(sameInputPreparationBinding(binding, changed), false);
        const missing = {...binding}; delete missing[field];
        assert.equal(validInputPreparationBinding(missing), false);
    }
    for (const invalid of [{...binding, viewerConnection: '1'}, {...binding, scope: 'all'},
        {...binding, preparationRequest: '091'}, {...binding, capturePart: 17},
        {...binding, captureEpoch: '18446744073709551616'}])
        assert.equal(validInputPreparationBinding(invalid), false);
});

test('unfocused preparation changes only the focus prerequisite, never modal/grab/hit eligibility', () => {
    const state = {nativeBackend: true, locked: false, overview: false, grabbed: false,
        modal: false, buttonsHeld: false, windowCount: 1, normalWindow: true,
        transient: false, visible: true, focused: false, oneSurface: true};
    assert.equal(unavailableReason(state), 'viewer-not-focused');
    assert.equal(unavailableReason(state, true), '');
    for (const changed of [{locked: true}, {overview: true}, {grabbed: true}, {modal: true},
        {buttonsHeld: true}, {windowCount: 2}, {normalWindow: false}, {transient: true},
        {visible: false}, {oneSurface: false}])
        assert.notEqual(unavailableReason({...state, ...changed}, true), '');
});

test('per-window tokens select only exact unique owned-client candidates', () => {
    const token = `org.deskflow.window.${'a'.repeat(64)}`;
    const other = `org.deskflow.window.${'b'.repeat(64)}`;
    assert.equal(validWindowAssociation(token), true);
    for (const invalid of [null, '', token + '\n', 'org.deskflow.window.a', token.toUpperCase()])
        assert.equal(validWindowAssociation(invalid), false);
    const windows = [{wmClass: other, instance: other}, {wmClass: token, instance: token},
        {wmClass: token, instance: other}];
    assert.deepEqual(associatedWindowIndices(windows, token), [1]);
    assert.deepEqual(associatedWindowIndices([...windows, windows[1]], token), [1, 3]);
    assert.deepEqual(associatedWindowIndices(windows, `${token}0`), []);
    assert.deepEqual(associatedWindowIndices(Array(257).fill(windows[1]), token), []);
});

test('same-value native association notification preserves only the exact live owned window incarnation', () => {
    const association = `org.deskflow.window.${'a'.repeat(64)}`;
    const binding = {session: {}, viewer: {}, window: {}, windowToken: 'owned-window-1', association};
    const previous = {wmClass: association, instance: association};
    const current = {...binding, ...previous, live: true, viewerExited: false, owned: true, wayland: true};
    const retained = {revision: 25, revoked: false};
    const notify = value => {
        if (!duplicateWindowAssociationNotification(binding, previous, value)) {
            ++retained.revision;
            retained.revoked = true;
        }
    };
    notify(current);
    assert.deepEqual(retained, {revision: 25, revoked: false});
    for (const change of [{session: {}}, {viewer: {}}, {window: {}}, {windowToken: 'owned-window-2'},
        {association: `org.deskflow.window.${'b'.repeat(64)}`}, {live: false}, {viewerExited: true},
        {owned: false}, {wayland: false}])
        assert.equal(duplicateWindowAssociationNotification(binding, previous, {...current, ...change}), false,
            JSON.stringify(change));
    notify({...current, window: {}});
    notify(current);
    assert.deepEqual(retained, {revision: 26, revoked: true}, 'An unchanged notification cannot revive a revoked seal');
});

test('native association no-op policy compares complete canonical class and instance without truncation', () => {
    const association = `org.deskflow.window.${'a'.repeat(64)}`;
    const other = `org.deskflow.window.${'b'.repeat(64)}`;
    const binding = {session: {}, viewer: {}, window: {}, windowToken: 'owned-window-1', association};
    const previous = {wmClass: association, instance: association};
    const current = {...binding, ...previous, live: true, viewerExited: false, owned: true, wayland: true};
    for (const value of [null, undefined, '', association + '\n', association + 'x'.repeat(256), other]) {
        assert.equal(duplicateWindowAssociationNotification(binding, previous, {...current, wmClass: value}), false);
        assert.equal(duplicateWindowAssociationNotification(binding, previous, {...current, instance: value}), false);
        assert.equal(duplicateWindowAssociationNotification(binding, {...previous, wmClass: value}, current), false);
        assert.equal(duplicateWindowAssociationNotification(binding, {...previous, instance: value}, current), false);
    }
    const changed = {...previous, wmClass: other};
    assert.equal(duplicateWindowAssociationNotification(binding, previous, {...current, ...changed}), false);
    assert.equal(duplicateWindowAssociationNotification(binding, changed, current), false,
        'Restoring a changed pair remains an invalidation');
    for (const broken of [{session: null}, {viewer: null}, {window: null}, {windowToken: ''}, {association: other},
        {association: association + '\n'}])
        assert.equal(duplicateWindowAssociationNotification({...binding, ...broken}, previous, current), false);
});

test('native point batches are bounded, exact, ordered captures in one epoch', () => {
    const points = [['12', '31', 1, 400, 300], ['13', '31', 2, 401, 300], ['14', '32', 1, 402, 301]];
    assert.equal(validRoutedPoints('8', '7', points, 2), true);
    for (const change of [[], Array(17).fill(points[0]), [points[1], points[0]], [points[0], points[0]],
        [['012', '31', 1, 400, 300]], [['12', '31', 0, 400, 300]], [['12', '31', 17, 400, 300]],
        [['12', '31', 1, 400.1, 300]], [['12', '31', 1, Infinity, 300]],
        [['18446744073709551616', '31', 1, 400, 300]], [['12', '31', 1, 400, 300, 0]]])
        assert.equal(validRoutedPoints('8', '7', change, 2), false, JSON.stringify(change));
    for (const epoch of ['', '07', '0', 7, '18446744073709551616'])
        assert.equal(validRoutedPoints('8', epoch, points, 2), false);
    assert.equal(validRoutedPoints('8', '7', points, 0), false);
});

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

test('activation completion includes one early deferred restack and two completed exact-view frames', () => {
    const view = {}, other = {};
    const rect = {x: 0, y: 0, width: 1000, height: 700};
    const elsewhere = {x: 1000, y: 0, width: 1000, height: 700};
    for (const restackDuringFrame of [false, true]) {
        const sequence = new PreparationActivationSequence('activate_1', 100, 200, 8, 100, 80);
        assert.equal(sequence.before(other, elsewhere, 1, 101), false);
        assert.equal(sequence.after(other, elsewhere, 1, 102), false);
        if (restackDuringFrame)
            assert.equal(sequence.before(view, rect, 1, 103), true);
        assert.equal(sequence.invalidated('restacked', true, 104), true);
        if (!restackDuringFrame)
            assert.equal(sequence.before(view, rect, 1, 105), true);
        assert.equal(sequence.after(view, rect, 1, 106), false);
        assert.equal(sequence.count, 1);
        assert.equal(sequence.complete, false);
        assert.equal(sequence.before(view, {...rect}, 1, 107), true);
        assert.equal(sequence.after(view, {...rect}, 1, 108), true);
        assert.equal(sequence.restacks, 1);
        assert.equal(sequence.deadline, 180);
    }
});

test('activation refuses unrelated, repeated or later invalidation without adopting a new revision', () => {
    const view = {}, rect = {x: 0, y: 0, width: 1000, height: 700};
    const fresh = () => new PreparationActivationSequence('activate_2', 100, 200, 8, 100, 80);
    for (const reason of ['focus-changed', 'window-created', 'window-position', 'window-size',
        'native-state-changed', 'surface-changed', 'grab-changed', 'layout-changed']) {
        const invalid = fresh();
        assert.equal(invalid.invalidated(reason, true, 101), false);
        assert.equal(invalid.reason, 'activation-state-invalidated');
        assert.equal(invalid.invalidated('restacked', true, 102), false);
        assert.equal(invalid.before(view, rect, 1, 103), false);
        assert.equal(invalid.complete, false);
        assert.equal(invalid.revision, 8);
    }
    const unfocused = fresh();
    assert.equal(unfocused.invalidated('restacked', false, 101), false);
    assert.equal(unfocused.reason, 'activation-state-invalidated');
    const repeated = fresh();
    assert.equal(repeated.invalidated('restacked', true, 101), true);
    assert.equal(repeated.invalidated('restacked', true, 102), false);
    assert.equal(repeated.reason, 'activation-state-invalidated');
    const later = fresh();
    later.before(view, rect, 1, 101);
    later.after(view, rect, 1, 102);
    assert.equal(later.invalidated('restacked', true, 103), false);
    assert.equal(later.reason, 'activation-state-invalidated');
});

test('activation completion preserves its short deadline and refuses nested, changed or late frames', () => {
    const view = {}, rect = {x: 0, y: 0, width: 1000, height: 700};
    const fresh = () => new PreparationActivationSequence('activate_3', 100, 200, 8, 100, 20);
    for (const callback of ['before', 'after']) {
        const nested = fresh();
        if (callback === 'before')
            nested.before(view, rect, 2, 101);
        else {
            nested.before(view, rect, 1, 101);
            nested.after(view, rect, 2, 102);
        }
        assert.equal(nested.reason, 'nested-main-dispatch');
        assert.equal(nested.complete, false);
    }
    const late = fresh();
    late.before(view, rect, 1, 101);
    late.after(view, rect, 1, 102);
    late.before(view, rect, 1, 119);
    assert.equal(late.after(view, rect, 1, 120), false);
    assert.equal(late.reason, 'stage-update-timeout');
    const expiredRestack = fresh();
    assert.equal(expiredRestack.invalidated('restacked', true, 120), false);
    assert.equal(expiredRestack.reason, 'stage-update-timeout');
    const moved = fresh();
    moved.before(view, rect, 1, 101);
    assert.equal(moved.after(view, {...rect, width: 999}, 1, 102), false);
    assert.equal(moved.reason, 'stage-layout-changed');
    const replaced = fresh();
    replaced.before(view, rect, 1, 101);
    replaced.after(view, rect, 1, 102);
    assert.equal(replaced.before({}, rect, 1, 103), false);
    assert.equal(replaced.reason, 'stage-view-replaced');
    const reentered = fresh();
    reentered.before(view, rect, 1, 101);
    assert.equal(reentered.before(view, rect, 1, 102), false);
    assert.equal(reentered.reason, 'stage-update-reentered');
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

test('mapped build contract requires both exact reviewed modules from the same package', () => {
    for (const build of ORDERING_BUILDS) {
        const files = [{path: ORDERING_MAIN, size: 2755048, sha256: build.main},
            {path: ORDERING_CLUTTER, size: 1104608, sha256: build.clutter}];
        assert.equal(orderingContractFor(files), `sha256:${build.main}`);
        assert.equal(orderingContractFor([{...files[0], sha256: '0'.repeat(64)}, files[1]]), '');
        assert.equal(orderingContractFor([{...files[0], size: files[0].size + 1}, files[1]]), '');
        assert.equal(orderingContractFor([files[1], files[0]]), '');
        assert.equal(orderingContractFor([...files, files[0]]), '');
        assert.equal(orderingContractFor([{...files[0], path: '/tmp/libmutter-18.so.0.0.0'}, files[1]]), '');
    }
    assert.equal(orderingContractFor([{path: ORDERING_MAIN, size: 2755048, sha256: ORDERING_BUILDS[0].main},
        {path: ORDERING_CLUTTER, size: 1104608, sha256: ORDERING_BUILDS[1].clutter}]), '');
    assert.equal(orderingContractFor({package: '50.1-0ubuntu2.4', HAVE_EIS_EVENT_REF: true}), '');
});

const mappedFiles = (mainPath = ORDERING_MAIN) =>
    `1000-2000 r--p 00000000 08:01 1001 ${mainPath}\n` +
    `2000-3000 r-xp 00001000 08:01 1001 ${mainPath}\n` +
    `3000-4000 rw-p 00002000 08:01 1001 ${mainPath}\n` +
    `5000-6000 r--p 00000000 08:01 1002 ${ORDERING_CLUTTER}\n` +
    `6000-7000 r-xp 00001000 08:01 1002 ${ORDERING_CLUTTER}\n`;

test('mapped module identity comes from live proc mappings and the same opened file', () => {
    const modules = mappedOrderingModules(mappedFiles());
    const file = {device: 2049, inode: 1001, size: 2755048, mode: 0o100644, uid: 0};
    assert.equal(modules[0].path, ORDERING_MAIN);
    assert.equal(modules[0].inode, '1001');
    assert.equal(matchesMappedOrderingFile(modules[0], file), true);
    for (const change of [{device: 2050}, {inode: 1002}, {uid: 1000}, {mode: 0o100664},
        {mode: 0o100646}, {mode: 0o120644}, {size: 0}, {size: ORDERING_FILE_MAX + 1},
        {inode: Number.MAX_SAFE_INTEGER + 1}, {device: -1}])
        assert.equal(matchesMappedOrderingFile(modules[0], {...file, ...change}), false);
});

test('mapping rejects deleted, substituted, writable-executable, missing and ambiguous loaded modules', () => {
    for (const text of [mappedFiles(`${ORDERING_MAIN} (deleted)`), mappedFiles('/tmp/libmutter-18.so.0.0.0'),
        mappedFiles().replace('r-xp', 'rwxp'), mappedFiles().replaceAll('r-xp', 'r--p'),
        mappedFiles().replace('08:01 1001', '08:01 1008'), mappedFiles().replace('08:01 1001', '08:02 1001'),
        mappedFiles().replace('1000-2000', '2000-1000'), mappedFiles().replace('r--p', 'r--s'),
        mappedFiles() + `8000-9000 r-xp 00000000 08:01 1009 ${ORDERING_MAIN}.other\n`,
        mappedFiles() + '\0', 'x'.repeat(ORDERING_MAPS_MAX + 1), '', null])
        assert.throws(() => mappedOrderingModules(text));
});

test('mapping layout identity changes for replacement mappings even with the same installed filename', () => {
    const old = JSON.stringify(mappedOrderingModules(mappedFiles()));
    for (const text of [mappedFiles().replaceAll('1001', '1009'),
        mappedFiles().replace('2000-3000', '2000-4000'), mappedFiles().replace('00001000', '00002000')])
        assert.notEqual(JSON.stringify(mappedOrderingModules(text)), old);
});
