<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# Native input queue audit — 2026-10-14

**Result: full queue-capacity certification remains incomplete.** The Core
and private protocol limits are enforceable application boundaries. Several
native libraries append to queues without a depth limit in the inspected
path, and the operating system/device queues have no verified capacity or
latency contract here. A successful finite workload cannot establish those
missing bounds. Do not replace an unknown capacity with the largest observed
depth, a socket's byte capacity, or the Core's 4096-event limit.

This audit covers the experimental Mac/Ubuntu route. The retained runtime is
macOS 26.1 (25B78), Qt 6.8.3 on Mac; Ubuntu 26.04, libei/libeis 1.5.0, Mutter
50.1-0ubuntu2.4 and Qt 6.10.2 in the isolated Linux fixture image. Source-file
hashes and provenance are in `native-queue-audit-sources.json`. These source
observations do not certify a different OS or library build, or equate the
headless Ubuntu test compositor to the user's desktop compositor.

| Boundary | What is established | Remaining limitation |
| --- | --- | --- |
| Mac capture → Server → Core EventQueue → private Host channel | The application admission/ownership limits and v2 mutation inventory are implemented; see `input-evidence.md` and `input-queue-budget.md`. Capacity or age failure terminates routing. | Recording begins at the capture callback, after device/HID/WindowServer buffering. Evidence-buffer loss invalidates a collection. |
| Mac/Simple/EI/X11 posted Core IDs | Each saved user Event retains a shared admission permit until retirement. EI has an additional readiness marker. | An EI System marker can represent many native events; Xlib also stores unrelated native events. These are not additional copies of the Core queue metric. |
| Original-input preparation and committed delivery notifications | The private Qt thread uses a mutex-protected latch admitting at most one queued wake; Core uses its existing coalesced progress event. Reservation, exact focus/map replies, private activation, committed delivery and cleanup use these wakes. They retain no input payload; records remain in bounded operation queues. Shutdown retires callbacks before owner destruction, and consumers recheck admission and deadlines. Regressions disable maintenance polling through keyboard/first-click preparation and subsequent scroll delivery, and cover lost admission and callback destruction. | This bounds this application's wake notifications, not Qt's general event queue or OS scheduling latency. Maintenance timers remain for expiry and cleanup; notifications neither grant authority nor extend deadlines. |
| Asynchronous Host native inspections and result delivery | One shared process limit admits at most 64 inspections, across result types and destroyed/replaced owners. A permit survives cancellation and expiry, the native read, deferred QThread destruction, and queued-result consumption or discard. Exhaustion refuses placement and restores existing exports if the environment watchdog cannot obtain a slot. Regressions fill all slots with blocked queries and with completed results whose thread objects have been deleted; replacement work remains refused until the retained work retires. Only the read-only environment watchdog prioritizes completion over ordinary queued work. Placement retains FIFO ordering with queued cancellation, and both retain their original deadlines. | This caps this application's outstanding inspections and result events, not allocations inside an individual native call or unrelated Qt events. Priority cannot preempt a running native call or revive an expired observation. Native IPC cannot be cancelled; deadlines reject late results without treating the work as finished. Other OS/library queues below remain outside the bound. |
| Mac/Linux lifecycle diagnostic output | One worker retains at most 128 records / 256 KiB including the in-flight record, with a 16 KiB line limit and separately bounded loss notice. Producers never wait for descriptor writes. Shutdown waits at most 250 ms before detaching owned state. Explicit loss and malformed records invalidate Active diagnostic evidence. | Producer formatting/allocation, other stderr writers, kernel output buffering and log readers are outside this queue. Closed output and process exit can lose logs; this is not an input receipt or native queue bound. See [diagnostic output](diagnostic-output.md). |
| libei decoded event and per-device pending-frame queues | In 1.5.0, `queue_event` appends to intrusive lists. A frame moves pending device events into the context list. | No depth admission check appears in that enqueue path. The application sees events only after `ei_dispatch` has parsed/allocated them. |
| libei unsent protocol output | `ei_send_message` tries flushing/writing, then `ei_queue_unsent` appends on EAGAIN. | No aggregate byte/count admission check appears in that append path. A bounded socket does not bound this userspace list. Existing route deadlines do not preempt a blocked native call. |
| Core consumption of a libei batch | Explicit experimental Core now permits at most 4096 processed native events per System callback and checks two seconds around dispatch/handlers. Overflow/expiry invalidates prefix state and exits the session. An independent active-dispatch monitor exits Core if the callback never returns. | This is a processing limit, not a libei allocation cap. No partial batch can qualify as a complete prefix. Native calls outside an active dispatch and stopped processes are outside the monitor's scope. |
| GNOME Clutter queue | Mutter's `_clutter_event_push` appends to `GAsyncQueue`; the provider requires an empty queue at the correct nonrecursive checkpoint and a later stage completion. | The inspected enqueue path has no depth guard. An empty checkpoint is an ordering observation, not a permanent capacity guarantee or physical-input receipt. |
| Wayland default input queue / Qt QPA | Qt's Wayland thread dispatches the retained default queue. QPA asynchronous delivery appends a new window-system event. The production witness uses the existing queue and rechecks identity/focus/paint afterward. | No depth guard appears in the inspected QPA append path. The witness does not limit Wayland or Qt allocation, and cannot preempt native dispatch. |
| Core TCP/TLS, private Unix sockets, D-Bus/GLib and Qt notification delivery | Application framing, request counts and deadlines are checked at their existing interfaces. | Kernel buffering, TLS/native-library allocations and general event-loop callbacks are outside the complete mutation inventory; whole-path capacity is not certified. |
| macOS HID/WindowServer, target AppKit delivery, device firmware, GPU/compositor/display | Existing capture/identity/revocation checks reject changed ownership and unsupported prerequisites. | No complete queue-capacity or display-completion instrument has been established. Source tags, send success and Qt paint cannot substitute for native consumption or physical display timing. |

The private EIS regression sends real protocol seat notifications to the
actual `EiComputer` context, deliberately fills the libei event queue, then
runs the production System handler. The fixture confirms an ordinary large
batch drains, an experimental batch at the limit drains, and an over-limit
batch leaves its tail for teardown and terminates Core. A delay inside the
actual library dispatch checks expiry before consuming the decoded event.
There is no compositor, input device, desktop route or delivery permit.

The later [Host progress monitor](host-recovery.md) bounds a stalled Mac Host
event loop through self-exit and existing journal recovery, subject to OS
scheduling. Its deadlines are independent of the Core event residence budget.
The additional Core [active-dispatch monitor](input-queue-budget.md) expires a
callback that never returns, even while nested/concurrent callbacks complete.
These add recovery coverage without establishing any missing upstream
allocation limits or preempting a separate compositor/library process.

The next native acceptance run must freeze its actual runtime and retain its
queue coverage review beside the raw input traces. H10 still requires owned
host/helper/core/viewer failures and sleep/wake with restoration observations.
H11 still requires actual input/display timing and sustained native workloads.
The user deferred these attended tests on 2026-10-14. Neither the audit nor the
offscreen load fixture changes their acceptance status.
