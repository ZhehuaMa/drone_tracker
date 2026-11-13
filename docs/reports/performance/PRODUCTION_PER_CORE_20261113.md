# Production-board CPU performance with ROS images disabled — 2026-11-13

The current tracker package was measured on every CPU core of `firefly-production`
with **`visualized_result=false` in all six runs**. The controlled target replay
spent 94.5–95.5% of its measured time in Tracking with recording enabled. Its
tracker consumed about 48.01 frames/s. CPU 7 was the busiest application
core, averaging 76.8% busy; CPU 5 averaged 66.3% and CPU 6 averaged 59.4%.

These are measurements of the replay pipeline under its selected CPU policy.
Video decoding replaces physical camera capture. HDMI, the real YOLO and
NanoTrack models, ROS transport, basic diagnostics, and the expanded session
recorder were active. The SC233HGS camera encoder cannot operate in video-file
mode and was explicitly disabled. The real bridge/interceptor services were
replaced by a synthetic typed-telemetry source, so their business computations
are outside this measurement.

## Platform and exact software

- Board: hostname `hzhy`, `HZ-RK3576_WuKong Board`; Ubuntu 22.04.5 ARM64;
  kernel `6.1.141-rt36`; approximately 3.8 GiB RAM, no swap, 57 GiB root eMMC.
- CPU 0–3: Cortex-A53, kernel capacity 442; CPU 4–7: Cortex-A72, capacity 1024.
  All eight were online. Both governors were `performance`. Sampled frequencies
  were 2016 MHz on cores 0–3 and 2208 MHz on cores 4–7.
- Source HEAD: `c47088f01c991d14f3e0b40980956cd3d3736847` plus the uncommitted
  centralized-recorder implementation. All **1,920 retained source entries**
  matched before measurement; `source-comparison.json` records zero differences.
- Executable SHA-256:
  `8fbad8fc5ccce24b20877ea0a68769200febf99ada89f0aafd6875f5b4c2ad37`.
  Package: `drone-tracker-20261112T140616Z-cksaad9e`. It ran from temporary staging;
  the production release was not deployed or replaced.
- System RKNN Runtime baseline: 2.3.2, SHA-256
  `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`;
  RKNPU driver readback: 0.9.8. The system library was unchanged.
- Selected package camera YAML SHA-256:
  `3c2e90046d06f95fd24f0fa76fab8e7587cca64cbc3344561443aa0ee3e377de`.
  CPU policy SHA-256:
  `09928cce2b8a7d03fd87b31dd0bd9b416043599653168feaa7494c7d085443f6`.
  Per-run effective YAML files and hashes are retained; paths, bag enablement,
  and replay-specific settings differ explicitly between cases.

