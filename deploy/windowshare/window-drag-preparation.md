<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Ordinary window dragging

P5's ordinary-titlebar paths are connected in both directions through real-release
drop and ordinary Core cursor transfer. The original native press and its real up
retain their owner throughout. A painted remote preview is an internal part of
that same drag; no separate selection or thumbnail interaction is required.
Physical acceptance remains open. Prioritize ordinary dragging through the Mac's
top edge onto Ubuntu above it; interactive drag-back is last. Ask immediately
before a physical fixture or changing the user's running input session.

## Outbound production path

1. The existing sole raw consumer joins the annotated native press to the
   Server's configured boundary. `LocalWindowDragCore` requires live capture,
   the exact press, Core TLS continuity, companion code admission and the
   immutable explicit Core-pin → WindowShare-pin policy.
2. Private protocol **v10** negotiates `localInputWindowDragCapability` (16),
   which requires pointer observation (8). Message **20** is **232 bytes**,
   Core → Host: Begin, Motion, Hold, Cancel, Drop or Finish. Message **21** is
   **240 bytes**, Host → Core: Ready, Committed, Cancelled or Finished plus the
   exact command binding. These have independent replay state; they do not
   prepare an input lease or advance original-input delivery counters.
3. Host independently matches the prior joined down, both configured pins and
   its current authenticated WindowShare connection. NativeHost retains the
   exact AX/native window, process birth, ordinary titlebar hit, unchanged
   window extent and physical layout before starting provisional capture.
4. Bridge requires negotiated `window-drag-preview-v1` and
   `window-drag-motion-v1`. The Viewer paints with provisional no-focus/show
   hints and denies all input before commit. Its grip offset comes from the
   original native titlebar observation. Placement uses the inherited provider
   socket and exact owned Wayland association. An acknowledgement requires
   native placement, an actual later Qt paint and an exact native frame/revision
   recheck. No peer-supplied PID or unrelated window can be addressed.
5. Core arms the original capture adapter's movement-only gate after Ready.
   While that first preview is preparing, retain only the latest boundary
   candidate; keep Begin/Hold at the exact initial position until its Ready
   arrives. Otherwise continuous movement can supersede every in-flight paint
   acknowledgement and prevent arming. After exact readiness and fresh admission,
   forward the retained latest position once, preserving its original sequence.
   Only held left-button movement for that exact press is withheld before
   native/legacy dispatch. The preserved raw deltas still reach the sole
   consumer and move the preview. Every original button transition passes.
   The gate has a 350 ms deadman; Core polls every 25 ms and sends Hold at most
   every 100 ms. The absolute drag budget is 30 seconds. Crossing back over the
   entry edge cancels the preview and resumes the original native drag.
6. Real up ends motion holding. Core joins that exact raw release to the
   independently annotated window up and Server's Local button dispatch.
   Server retains a bounded release reservation whose producer-sequence fence
   revokes it on any later raw input. No timer or empty queue manufactures a
   release. Drop has an absolute 1,500 ms budget.
7. The remote Viewer finishes and verifies the final native placement/paint.
   NativeHost additionally requires the matching retained real up, HID left-up,
   current exact window/process/layout and unchanged extent. It journals the
   source's latest frame before moving it to the holding display. Failure to
   create the holding display cannot silently leave two active copies.
8. Only after native commit does Core request ordinary Server cursor transfer.
   Server rechecks the producer fence, exact peer/layout, no held inputs and no
   pending dispatch, then performs its normal switch. Finish retires only the
   drag coordinator; the exported window and recovery record remain alive.
   Failed transfer cancels and restores the source.

The post-release native query is not advertised as a fabricated native-grab
completion receipt. This design preserves native button ownership and observes
both the real release and HID-up state; physical behavior still needs H07.
The Viewer commit changes the focus hint on the existing QWindow and mirrors
QWidget's stored flags. Calling QWidget::setWindowFlag here recreates the
Wayland surface and loses the compositor's placement; the actual GNOME
regression explicitly checks that neither identity nor frame changes.

Escape, another press, lost capture/annotation, topology/peer/admission loss,
private-channel loss, stale paint, resize, cancellation and shutdown all retire
the exact attempt. Callbacks are rechecked after reentrancy. Untouched
preparation cancellation does not move or raise the source. Durable recovery
covers attempted native mutations.

### Capture boundary at arming

`armWithPrefix` returns the capture epoch and sequence through which native
movement can already be queued. The producer publishes its sequence before its
suppression decision; the request and that sequence use a shared sequentially
consistent atomic order. Thus an unheld decision made before arming cannot
escape the returned prefix. The producer remains lock-free, with no allocation,
native query, additional consumer or posted input.

The coordinator validates that receipt against the acknowledged preview and
the current owner. Queued raw deltas inside the prefix advance only the preview,
once per original sequence. Mac primary-screen legacy translation samples the
live cursor, marks itself uncertain and may emit zero parts at a clamped edge;
those facts do not invalidate the original raw movement inside this exact prefix.
Wrong stamps, modifiers, other drag buttons or multiple translated parts still
cancel. Later movement must carry actual suppression evidence with zero certain
translation parts. Native motion outside the prefix cancels instead of being
silently ignored. Every real up retains the existing independent release joins.

`drag.motion.arm` records the initial acknowledged sequence, latest forwarded
sequence and returned native prefix, or failed admission. `drag.motion.capture`
records an unexpected native movement beyond that prefix. These are lifecycle
diagnostics, not per-motion logging or a new input-authority mechanism.

## Return production path

