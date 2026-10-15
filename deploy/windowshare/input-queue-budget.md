# Core event backlog limits

P7's explicit experimental Core launches now enforce one process-lifetime
admission budget, independently of optional evidence recording. Ordinary
Deskflow launches retain their existing admission policy. No environment
variable or diagnostic command grants input authority.

The budget is installed before producers start for the configured Mac input
session, GNOME provider client, or explicit X11 observation client. X11 still
cannot authorize original input. Limits are fixed in
`src/lib/base/EventQueueBudget.h`:

| Resource | Limit | Lifetime |
| --- | --- | --- |
| Outstanding Core Events | 4096 | Admission through pending, saved, retrieved and dispatch completion |
| Active Core dispatch scopes | 64 | Includes immediate and nested borrowed callbacks |
| Processed libei events per System dispatch | 4096 | Separate native-batch processing limit, not upstream storage capacity |
| Event residence | 2 seconds | Checked before/after dispatch; independently enforced while dispatch is active |
| Active-dispatch watchdog scan | Every 25 ms | Subject to OS scheduling; terminates only its own experimental Core |
| Admission compare/exchange attempts | 16 | Excess contention terminates the experimental session |
| Empty-buffer fault detection | At most 50 ms between waits | Subject to OS scheduling; not a real-time guarantee |

These are Event/callback counts, not a total byte or RSS limit. Payload sizes
and native/library memory still require the resource workload measurements.
Registered timers/handlers are metadata outside these Event lifetime counts.

All Core event types share the outstanding limit. An Event retrieved by a
caller still consumes a slot. Moving transfers ownership; nested redispatch
must acquire another slot. Local dispatch scopes retain the budget even if a
callback destroys its Event or queue. Neither reconstruction of a queue with
the same budget nor a nested cleanup loop clears terminal failure.

Capacity, deadline, platform-buffer refusal, cross-queue Event reuse, accounting
failure or excessive contention is terminal. Core does not drop a key/button
release and keep routing. On Mac, the first failure synchronously stops the
retained capture source, revoking its epoch, completed inbox and held-motion
gate before the failing producer returns. The event loop exits unsuccessfully;
the existing Core shutdown disconnects the peer and drains/restores the owned
companion.

An independent `EventQueueWatchdog` thread also watches the 64 fixed dispatch
slots. Each slot retains its original admission deadline, including time spent
waiting before dispatch. Nested callbacks and concurrent dispatch cannot renew
an older scope. Retirement clears only its own slot; expiration claims the
exact still-active deadline atomically, preventing a stale observation from
expiring a later lease. Moves, exceptions and destruction of a queue inside its
callback retain the same admission/retirement ownership.

An overdue active scope calls `std::_Exit(71)` in Core. It does not invoke the
revocation callback, log, wait for Qt or run native destructors. Process exit
closes the peer/Host channels, invoking their existing disconnect recovery.
This covers a handler stuck in an OS/library call while the monitor thread is
still scheduled. Construction must succeed before producers start, and the
guard outlives the Core thread and queue teardown. Successful teardown cancels
and joins it. Ordinary launches create no watchdog thread.

This is **active-dispatch recovery**, not a whole-process liveness or real-time
guarantee. Blocking startup, buffer initialization/waits, teardown outside an
active callback, process suspension and kernel stalls remain outside its
scope. A caller-retained Event is denied on reuse after the age limit; its
inactive owner is not preempted. Upstream queues and allocations remain outside
the budget, including allocations made inside a callback before expiration.

The Mac Host separately has an independent event-loop progress monitor; see
[host-recovery.md](host-recovery.md). A Host stall terminates that Host so its
recovery child can reopen the journal. Neither monitor certifies the native
queues before either process or proves physical window restoration.

Startup transfer and producer publication are serialized, preserving the
pending prefix. Cleanup timers also run when the guarded queue remains busy.
Spurious platform wakeups are handled iteratively. Payloads abandoned at
terminal failure, including expired retrieval, are disposed before their
admission tokens retire.

