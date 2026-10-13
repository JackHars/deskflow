<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Original input before legacy dispatch

The Server defers a translated input action before changing its press ledger
or writing its legacy input packet. The explicitly configured Mac launcher now
installs `OriginalInputCoordinator` as its dispatcher. The coordinator joins
the actual selected Core connection, private Host lease, Linux native mapping,
Viewer full-input fence and NativeHost destination before original delivery.
Ordinary Deskflow remains the default. Missing or revoked prerequisites cannot
be replaced by a successful fixture or a generic coordinate observation.

This describes the current implementation. See the newest `TESTING.md` entry
for the exact tested candidate and the separate physical acceptance status.

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
head. Poll replies echo the exact ticket; an inline terminal `reserve` reply
has ticket zero and retains its exact operation identity in the reservation
nonce. Duplicate, reused, malformed,
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

Source-side admission requires an observed release followed by a fresh down
for that exact control in the same capture lifetime. Controls initially have
unknown state; neither a timestamp nor a current physical-state query primes
them. Discrete scroll and a fresh gesture beginning have separate transition
evidence. Motion cannot acquire a button owner. Normal local capture observes
releases before the pointer enters a remote screen. An unknown acquisition
cannot enter the private route; under an existing full-input owner it revokes
and suppresses instead of leaking a new legacy press through the closed gate.

The first eligible key or button down chooses its owner. Repeats and releases retain
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

## Deferred private preparation

`LocalInputPreflight::core` can retain its exact quiesced lease while a local
controller performs asynchronous preparation. Its optional notification receives
the lease and the existing absolute two-second attempt deadline. With no
notification installed, the previous synchronous behavior is preserved.

The coordinator calls `continuePreparation(exactLease)` on the Qt owner thread.
This queues at most one continuation and is not a readiness receipt. Processing
it rechecks the retained capture, selected connection, peer policy, code
admission, delivery readiness and the retained source acquisition boundary before the ordinary
private Prepare/Commit exchange. Old, canceled, mismatched, duplicate and
expired continuations cannot activate an owner or renew the deadline.

The dispatcher also obeys the Server's earlier 500 ms deadline and cancels
native work through exact-owner release. The private two-second budget never
extends a pending Server reservation. The scheduling hook itself grants no
native drain permission.

## Native cutoff components

`InputCutoffBinding` correlates one nonreused request with its authenticated
Core connection generation, capture epoch/sequence/part, exact full-input
forwarding binding and Viewer connection. Correlation fields never replace
the retained connection or native ownership checks.

The separately negotiated `IPC1` Core extension sends a bounded cutoff after
previously compressed legacy motion. Its receiver delegates on the existing
core event thread to the owned EI sender. A requested ping is observed only
after the entire returned EI event batch, under the inherited deadline;
device/session loss overrides PONG. Exact resume has a separate receipt.
New legacy writes revoke the observation and order resume before the write.
Old peers and disabled builds do not acquire this optional capability.

`GnomeProviderClient::requestInputDrain` implements the later native segments:
the owned provider waits for an empty Clutter queue outside recursive dispatch,
then a newly completed same-view stage update. It proceeds to a distinct private
Qt `input-drain` operation carrying the same cutoff and the remaining budget.
The Viewer must retain the exact All gate through both Wayland sync callbacks,
QPA processing and final native/paint checks. Gate changes synchronously revoke
pending and retained results. Generic viewport observations cannot stand in
for this operation.

Qt input notifications (including input-method queries) and native handler
dispatch must both be idle. The exact supported Qt build must own its handler
slot and retain either no input context or the built-in QtWayland context with
input methods disabled. Other plugins, replacement/destruction and nested
input dispatch deny. These checks observe normal Qt delivery without filtering
events or replacing a foreign handler.

The provider verifies the mapped Mutter and Clutter ELF identities against
the source-backed build contracts in `gnome-provider/ordering-builds.json`.
The current contracts cover the audited Ubuntu Mutter 50.1 builds only; an
imported symbol or matching version string is insufficient. Unknown builds
report `mutter-eis-sync-build-unverified`. The native fixture exercises an
owned sender and actual compositor/Qt queues; its synthetic Core correlation
does not itself authorize production delivery.

## Retained prefix and batched exact points

The production path negotiates `IPC5` before its first `DICS` request. One
initial old-prefix drain creates a retained seal while the exact All forwarding
gate stays closed. Point observations still have individual absolute lifetimes
of at most 250 ms. Their expiry does not reopen forwarding or invalidate a
healthy stationary held key/button. The ordinary EI inactivity timer is paused
only for this live seal; native session/device loss still revokes it.

