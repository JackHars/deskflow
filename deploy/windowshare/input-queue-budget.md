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
| Event residence | 2 seconds | Checked before dispatch and after its handler returns |
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
companion. Shutdown of a handler that never returns still relies on the
independent watchdog. The two-second age limit cannot preempt a blocked handler
or an OS/library call. A caller-retained Event is denied on reuse after the age
limit; no separate expiry thread preempts its owner.

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

OS HID/WindowServer buffers, libei's decoded native batch, Xlib's other native
events, Qt/compositor input, device buffers and display presentation are outside
this admission boundary. Their complete capacities/latencies are not certified.
A Core System event may cover multiple native events. The actual Mac/EI buffer
tests and private Xvfb smoke do not establish limits for those upstream queues.

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
