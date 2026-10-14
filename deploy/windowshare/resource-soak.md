<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# H11 resource-soak evidence (read-only; physical acceptance still NOT_RUN)

`resource-soak.py` records bounded, **per-process, local** CPU and resident-memory
samples, plus optional Linux disk-I/O counters, without modifying Deskflow,
installing any extension, recording input, or using synchronized clocks.
It never contacts the peer or accesses source window content. Its child process
birth observation is for detecting PID reuse in diagnostics, **not** a native
input-delivery identity/permit. Stop without `--output` overwrites or symlinks.
The Mac `ps` process-birth and CPU time resolutions are only one second.

## Setup on each machine

1. Build and launch the **separate opt-in experimental** app/core per
   [`TESTING.md`](../../TESTING.md). Use disposable, authorized windows.
2. Record the experimental process PIDs locally, confirm they belong to this
   test's processes, and create a **private** evidence directory with mode 0700:
   `mkdir -m 700 -p "$HOME/deskflow-h11"`.
3. On the Mac, replace `$MAC_HOST_PID` with the confirmed PID and run:

   ```sh
   python3 deploy/windowshare/resource-soak.py \
     --pid "host=$MAC_HOST_PID" --duration 3600 --interval 2 \
     --output "$HOME/deskflow-h11/mac-host-20261013.jsonl"
   ```

4. On Ubuntu, replace `$VIEWER_PID` and `$CORE_PID` with the confirmed
   test process IDs and run:

   ```sh
   python3 deploy/windowshare/resource-soak.py \
     --pid "viewer=$VIEWER_PID" --pid "core=$CORE_PID" \
     --duration 3600 --interval 2 \
     --output "$HOME/deskflow-h11/linux-viewer-core-20261013.jsonl"
   ```

5. Review the terminal exit code, the last JSONL `summary` event, the initial
   `start` identity and every `sample`. A death, PID reuse, sampler error,
   missing summary or non-`complete` outcome invalidates the run; keep it as
   failure evidence, do not relabel it as a pass. Subsequent runs must use
   **new output filenames**. `python3 deploy/windowshare/resource-soak-tests.py`
   exercises the collector itself on either OS; it does **not** test Deskflow.

The JSONL format `deskflow-resource-soak-v2` contains elapsed monotonic seconds,
per-process cumulative CPU seconds, RSS bytes and Linux disk read/write bytes.
Every process probe has its own monotonic start/end observation window; the
header records CPU counter resolution. Counter regression invalidates the run
instead of being clamped into zero usage. The diagnostic summary uses probe
midpoints. It reports p50/p95/max *per-interval* process CPU usage (100% means one full
logical core) and peak RSS. No credentials, argv, typed text or window titles
are captured. Linux `/proc` start-ticks distinguish reused PIDs. If the
collector cannot observe the exact original process, it refuses rather than
silently adopting a replacement. Sample count is capped at 25,000 and at most
8 processes; a request above this budget is rejected before opening evidence.

## Provisional budgets chosen BEFORE physical measurements

For the user's Mac/Ubuntu setup, propose a *pilot* 60-minute run with two active
30-fps exports and a second run with one window hidden/minimized. Proposed
process/resource limits: no unexpected process exit or session loss, no stale
held keys after return, no increasing dropped-frame backlog; peak RSS below
1 GiB per experimental host/viewer and 512 MiB for the Linux core; less than
100 MiB net RSS growth for each long-lived process per hour; average CPU under
200% per host and viewer and 75% for the core. These are **untested engineering
budgets**, not established product measurements. Hardware capability, app workloads and whether GPU decode is used must
be recorded with each future run. Do not silently revise targets after seeing
results; record the target revision and rationale first.

## Fixed budget report

`resource-budget.py` evaluates v2 traces offline against those existing pilot
budgets (`pilot-20261013-v1`). Thresholds cannot be changed by command-line flags:

```sh
python3 deploy/windowshare/resource-budget.py /absolute/private/run/mac-host.jsonl
python3 deploy/windowshare/resource-budget.py /absolute/private/run/worker.jsonl --role worker=viewer
python3 deploy/windowshare/resource-budget-tests.py
```

Targets named `host`, `viewer` and `core` use the corresponding budget. Other
labels require an explicit role for every target. The report recomputes results
from every raw sample, includes the trace hash and rejects missing/duplicate
slots, changed process sets, CPU/disk regressions, unsupported timestamps,
incomplete collection and malformed/truncated evidence. It does not trust the
collector's rounded summary as the budget calculation.

Average CPU uses the smallest possible interval between the first and last
probe windows and adds both endpoints' counter uncertainty. Positive RSS
growth uses that same interval; shrinking RSS uses the longest possible
interval so the upper bound does not overstate released memory. Limits are exclusive as specified above ("below"
and "under"). At least 3600 planned seconds are needed; a short diagnostic run
or old v1 trace is **INCOMPLETE**, never an hour-long acceptance pass. Exit codes
are 0 for a resource-component PASS, 1 for a measured budget failure and 2 for
incomplete evidence. All reports retain `h11_complete:false`,
`measurement_authenticity_verified:false` and `workload_verified:false`.
The parser cannot establish native provenance or verify that the intended
two-active/one-hidden app workload actually ran. Sampled RSS is not the true
allocation peak. Record these limits with any component result.

For a reproducible automated decoder/Viewer workload, see
[media-load.md](media-load.md). Its synthetic loopback process and offscreen Qt
paints do not substitute for native capture or an attended production soak.

`resource-soak.py` does **not** observe GPU utilization, per-process network
bandwidth, presentation/frame drops, input latency or end-to-end Mac capture →
remote compositor → Qt paint. H11 additionally requires the existing physical
input-receipt harness, frame/transport diagnostics with explicit source and
receiver timestamps and clocks, and platform tools such as per-process
`nvidia-smi pmon` / GPU monitoring and scoped network counters. Cross-machine
monotonic timestamps cannot be subtracted directly. Without those independent
measurements and H11 evidence, mark the complete H11 outcome **NOT_RUN**, not
PASS. The collector is a reproducible resource-evidence component only.

After a run, preserve only selected evidence and SHA-256 hashes under a private
location. Do not upload whole desktop logs, installed-app lists, process
arguments, paired peer secrets or user-input content to the repository.
