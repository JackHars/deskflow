<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors -->
<!-- SPDX-License-Identifier: MIT -->

# Host crash and event-loop recovery

An experimental Mac Host must arm its independent recovery child and its
progress monitor before creating the Bridge or allowing an export. Failure to
start either mechanism refuses startup. Ordinary Deskflow and Linux Viewers do
not create these monitors.

The child holds the read end of a private lifetime pipe in a separate process
group. On EOF it waits for the original Host's kernel process-birth identity to
end, including an exited but unreaped Host. It then execs the existing
`--recover --journal PATH` mode. Recovery must acquire the journal lock and
validate the exact saved process/window identities; unresolved entries remain
in the journal. EOF alone cannot authorize recovery while the Host is alive.

The child cannot detect a still-running Host whose event loop is stuck. A
separate thread **inside that Host** therefore checks a monotonic progress
deadline:

| State | Deadline |
| --- | --- |
| Both monitors armed, before the first event-loop heartbeat | 15 seconds |
| After a heartbeat from the creating Host thread | 5 seconds |
| Event-loop heartbeat interval | 250 ms |
| Shutdown requested | No further heartbeat renewal |

Expiry calls `_exit(70)` in the Host itself. It does not signal another PID,
run native/Qt cleanup, flush a log stream or invoke a user callback. The
independent child observes the closed lifetime pipe and starts journal recovery.
This covers an event loop blocked in a native call, including teardown after
the Qt loop has ended, provided the OS still schedules the monitor thread.
Late heartbeats and heartbeats from another thread cannot renew the deadline.
The guard outlives the Bridge and its native destructors; successful destruction
cancels and joins the monitor before closing the lifetime pipe.

These are liveness deadlines, **not native queue-capacity or real-time
guarantees**. A nested event loop that continues to service heartbeats during
ordinary work can still conceal an unfinished native operation. Shutdown stops
renewal even if restoration enters a nested Qt loop. A stopped process, stalled
kernel, suspended computer or stalled recovery executable requires separate
handling/acceptance. The progress guard does not certify Core, compositor, HID,
GPU or application queues, nor prove that a window was physically restored.

Experimental Core separately uses an [active-dispatch watchdog](input-queue-budget.md)
for its two-second event residence policy. A blocked Core callback exits Core
with code 71, closing its Host channel. This Host monitor remains independent
and can expire a Host that stalls while handling that disconnect. Physical
restoration across the combined failure still requires H10.

## Automated checks

Build and run `windowshare-recovery-watchdog-tests` on Mac. Its private recovery
mode writes one exact marker in a disposable path; it does not enumerate or
move windows, install input taps, launch a real Host, or access a desktop.

The suite covers unlaunchable recovery helpers, readiness, crash after arming,
unreaped Host death, startup stalled for the real 15-second deadline, event-loop
stall at the real five-second deadline, ineffective worker-thread heartbeats,
healthy owner-thread heartbeats past the deadline, and cancellation/join on
normal teardown. Each stalled Host must exit with code 70, and only then may
the existing child write the exact marker. The healthy case must remain alive
past both its active period and the cancelled deadline, without early recovery.

`windowshare-bridge-tests` separately checks terminal shutdown and nested
release/restore ordering. The hosted Mac job runs both suites. `TESTING.md`
records the actual local and hosted results and sanitizer scope.

H10 still requires real owned-process failures, window restoration observations
and sleep/wake on the final package. Those attended checks remain deferred.
