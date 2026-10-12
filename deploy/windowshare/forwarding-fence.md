<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Viewer input forwarding handoff

The optional `input-route-v1` capability closes keyboard, button, movement and
scroll forwarding together in the real Viewer and NativeHost paths. It extends
the existing keyboard-only handoff. It does not select original input in the
Deskflow Server, drain native compositor queues, or authorize Mac input posting.

## Negotiation and binding

The Viewer offers `keyboard-route-v1` and `input-route-v1` in Hello. The host
acknowledges only supported offered capabilities. Full forwarding requires both.
Peers that offer only the keyboard capability keep the keyboard-only behavior;
peers without either continue ordinary viewer input.

`KeyboardRoute` retains message type 17 and keyboard-only semantics. `InputRoute`
uses message type 18 and full-input semantics. Both use the existing bounded,
exact JSON schema: generation, focusRequest, focusRevision, geometry, action,
mode, and the action's result or reason. The packet header supplies session and
epoch. Original key codes and capture records are never carried by this control.

Scope is part of the retained binding. Rewriting a full-input acknowledgment or
resume as a keyboard-only packet changes that binding and cannot reopen the
gate. Generations are shared across scopes and never reused. Unnegotiated full
controls or mismatched active replies terminate the connection.

## Forwarding order

1. The host reserves an exact current native session/focus/geometry binding and
   retains its authenticated connection and setup deadline.
2. Viewer rejects handoff while a button is held or delivered. It does not
   synthesize a button release to force readiness. Once eligible, it closes the
   requested gate before invoking callbacks, releases delivered keys to their
   existing owners, and drops affected pending input before acknowledging.
3. Each pending-focus flush retains the accepted handoff generation for keys and
   pointers separately. A callback that pauses and immediately resumes cannot
   replay entries detached from the member queue. Keyboard-only handoff still
   leaves queued pointer input intact.
4. NativeHost independently requires an empty Qt input ledger and fresh native
   focus/geometry. Its full gate rejects network pointer acquisition, movement
   and scrolling as well as keys. Exact-owner releases and lifecycle controls
   remain available. A peer cannot bypass this gate by bypassing QWidget.
5. Invalidation leaves the gate closed. The local controller must revoke and
   release original ownership before an exact resume can reopen forwarding.
   Viewer sends the resume acknowledgment before admitting fresh input.
6. Keys and buttons pressed while fenced retain suppression records until their
   physical releases. A resume, focus change, or callback cannot retarget them.
   Pointer movement and scrolling remain suppressed while such buttons are held.

The gate changes only exported-window forwarding. It does not stop ordinary
Deskflow from moving the Linux cursor or controlling Linux applications.

## Remaining integration

`reserveOriginalInput` selects full scope in the Bridge. The production launcher
does not yet install the pre-dispatch controller that invokes it. The existing
private input lease represents keyboard scope only; it must not silently erase
full scope or start an unrepresentable handoff.

Before enabling original input, the implementation still needs an ordered
Server decision, exact routed coordinates, native queue draining, physical
neutrality, original press ownership, and delivery to the exact Mac source.
Neither this acknowledgment nor an input-submission receipt proves that Mutter,
Wayland, Qt or the Mac application consumed an event.