| Logical model file | SHA-256 |
|---|---|
| YOLO, `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| NanoTrack template Backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| NanoTrack search Backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| NanoTrack Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

Model parameters were not tuned to force tracking. YOLO confidence stayed 0.5,
target class 0, interval 4; DCF stayed disabled. Camera intrinsics remain the
existing production placeholders; these results do not establish LOS accuracy.

## Inputs and measurement method

1. **Controlled acceptance replay:** existing `fixed-balloon.avi`, 65 seconds,
   3,900 MJPEG frames, 1920×1200 at nominal 60 Hz. SHA-256
   `c25aec33208f5dfca315eb9c2c4fb86b04d98883ec5d74b68f5ac7746a669a95`.
   A retained decoded frame shows outdoor balloon/drone imagery without rendered
   tracking boxes. Sampled decoded frames differ. Its original acquisition and
   construction chain was not recovered, so it is **not claimed as authenticated
   continuous flight footage or a motion-quality benchmark**.
2. **Real-flight reference:** existing `nov4-round2-first8s.avi`, 480 MJPEG frames,
   1920×1200 at 60 Hz, SHA-256
   `ac506822b26499863d1fc50af1324fbc29afd3fa76c82e67d03ce74a452f9ae4`.
   This is the previously prepared unannotated eight-second derivative of the
   November 4 production recording, run `12-06-28`, session
   `3b279759c297707a6881520d89ce3377`. The original recording inventory is in
   `/home/mzh/nickluo/test-data-2026-11-04/README.md` and `manifest.tsv`.
   Eight supported ON rounds reopened the same clip from its start; no artificial
   detections, bounding boxes, frame-time rewrites, or model changes were injected.

Each controlled run requested 60 active seconds and excluded the first eight
seconds and last 0.3 seconds from steady metrics. Each flight round requested
6.5 seconds and excluded its first 1.5 seconds and last 0.3 seconds. Actual
monotonic windows, rather than requested durations, define every denominator.
The order was controlled disabled A, expanded A, flight disabled, flight
expanded, controlled expanded B, disabled B. Individual process lifetimes were
64.8–67.3 seconds, below 180 seconds.

The selected application policy allowed cores 4–7: control/tracking=4, YOLO=5,
ROS transport/display=6, decoder/session writer=7. The synthetic message fixture
was allowed cores 0–3; the observer was pinned to core 1. This policy is the
current package's policy, not an assertion that the separately restored installed
legacy policy has the same bytes or transport role.

The observer read `/proc/stat` per-core counters every 0.5 seconds. Busy excludes
idle and I/O-wait; guest counters are already included in user/nice and are not
added twice. Means use summed counter deltas. Peak means the highest sampled
half-second interval, not an instantaneous maximum. Thread CPU use, observed
affinity masks and sampled current cores are retained separately. A migrating
thread's complete tick delta is never assigned to its sampled last CPU. Thread
scans are not atomic; their individual observation timestamps are retained. The
per-core counters read at the start of each snapshot determine core totals.

**Measurement overhead is material.** The telemetry fixture used about 100% of
one A53 CPU across cores 0–3. The observer used 31–35% of one A53 CPU, with
individual snapshot scans taking roughly 146–229 ms. Core 0–3 totals therefore
include substantial benchmark work and must not be described as production
application demand. All reported numbers include this instrumentation load.

The busy Python receiver accumulated state-callback delays of up to 7.12 seconds.
State occupancy and Tracking-only CPU windows therefore use **native monotonic
`UpdateEnd` output-state transitions**, not receiver arrival timestamps. Receiver
timestamps and their delayed-state durations remain in `analysis.json` as
instrumentation evidence; they are not production DDS latency measurements.

## CPU usage on every core

Each mean below averages the two controlled-run means for the given recording
setting. P95 is the worse of the two expanded-run 95th percentiles; it is not
a percentile of averaged values. A 95th percentile bounds 95% of the sampled
intervals. Frequencies are sampled reported frequencies.
100% means one fully busy core; the A53 and A72 cores have different capacities.

| Core | Type | Disabled mean | Expanded mean | Expanded p95 | Expanded peak | MHz |
|---:|---|---:|---:|---:|---:|---:|
| 0 | A53 | 31.7% | 31.0% | 38.4% | 43.1% | 2016 |
| 1 | A53 | 51.3% | 53.6% | 60.0% | 64.0% | 2016 |
| 2 | A53 | 30.5% | 30.7% | 39.9% | 45.1% | 2016 |
| 3 | A53 | 31.7% | 30.8% | 38.0% | 41.2% | 2016 |
| 4 | A72 | 55.7% | 55.8% | 63.2% | 68.6% | 2208 |
| 5 | A72 | 64.4% | 66.3% | 73.5% | 74.5% | 2208 |
| 6 | A72 | 23.7% | 59.4% | 65.3% | 67.3% | 2208 |
| 7 | A72 | 57.9% | 76.8% | 80.0% | 82.0% | 2208 |

Expanded-run component means, in percent of each core's time:

| Core | User/nice | System | IRQ/softirq | I/O-wait | Idle |
|---:|---:|---:|---:|---:|---:|
| 0 | 26.62 | 4.32 | 0.11 | 0.31 | 68.64 |
| 1 | 38.61 | 14.92 | 0.05 | 0.12 | 46.30 |
| 2 | 27.63 | 2.97 | 0.11 | 0.32 | 68.97 |
| 3 | 27.45 | 3.05 | 0.33 | 0.21 | 68.95 |
| 4 | 50.86 | 4.94 | 0.02 | 0.00 | 44.18 |
| 5 | 55.09 | 11.17 | 0.00 | 0.00 | 33.74 |
| 6 | 39.81 | 19.53 | 0.04 | 0.81 | 39.81 |
| 7 | 68.20 | 8.56 | 0.00 | 0.01 | 23.23 |

Core 7 carries MJPEG decoding plus the bag writer. Core 6 carries the recording
callback executor, display, middleware and diagnostics. Compared with disabled
recording, their observed busy fractions increased by approximately 18.8 and
35.7 percentage points. Core 5's YOLO worker was also substantial. There is no
evidence here that moving all work to another core would improve performance.

For example, the first expanded run's actual managed thread readbacks were:

| TID | Logged managed role | Observed CPU mask | CPU use |
|---:|---|---|---:|
| 17353 | yolo | 5 | 66.49% |
| 17358 | capture | 7 | 58.68% |
| 17357 | tracking | 4 | 44.78% |
| 17354 | ros_transport | 6 | 27.50% |
| 17356 | recording | 7 | 18.54% |
| 17351 | display | 6 | 17.40% |
| 17337 | control/ros_transport | 4 | 12.29% |
| 17348 | unassigned library/middleware | 6 | 10.64% |

The role-less thread is left unattributed. Native helper/middleware names alone
are not proof of algorithm ownership. Complete thread records and migration
observations are in each run's `per-core.jsonl` and `analysis.json`.

## Throughput, actual state coverage, and latency

Tracking occupancy is time weighted from native output-state transitions.
All cases entered real Tracking with the approved model files. It is not a
claim of correct boxes or continuous target retention against ground truth.

| Run | ROS domain | ON rounds | Steady s | Native Tracking | Consumed / displayed FPS | Node CPU |
|---|---:|---:|---:|---:|---:|---:|
| controlled-disabled-a | 85 | 1 | 51.81 | 100.0% | 48.00 / 46.86 | 201.18% |
| controlled-disabled-b | 182 | 1 | 51.87 | 99.5% | 48.82 / 48.24 | 197.09% |
| controlled-expanded-a | 24 | 1 | 51.85 | 95.5% | 47.81 / 46.35 | 256.50% |
| controlled-expanded-b | 64 | 1 | 51.80 | 94.5% | 48.20 / 46.92 | 254.72% |
| flight-disabled | 125 | 8 | 39.18 | 41.0% | 58.24 / 57.74 | 173.61% |
| flight-expanded | 118 | 8 | 39.20 | 37.6% | 58.44 / 57.67 | 230.46% |

Controlled expanded runs averaged 255.61% node CPU versus
199.14% disabled: an observed +56.48 percentage points
(28.4%). Consumed FPS changed by -0.40
(-0.83%). The exact asynchronous
state mixtures and measured telemetry rates differ, so these deltas are not
an isolated causal estimate of recorder cost at identical algorithm state.

The update values below are **HybridTracker calls ending in Tracking**, including
tracking, recovery and arbitration work. They are not pure NanoTrack/NPU kernel
latency. Detection-job latency pairs native DetectStart/DetectEnd; one job can
perform ROI, full-image and multiple tile model invocations. Basic diagnostics
do not expose a separate pure NanoTrack Backbone/Head timing distribution.

| Run | Tracking calls | Update p50 / p95 / p99, ms | Detection jobs | Job p50 / p95 / p99, ms | Display mean, ms |
|---|---:|---:|---:|---:|---:|
| controlled-disabled-a | 2487 | 10.09 / 60.07 / 121.79 | 345 | 151.4 / 304.5 / 307.4 | 22.89 |
| controlled-disabled-b | 2522 | 9.93 / 52.22 / 116.91 | 350 | 150.7 / 302.2 / 303.8 | 22.49 |
| controlled-expanded-a | 2361 | 10.26 / 63.39 / 124.86 | 338 | 154.8 / 310.6 / 313.0 | 23.22 |
| controlled-expanded-b | 2360 | 10.24 / 54.69 / 123.53 | 329 | 154.9 / 311.3 / 313.1 | 23.41 |
| flight-disabled | 963 | 9.91 / 10.92 / 11.90 | 129 | 293.5 / 303.1 / 304.8 | 22.93 |
| flight-expanded | 889 | 10.24 / 11.31 / 12.81 | 131 | 297.7 / 312.0 / 313.6 | 23.41 |

Display latency runs from submission to display, not exposure to photons.
All six merged display histograms had p95/p99 upper bounds of 32.768 ms.
Their power-of-two histogram bounds are approximate. Display FPS uses complete
OSD timing windows contained inside steady windows; the exact coverage is in
`analysis.json`. Consumed FPS uses consumed calls over the full steady window.

## Other resource and recording results

RSS is resident RAM mapped into the tracker process. eMMC writes are shared
root-filesystem block counters, not per-process attribution or durability proof.
The fixture requested 200 Hz body-IMU input but reached the actual rates below;
these runs do not prove capacity at a sustained 200 Hz offered rate.

| Run | Median RSS MiB | Root writes MiB/s | Diagnostics CPU | Fixture CPU | Observer CPU | Actual body-IMU Hz |
|---|---:|---:|---:|---:|---:|---:|
| controlled-disabled-a | 224.48 | 0.65 | 2.72% | 99.67% | 33.11% | 137.7 |
| controlled-disabled-b | 223.70 | 1.56 | 2.70% | 99.71% | 32.58% | 138.6 |
| controlled-expanded-a | 231.36 | 0.80 | 2.78% | 100.25% | 35.07% | 130.9 |
| controlled-expanded-b | 230.26 | 1.60 | 2.72% | 100.19% | 34.35% | 130.8 |
| flight-disabled | 218.00 | 0.64 | 2.42% | 99.72% | 31.33% | 141.0 |
| flight-expanded | 224.25 | 1.98 | 2.40% | 100.34% | 32.80% | 132.5 |

All ten expanded-session bags closed complete and passed full readback: eight
flight bags plus one bag in each controlled expanded run. Across them,
**103,616 accepted messages were all written**; 49,496 were
intentionally sampled. Queue, middleware, byte-limit, oversized and
accepted-but-unwritten counts were zero. Each session retained ON/OFF and its
final invalid bbox/LOS pair. Disabled runs created no bag.

Each effective ROS parameter query returned Boolean `visualized_result=false`.
The image subscriber saw **zero messages** throughout every run, despite one
advertised image publisher. No bag contained `/fpv/tracker_image`. Thus publisher
metadata was not mistaken for image traffic. HDMI remained enabled at 720×480,
60 Hz throughout; disabling ROS images did not disable HDMI.

All diagnostic exports reported normal completion, zero cumulative gaps for
all five producers, no degradation, and no duplicated events. Measurement
call/submission arrays had zero overflow. Per-core counters had no regressions.
Maximum sampled thermal-zone temperature was 60.08 °C.
Reported CPU frequencies stayed at their maxima; exported NPU/GPU cooling states
stayed zero. Separate CPU thermal-throttling counters were not exposed, so these
samples are not a proof that no unobserved throttling occurred.

## Reproduction, retained evidence, and boundaries

Retained evidence root:
`/home/mzh/nickluo/validation-retained/per-core-performance-20261113/`.
It contains both input clips, exact benchmark/observer/analyzer scripts, raw
per-core/thread snapshots, effective YAML files, logs, native diagnostic exports,
SQLite bags, readbacks, identities, and independent accounting checks. It is
outside disposable build caches. Board staging was `/var/lib/drone_tracker/cpu-pf13`.

The commands below record the historical run and its exit statuses. Output
directories are created exclusively (`exist_ok=False`). To repeat the experiment,
copy and adjust the scripts to use fresh host and board output roots; do not run
over or overwrite the retained evidence directories.

Commands, run in this order, with actual exit statuses:

```bash
bash /home/mzh/nickluo/validation-retained/per-core-performance-20261113/run-matrix.sh
# exit 0; all six workload runs, bag readbacks and diagnostic exports exited 0
/home/mzh/miniconda3/bin/python /home/mzh/nickluo/validation-retained/per-core-performance-20261113/analyze.py \
  /home/mzh/nickluo/validation-retained/per-core-performance-20261113/board
