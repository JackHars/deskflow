<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Original input before legacy dispatch

The Server can defer a translated input action before changing its press ledger
or writing its legacy input packet. A trusted local dispatcher can then reserve
an original-input destination. The production launcher does not install that
dispatcher yet: ordinary Deskflow delivery remains the default, and this change
does not enable native Mac input posting.

## Capture and order

`LocalInputCoreSession` remains the only consumer of the raw Mac capture stream.
After observing a completed capture, it publishes a copy to a separate bounded
`OriginalInputCapturedInbox`. The inbox binds to one Server incarnation, retains
the original capture epoch and accepts at most 256 records. The first record
after binding may follow earlier locally drained input; subsequent records must
have consecutive sequence numbers and nondecreasing source timestamps.

Untranslated records are copied too. Invalid records, discontinuity, overflow,
source shutdown or capture-epoch revocation terminate this inbox. Inbox failure
does not revoke the raw capture stream or stop its independent emergency path.
The event tap never waits for either consumer.

With no dispatcher installed, the Server drains these copies without deferring
legacy input. When installed, it queues typed copies of keys, buttons, motion
and scroll, retaining the recipient incarnation, selected peer, authenticated
connection and routed desktop point. It retains no borrowed Event or proxy
pointer. A core event-queue timer drains the inbox; pending capture waits expire
after 100 milliseconds. Missing capture and failed eligibility preserve legacy
delivery until an original reservation has been accepted.
Selection changes resolve older queued input before writing leave, enter or
same-screen cursor-warp controls. A disconnected recipient loses its incarnation
before this flush, so pending input cannot reach its replacement.

Diagnostic decisions retain event order as well. `OriginalInputRoutes::defer`
registers a pending stamp before the action is queued. Later immediate filter
decisions wait behind it, and resolving the exact stamp releases only the ready
prefix. Duplicate or overflowing staged decisions revoke diagnostic evidence.
`LocalInputSession.onRoutedInput` remains a historical observation, never an
original-input dispatch callback.

## Reservation and press ownership

Only a live, exact, single-part, unambiguous capture can reach the dispatcher.
Filtering, translation expansion, relative pointer motion, configured modifier
commands, stale selection and missing authentication cannot acquire original
input through this path. The dispatcher must separately establish the native
destination, coordinate mapping and full viewer forwarding fence.

`reserve` reserves output capacity without delivering input. Its result is an
explicit `Refused`, `Pending`, `Accepted` or `Revoked`, not an optional value
that conflates missing authority with asynchronous preparation. A pending result
retains capacity and a strictly increasing request ticket. The Server polls it
on its existing core-thread timer; later operations cannot pass the pending
head. Every reply must echo the exact ticket. Duplicate, reused, malformed,
exceptional or late replies are terminal for original eligibility.

Preparation uses a separate, nonrenewable 500 ms deadline. The existing 100 ms
budget still limits arrival of each completed capture, but a timely capture is
not invalidated by waiting behind older preparation. Pending cancellation is
explicit and idempotent, and provider destruction/Server shutdown aborts retained
work. A provider can return `Refused` after starting a handoff only when its
release/drain has completely finished; `Revoked` suppresses the acquisition.
Callback reentry and destruction cannot expose a dangling in-flight record.

Once a reservation is accepted, failure cannot fall back to a legacy acquisition. `commit` is the
original delivery operation; cancellation releases unused reservations, and
owner revocation requests cleanup at the original destination. Absolute motion
still sends its frozen desktop position to the Linux cursor while the reserved
viewer fence suppresses duplicate application forwarding.

The first key or button down chooses its owner. Repeats and releases retain
that owner across focus and connection changes. Revocation leaves a suppressed
press until its physical release; it cannot transfer to a newly focused window.
Original and legacy presses share the existing 1,024-press bound. Synthetic
modifier cleanup still targets the recipient that acquired those modifiers.
Each fresh press has a nonreused incarnation, so delayed capture cleanup cannot
remove a later press of the same control. Raw physical releases also retire
original tombstones when translation produces no matching key or button up.

The Server bounds pending actions and completed captures to 256 each. Its
original-owner history also has a 256-entry limit; exhausting it ends original
eligibility for that Server rather than forgetting revoked owners. Legacy
delivery and release accounting continue after original eligibility ends.

The `Original` diagnostic outcome means that the local dispatcher committed its
reservation. Its retained connection and capture evidence can still be revoked.
It does not prove that a compositor, Qt widget or Mac application consumed an
event.

## Remaining integration

The production coordinator must install a dispatcher only after native queue
draining, exact routed-point validation, physical neutrality and a full-scope
forwarding handoff have been established. It must retain those conditions
through delivery and release. Neither the viewer handoff acknowledgment nor a
legacy backend submission receipt provides compositor-consumption evidence.

The Server tests use local dispatcher fixtures and actual protocol proxies;
they do not post native input. Physical two-machine acceptance remains separate.