## Platform boundary and evidence

The Mac buffer and Simple buffer each store one posted ID per saved Event, so
the 4096 shared permits also bound their posted IDs. The same invariant applies
to Deskflow's EI and X11 user-event IDs. The EI buffer may additionally retain
one native-readiness marker produced by the event loop's empty-buffer wait.
That marker does not represent every event retained inside libei. X11 IDs can
pass through Xlib/the X server; the shared permit stays held until Core retrieves
and completes the corresponding Event.

The guarded EI consumer separately refuses a batch beyond 4096 processed
native events. It checks the existing two-second deadline before and after
`ei_dispatch`, before each event and after each handler. Overflow or expiry
invalidates the GNOME prefix observation and returns through terminal Core
failure; a partial batch cannot publish its completed-prefix acknowledgement
or resume input. The remaining libei objects are released by context teardown.
Ordinary launches retain their previous native-batch processing behavior.
The active-dispatch watchdog also expires a System handler stuck inside
`ei_dispatch`; neither mechanism bounds allocations made before expiration.
The batch count is deliberately not emitted as a queue-capacity descriptor in
input evidence.

OS HID/WindowServer buffers, libei's decoded native batch, Xlib's other native
events, Qt/compositor input, device buffers and display presentation are outside
this admission boundary. Their complete capacities/latencies are not certified.
A Core System event may cover multiple native events. The actual Mac/EI buffer
tests and private Xvfb smoke do not establish limits for those upstream queues.
The inspected libei, Clutter and Qt paths and their specific gaps are recorded
in [native-queue-audit.md](native-queue-audit.md).

Evidence v2 adds `event-all-outstanding` and `event-all-dispatch`, including
configured actions, timers and control events, alongside the original-stamped
subset. Outstanding limits are 4096; dispatch limits are 64. The normalizer
requires the exact v2 inventory and actual limits, rejects mixed versions, and
does not upgrade a legacy v1 trace's explicit null capacities. It still reports
full pending-input coverage as incomplete. See [input-evidence.md](input-evidence.md).

## Reproducible checks and remaining acceptance

Build `EventQueueTests`, `ServerTests`, `ClientProxyTests`, `ServerProxyTests`
and `windowshare-input-evidence-tests` from the root with window sharing ON.
Repeat ordinary compatibility with it OFF. On Mac also build
`windowshare-original-input-capture-mac-tests`; this constructs private CGEvents
without installing taps or posting input. On Linux build/run `EiComputerTests`;
its owned Unix listener never accesses a desktop or input device.
The additional EIS fixture queues 4096 and 4098 real native seat notifications,
checks the ordinary/guarded difference, and delays the library's actual
dispatch to verify that expired events remain unprocessed. It creates no
devices and does not inject keyboard or pointer input. Linux tests require
`libeis-dev` as well as `libei-dev`.

`EventQueueTests` runs the real two-second deadline in directly owned disposable
test processes. Blocked, nested and concurrent callbacks must exit with code
71; healthy idle/reused slots, exception/destructive dispatch, cancellation and
ordinary unguarded callbacks must survive. A deliberately blocking revocation
callback verifies that emergency exit does not depend on it. The fixture never
launches Core, a Host, an input source or a native window. It also checks
original admission deadlines, moved leases and retirement/reuse directly.

Run EventQueue/capture checks under ASan/UBSan, plus:

```sh
python3 deploy/windowshare/input-evidence-normalize-tests.py
python3 deploy/windowshare/h11-evidence-tests.py
python3 deploy/windowshare/physical-macos-tests.py
```

`TESTING.md` records actual candidate results. P7 acceptance still requires H10
owned-process crash/sleep recovery, H11 actual input/display latency and the
planned hour-long resource workload. Obtain fresh physical readiness before
starting desktop input or destructive owned-process tests. Short offscreen
load runs and synthetic parser fixtures cannot complete those checks. Complete
the native/OS queue audit on the frozen runtime as part of H11; do not convert
an unknown capacity into a guessed positive number.