# exit 0
/home/mzh/miniconda3/bin/python /home/mzh/nickluo/validation-retained/per-core-performance-20261113/validate_and_report.py
# exit 0; 734 identity/accounting/resource checks passed
```

The board commands source `/opt/ros/humble/setup.bash`. The board uses system
Python for its matching ROS bindings; host analysis uses conda base Python.
Each run generated a fresh random ROS domain and localhost-only discovery.
The exact domain IDs are listed above. No new algorithm build was needed.
Before replay, device topology, permissions, OS, Runtime, driver and deployed
state were rechecked; this does not constitute a fresh camera-capture test.

The first qualification attempt failed before tracker launch because that
board's rclpy lacks `parameter_client`. The instrumentation was changed to use
the generated GetParameters service directly. `pilot-import-failure.log` retains
the failure. Both real-model qualification runs then passed. There was no
tracker, model, diagnostic-setting or production-config workaround.

All four originally active services were restored: tracker, diagnostics, bridge
and interceptor. Their prior/after states match; Runtime/camera/installed-policy
checksums match; no temporary benchmark processes remain. No commit, archive,
deployment, rollback, kernel setting or CPU-policy change was performed.

This is a replay performance measurement with synthetic telemetry. It does not
establish physical-camera/encoder performance, full flight-stack CPU demand,
target accuracy, calibrated LOS accuracy, device-memory usage, power-loss
durability, maximum offered-rate capacity, or Jetson behavior. The declared
windowing, nontrivial fixture/observer cost and input-provenance limits apply
to every result above.
