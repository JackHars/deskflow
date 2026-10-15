<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: MIT
-->
# Reproducible media load (P7)

`windowshare-media-load` exercises production `Channel`, FFmpeg decoder and
`Viewer` objects with disposable pinned TLS identities on loopback. It forces
Qt offscreen before constructing QApplication and does not pass platform
arguments to Qt. It creates no NativeHost, source capture, input tap or input
permit. Automatic Viewer focus requests receive explicit denial.

The default source is one generated 64×48 independent H.264 image, repeated
at the chosen rate. This is synthetic workload evidence, not actual Mac capture,
WAN/LAN throughput, display presentation or H11 acceptance. The fixture commits
its own Viewers after their painted Ready; it does not run the Bridge or native
source scheduler. CPU/RSS measured for this process includes its loopback sender,
all decoder threads and Qt; it cannot be split into production host/viewer costs.

## Run

With the existing application dependencies:

```sh
cmake -S src/apps/windowshare -B build/media-load -DWINDOWSHARE_APP=ON
cmake --build build/media-load --target windowshare-media-load windowshare-viewer-tests --parallel 3
ctest --test-dir build/media-load --output-on-failure -R '^windowshare-(media-load-.*|viewer)$'
build/media-load/windowshare-media-load --duration 30 --streams 4 --fps 30 --scenario steady
build/media-load/windowshare-media-load --duration 30 --streams 4 --fps 30 --scenario mixed
```

Use separately signed copies on a Mac requiring ad-hoc code signing; keep the
copy next to the built binary so its dependency paths remain valid. CTest alone
does not sign binaries. The normal root build places executables in `bin/`.

To use a larger generated source, predeclare the dimensions and retain this
command with the resulting frame and report:

```sh
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=1 \
  -frames:v 1 -c:v libx264 -preset ultrafast -tune zerolatency \
  -pix_fmt yuv420p -f h264 /absolute/private/new-frame.h264
build/media-load/windowshare-media-load --frame /absolute/private/new-frame.h264 \
  --duration 30 --streams 2 --fps 60 --scenario steady
```

The tool validates an independent SPS/PPS/IDR and decodes its actual dimensions;
it never labels the small default image 1080p. Limits are 2–16 streams, 1–60 fps,
2–3600 seconds, one bounded access unit and at most 3840×2160 decoded pixels.
The source frame SHA-256, dimensions and requested workload appear in the report.
Record the tested executable's SHA-256 separately. Retain failed reports too.

## Workload and result contract

* `steady`: regularly offer one IDR per stream, rotating the first stream.
  The production channel permits one frame until its TLS output drains. The
  fixture retains at most one pending IDR per stream, explicitly counting
  replacements when the source outruns that gate. It never queues an unbounded
  backlog or bypasses transport admission. These are fixture scheduling rules,
  not a measurement of the native capture scheduler.
* `mixed`: at one-quarter duration, inject 64 calls to the actual decoder
  admission entry point for the first Viewer in one event-loop turn. This
  intentional fault bypasses transport only and is separately counted as
  `injectedDecoderBurstFrames`; it is not described as network receipt. The
  production decoder bound must hold, drain, request one fresh IDR and recover.
  At half duration hide the last Viewer, then show it at three-quarter duration.
  Hidden frame admission/paint must stop and a new recovery frame must paint
  after showing. The other streams continue receiving load throughout.

An exit code of zero / `status:PASS` means these **functional workload
invariants** held: every accepted TLS frame reached the intended Viewer,
receive/admit/ignore counts reconcile, admitted work retired, actual queue
peaks stayed within the production capacities, queues drained, no decode error
or unexpected return occurred, and every stream made paint progress. Steady
load requires no overflow/recovery; mixed load must actually exercise overflow
and hidden/resumed recovery. It does not mean a performance target passed.

Reports always retain `h11_complete:false` and, on a successful workload,
`performanceAcceptance:NOT_RUN`. Missed offer slots and source-IDR replacements
are reported, not renumbered away. The workload duration and drain tail are
separate. `workloadQtPaintRateHz` counts distinct images actually painted in the
workload interval; warm-up and drain counters remain in the totals. A positive
rate alone is not a fairness or sustained-fps acceptance test. Failure reports
preserve available partial stream counters.

## Production measurement hook

`Viewer::enableMediaMetrics()` explicitly enables GUI-thread, fixed-storage
counters before the first decoded frame. It cannot reset a running collection.
The ordinary app leaves it disabled. The counter hooks follow actual decoder
queue admission/completion, generation invalidation, recovery requests and Qt
painting; no send, focus or input authority is added.
Retired work is classified as an adopted image (`decoded`), a decode error,
an empty decoder result or a discarded completion after generation/lifetime
invalidation; those four counts must reconcile with the retired total.

Each admitted image carries its local receive time into the existing worker
completion. A paint records it once, after QPainter finishes; unrelated widget
repaints do not manufacture frames. Replaced unpainted images are counted.
Percentiles use nearest rank over fixed upper-bound buckets of 1, 2, 4, …,
65536 ms. `null` means no samples or an unbounded overflow bucket, not zero.
The maximum is measured directly. These values measure **local receiveFrame
entry to completed Qt paint**; they exclude transport before admission,
ScreenCaptureKit and physical/compositor display completion. They do not meet
H11's separate capture-to-visible latency boundary.

Use [resource-soak.md](resource-soak.md) for passive per-process samples and
the fixed resource budget report. Use [h11-evidence.md](h11-evidence.md) for the
actual physical input/latency procedure. GPU use, bandwidth, native multi-stream
fairness, full platform/kernel queue coverage and physical H10/H11 remain
separate open checks. Core's configured EventQueue and native-batch processing
limits are documented in [input-queue-budget.md](input-queue-budget.md).