The GNOME provider observes the exact owned Viewer's normal native move grab,
with ordered begin/motion/end/cancel metadata and the actual released state.
`WindowDragWatcher` joins this to a committed painted Viewer and sends a strict
`window-drag-move-observation-v1` message through the existing authenticated
session/epoch envelope. Replaced geometry/surface, native denial, missing reply,
retirement, replay and late callbacks cannot revive the retained observation.
This metadata establishes the owned moving window, not the originating physical
device or an original-input delivery permit. The provider and endpoint retain
at most 16 concurrent watches; retiring windows releases capacity. Late events
cannot revive a retired watch, and late successful Watch replies are cleaned up.

1. Host resolves the exported native Mac ID locally, binds the authenticated
   WindowShare pin and assigns a monotonic move offer. Private v10 capability
   `localInputWindowMoveCapability` (32, requiring 16) carries message **22**:
   **112 bytes**, Host → Core, Begin/Motion/End/Cancel plus exact session, epoch,
   geometry, native grab/sequence, source frame and point. It is observation
   metadata with independent replay state; it acquires no input lease.
2. Core joins the native move to its independently retained original left down,
   actual Server dispatch to the exact authenticated Ubuntu recipient, the
   starting native frame, explicit dual-pin policy and a configured edge back
   to the primary computer. It first waits for Session to enqueue that down's
   private metadata. The command's nonzero `returnOffer` binds the direction.
   Remote input is consumed at the HID tap, so the existing bounded pointer
   join may expire without an application annotation and report window=0.
   Return does not invent a Mac window-under-pointer: GNOME supplies the owned
   Viewer identity, Host resolves its Mac source, and Core supplies the press.
3. NativeHost revalidates the retained source identity, process birth, holding
   frame, capture geometry, lifecycle and physical outputs. Bridge paints a
   frameless, non-activating, input-transparent Mac preview using that source's
   current capture. Ready requires a fresh Qt paint and the actual owned Cocoa
   content view's exact visible WindowServer frame. Held raw deltas move this
   preview; normal buttons still go to their original Ubuntu recipient.
4. Drop requires the original Core release and its Server dispatch, the same
   bounded raw-producer fence, and GNOME's matching native End with released
   state. Both release orderings are supported. Missing release, new input,
   native cancellation, lost preview, changed layout/identity or timeout cancels.
5. NativeHost independently requires HID left-up and the exact destination,
   persists the destination in the recovery journal, restores the retained
   source and focuses its application, then retires its capture/remote session.
   An attempted restoration failure retains recovery. Only after this commit
   may Server transfer the cursor back to the Mac with no held inputs. If that
   last transfer fails, the source remains available locally; no second press
   is generated and no failed transfer is reported as successful.

The return preview is an internal visual part of ordinary titlebar dragging.
It requires no source-selection step and cannot emit keyboard or pointer input.
A destroyed QWidget can emit its destruction signal before QPointer clears;
cleanup explicitly retires that pointer before invoking any Viewer methods.

## Automated verification

Build the enabled Mac Core/WindowShare application and the disabled Core.
Relevant test targets are `ServerTests`, `windowshare-drag-coordinator-tests`,
`windowshare-local-input-tests`, `windowshare-window-drag-edge-tests`,
`windowshare-window-drag-motion-gate-tests`, `windowshare-drag-preview-tests`,
`windowshare-drag-watcher-tests`, `windowshare-viewer-tests`,
`windowshare-bridge-tests` and `windowshare-native-tests`.
Viewer/Bridge tests use `QT_QPA_PLATFORM=offscreen`; they post no desktop input.
Use a separately ad-hoc-signed copy when macOS refuses an unsigned executable.

The real private-channel coordinator suite has 53 child-process scenarios:
preparation/ownership/lifetime failures plus normal drop, raw-up/Server ordering,
wrong annotated window, wrong route, newer input, native denial, transfer failure
and missing commit. Return cases also cover both native/Core release orderings,
missing native release, native cancellation, original-route mismatch, newer input,
failed final transfer and unannotated remote pointer metadata. Continuous
preparation covers right/top edges, latest-position coalescing, queued native
motion (including zero-part legacy translation), exact capture prefixes,
interrupted arming, early release and rejection after actual suppression loss.
The motion-gate suite also races the producer and coordinator 2,000 times, with
no sleep statements or native input. Native callbacks in this suite are explicit fixtures;
separate native tests establish their platform behavior.

For native Viewer continuity, build Linux target
`windowshare-gnome-drag-surface-fixture` and run:

```sh
python3 deploy/windowshare/gnome-window-drag-tests.py \
  --fixture /absolute/build/windowshare-gnome-drag-surface-fixture \
  --output /absolute/new-evidence-directory
```

Use only the isolated GNOME 50 container described by
`gnome-provider-container.sh`: private session/system buses, private settings,
headless monitor and software rendering, with no user display or input mounts.
The harness checks the actual production Viewer and native compositor tokens,
not simulated Qt global positions. Its synthetic EI events remain inside that
owned compositor. Results and exact source/binary hashes are in `TESTING.md`.

## Remaining acceptance

- [x] Ordinary Ubuntu-to-Mac edge return, source pose/focus and remote retirement code.
- [x] Connected coordinator/Bridge/Server release, failure, timeout and topology regressions.
- [ ] H07 physical Mac → Ubuntu → Mac, canceled drag, output/connection changes,
      delayed first frame and exact original down/up counts.
- [ ] Arbitrary native/browser/Electron application titlebar compatibility.

Use [h07-edge-drag.md](h07-edge-drag.md) for the deferred attended verification.
The owned AppKit native titlebar eligibility run passes physically. It does not
satisfy the movement-hold or end-to-end H07 tests; those remain NOT_RUN.
