// SPDX-FileCopyrightText: 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import test from 'node:test';
import {OwnedDragPlacement, OwnedMoveGrab} from '../window-drag.js';

const association = `org.deskflow.window.${'a'.repeat(64)}`;
function fixture() {
    const window = {}, layout = {};
    let frame = {x: 100, y: 100, width: 640, height: 368};
    const placement = new OwnedDragPlacement({association, drag: 1, window, layout, frame, now: 0});
    const provider = {current: {window, layout}, frame: target => target === window ? {...frame} : null,
        move: (target, x, y) => { assert.equal(target, window); frame = {...frame, x, y}; }};
    return {placement, provider, window, layout, setFrame: value => { frame = value; }};
}

test('exact compositor placement acknowledges only its actual frame and original size', () => {
    const f = fixture();
    assert.deepEqual(f.placement.place(4, -40, 200, 10, f.provider),
        {drag: 1, sequence: 4, revision: 1, x: -40, y: 200, width: 640, height: 368});
    assert(f.placement.verify(4, 11, f.window, f.layout, f.provider.frame(f.window)));
    assert.throws(() => f.placement.place(4, 20, 30, 12, f.provider));
    assert.throws(() => f.placement.place(5, Infinity, 30, 12, f.provider));
});

test('native boxed rectangle fields are preserved even when they are not enumerable', () => {
    const window = {}, layout = {};
    let x = 100, y = 100;
    const boxed = Object.create(Object.defineProperties({}, {
        x: {get: () => x}, y: {get: () => y}, width: {get: () => 640}, height: {get: () => 368},
    }));
    assert.deepEqual({...boxed}, {});
    const placement = new OwnedDragPlacement({association, drag: 1, window, layout, frame: boxed, now: 0});
    const actual = placement.place(1, 120, 130, 10, {
        current: {window, layout}, frame: () => boxed, move: (_window, left, top) => { x = left; y = top; },
    });
    assert.deepEqual(actual, {drag: 1, sequence: 1, revision: 1, x: 120, y: 130, width: 640, height: 368});
    const grab = new OwnedMoveGrab(window, 1, boxed, {x: 140, y: 142}, 10);
    assert.deepEqual(grab.end(window, boxed, {x: 140, y: 142}, 20, true).frame,
        {x: 120, y: 130, width: 640, height: 368});
});

test('native clamp, resize and reentrant cancellation cannot fabricate placement', () => {
    for (const outcome of ['clamp', 'resize', 'cancel', 'throw']) {
        const f = fixture();
        f.provider.move = () => {
            if (outcome === 'throw') throw new Error('Native operation failed');
            if (outcome === 'resize') f.setFrame({x: 20, y: 30, width: 641, height: 368});
            if (outcome === 'cancel') f.placement.close();
        };
        assert.throws(() => f.placement.place(1, 20, 30, 10, f.provider));
        assert(f.placement.closed);
    }
});

test('layout, identity, timeout and changed acknowledged frame retire placement', () => {
    for (const change of ['layout', 'window', 'deadline', 'moved']) {
        const f = fixture();
        f.placement.place(1, 20, 30, 10, f.provider);
        assert.equal(f.placement.verify(1, change === 'deadline' ? 360 : 11,
            change === 'window' ? {} : f.window, change === 'layout' ? {} : f.layout,
            change === 'moved' ? {x: 21, y: 30, width: 640, height: 368} : f.provider.frame(f.window)), false);
        assert(f.placement.closed);
    }
});

test('heartbeats cannot revive expired placement or extend the absolute drag budget', () => {
    const f = fixture();
    f.placement.place(1, 20, 30, 10, f.provider);
    for (let now = 100; now < 30000; now += 100)
        assert(f.placement.hold(now, f.window, f.layout, f.provider.frame(f.window)));
    assert.equal(f.placement.hold(30000, f.window, f.layout, f.provider.frame(f.window)), false);
    assert.equal(f.placement.hold(30001, f.window, f.layout, f.provider.frame(f.window)), false);
});

test('ordinary native grab remains exact, single-ended and cannot treat cancellation as release', () => {
    const window = {}, frame = {x: 1, y: 2, width: 640, height: 368}, point = {x: 50, y: 10};
    const grab = new OwnedMoveGrab(window, 9, frame, point, 0);
    assert.equal(grab.sample(window, {...frame, x: 10}, {...point, x: 59}, 1).sequence, 1);
    assert.equal(grab.end(window, {...frame, x: 20}, {...point, x: 69}, 2, true).sequence, 2);
    assert.equal(grab.end(window, frame, point, 3, true), null);
    const cancelled = new OwnedMoveGrab(window, 10, frame, point, 0);
    assert.equal(cancelled.end(window, frame, point, 1, false), null);
    const replaced = new OwnedMoveGrab(window, 11, frame, point, 0);
    assert.equal(replaced.sample({}, frame, point, 1), null);
});