`DICB`/`CIMB` map at most 16 distinct frozen capture stamps and routed points
under that seal. Native hit checks bracket one exact Qt painted-map batch;
every record retains its own coordinate and deadline. Keys, buttons, drag,
scroll and momentum records are never collapsed. A new batch can replace old
point receipts only after all preceding private output has been accepted in
source order. The coordinator bounds delivery to one batch plus one waiting
preparation; it does not require a new native queue drain for each event.

Server may compress only adjacent, unreserved absolute hover motions under
an already active owner, with no held controls or active scroll gesture.
Preparing or retained batch lookahead disables compression. Every raw capture
and its final historical `Coalesced` decision is retained; drag/scroll/key/button
records and the final routed point are unchanged.

Qt and Core exchange retained values through a bounded mailbox. Each remains
the sole owner of its channel/backend callbacks. Private protocol v5 sends
the mapped original to NativeHost with its absolute deadline. NativeHost checks
the exact AX/window/process-birth/focus/geometry/lifecycle owner, preserves the
original event fields and targets that retained process/window. This is not a
claim of application consumption.

Cleanup retains the original seal as the resume address. A failed or canceled
handoff cannot return to ordinary delivery until both exact Core resume and
Host completed-release acknowledgments arrive. An explicit empty Host decline
can return `Refused` only after that empty release is acknowledged. Lost focus,
selection, capture, process, surface, device or transport revokes the route and
keeps physical-up tombstones; no new destination inherits a held press.

X11 now has an actual-connection namespace witness and native hit validation,
but no proven old-input application drain. It cannot issue the original-input
permit. Unsupported IME contexts and native transient surfaces likewise deny.

The coordinator fixture uses actual constructed Mac capture records, Core
packet encoders/decoders and an authenticated private child channel without
posting input. Separate native GNOME/Qt and NativeHost tests cover those
adapters. Physical two-machine acceptance remains separate.

## First-click preparation

A fresh eligible button down may negotiate `IPP1` in addition to `IPC5`.
`CIPA` acknowledges the preparation capability. Before `DIPP`, the coordinator
releases the previous authorized legacy controls and reserves cleanup ownership
under the same lock that verifies its current owner. Keyboard, hover and scroll
records cannot use this path to activate a window. An old peer that does not
acknowledge within the bounded negotiation wait declines before any native gate
is acquired.

`DIPP` retains the exact capture stamp, frozen desktop point, authenticated Core
connection and original preparation deadline of at most 500 ms. It carries no
invented Host All lease or Viewer identity. The Linux implementation establishes
a separate preparation gate over its owned Viewers before native focus changes;
it must drain the old EI/compositor/Qt input and identify the exact private
top-level role at that frozen point. Activation changes native focus without
injecting a Linux click. A fresh Qt focus exchange and actual Host focus
acknowledgement produce `CIPF`, including the Viewer-observed Host fingerprint,
connection, session/epoch, focus request/revision and painted geometry.

The coordinator accepts only that live result, then obtains a genuine private
Host All lease matching every returned identity. Only after Host quiescence
does `DIPM` promote the preparation into a mapped cutoff. The mapped receipt's
deadline is at most 250 ms and never exceeds the initial preparation deadline.
The original button remains withheld until the exact mapped observation and
private Host commit authorize its one original delivery.

Before a fully validated Sealed receipt, `DIPX` owns cleanup of both the original
preparation and any linked pending promotion; it must not race a second `DICX`.
After validation, exact `DICX` cleanup owns that sealed route and still drains
the linked preparation gate. That gate survives promotion and an independent
Host All resume until native and Qt cleanup complete. Core `CIPR` Released or
the sealed resume receipt and the private Host release must both finish before
ordinary acquisition reopens. TLS loss, endpoint EOF and a failed provider
revoke delivery but cannot substitute for those acknowledgements.

The backend returns the exact attempted request identity even if a synchronous
write reenters cancellation. An empty result means rejection before any wire
attempt; an ambiguous exception retains cleanup ownership. The Mac regression
fixture exercises real packet decoding, exact Host substitution rejection,
cancellation during both writes, old-peer decline and lost-transport behavior.
Native Linux preparation and physical acceptance have their own results in
`TESTING.md`; passing the Mac fixture alone does not establish them.
