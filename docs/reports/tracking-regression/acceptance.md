# RK3576 tracking regression integration record

Date: 2026-10-13. Change: `add-rk3576-tracking-regression-comparison`.

Implementation is complete: **36/36 tasks** have evidence. Full predecode passed production-board qualification. Three independent reference runs were explicitly frozen, followed by three qualified candidate repeats. The automatic Chinese Markdown, JSON and CSV reports contain 210 metric rows. The comparison is comparable; no performance decision rules were set.

The two cohorts use the **same measured program build**. This acceptance validates independent runs, input timing, evidence, baseline freezing and automatic comparison. Differences describe repeatability and asynchronous scheduling variation. They do not establish an algorithm optimization, target accuracy or real-flight acceptance. The acceptance phase did not commit or archive. Subsequent independent OpenSpec verification found no issues. The change is now synchronized and archived at `openspec/changes/archive/2026-10-13-add-rk3576-tracking-regression-comparison`; its verification report is retained there.

## Approved input method and software verification

The user authorized removing the 256 MiB input-cache ceiling and caching every frame before measured playback. Configuration version 2 fixes `input_mode=predecode_all` and rejects `prefetch_frames`. There is no substitute fixed memory ceiling. Exact pixels, original timestamps, the 50 ms supply-lateness setting, and the 110/120-second total controls remain fixed.

Each attempt verifies immutable files and timing/attitude mapping, then decodes the FFV1 clip once. The native source checks every actual retained Gray8 frame against its SHA-256 before model construction. All 1,200 cached frames occupy exactly 2,764,800,000 bytes (about 2.575 GiB). Decoder resources are released before playback. Models become ready before establishing the 20-second clock. The source supplies independent pixel copies at the original timestamps, while the production consumer processes the latest frame. Preparation failures cannot establish a valid clock or qualify.

The ARM64 SDK revision passed **56/56 CTest targets**, including **52 Linux Python tests**, with no skips. A subsequent workload-identity correction passed all **4/4 regression targets** and **53 Linux Python tests**, with no skips (42.67 seconds). Tests distinguish full readiness, actual cached-pixel errors, controlled allocation failure, missing preparation, early stop, slow consumers, queue rejection, EOF and deadlines. Existing node isolation and pre-model refusal tests passed. Logs are in `predecode-revision/` and `predecode-board-20261013/` under `.docker-artifacts/tracking-regression/`. The pinned image does not provide configured clang-tidy/clangd checks.

The workload correction identifies kernel threads using the actual `PF_KTHREAD` flag in `/proc/PID/stat`. Dynamic kernel-worker names remain observations. Userspace process names and formal service states remain comparison conditions. Only controller ancestry and known private test scripts/executables are excluded; unrelated Python and shell workloads remain visible. Tests accept renamed flagged kernel workers and refuse added userspace workload, including a userspace process named like a kernel worker. This is a process-name check, not exhaustive CPU/I/O profiling.

## Measured build, fixture and platform

Worktree: `tracking-regression-comparison`, branch `codex/add-rk3576-tracking-regression-comparison`. The measured implementation included the authorized import of exact dependency commit `e555f7a335660b6efd89bd5c4f757522834a6b03`. That import is included in the regression integration commit. The primary checkout's unrelated local changes were not imported.

| Measured identity | SHA-256 |
|---|---|
| Source export (393 files) | `fb8d0049e7e4b617ed696ba4a67b2870f7cc478235a02b3a60b7602310e76290` |
| Regression executable | `9bb08bd6c63840a0235cad31e96a08f1528911b49cf3de3e77491098f86fafa3` |
| Diagnostic worker | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` |
| Delivery archive | `93da08832da3735cc287c0b7db6b9906caed0edab23eb812fb4f59ef556646c1` |
| Frozen baseline file | `b0fc47621305711cecfb49eedcbaca4de6cea5efbfaf5470e3c1e3cc7af99ee9` |

The measured source and install are preserved under `.docker-artifacts/tracking-regression/predecode-board-final-20261013/`. The exported source and delivery archive remain immutable after acceptance documentation and task updates in the working checkout. Every run identifies its exact source files, executable and resolved external dependencies. It resolves 249 node libraries, five worker libraries and the effective ROS middleware content under the actual environment. These identity checks cover key dependencies, not every optional plugin.

The immutable fixture is `wlcb-1513-loss-recovery`: the original `[55,75)` second interval, exactly 1,200 Gray8 1920×1200 frames, original IDs 3302–4501. The first offset is 15,721 µs and the last is 19,992,663 µs. Source-derived luminance, attitude and effective algorithm/model identity were preserved. The FFV1 clip SHA-256 is `c46e74b63309e76f552f32bd39bca8e3389477af7d691c88bd842d5ae75f21bd`; its fixture manifest is `1a00dd489d6d4062ebc080ac48514c2f40200e09b386ff98c9a07b2043c704a3`. Models use the recorded balloon YOLO and FP16 template/search backbones and head, relocated through content-checked paths under `/opt/drone_tracker/models`.

All board access used **adb**, serial `5ceb49407502303d`. The target was the same `HZ-RK3576_WuKong Board`, machine ID `1fe0a397f2ae4343896c7cf9d253a608`, hostname `hzhy`, Ubuntu 22.04 ARM64, kernel `6.1.141-rt36`. OS-visible memory was 8,112,044 KiB (about 7.74 GiB). The system RKNN Runtime was 2.3.2 and RKNPU driver was 0.9.8. System Runtime SHA-256 remained `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.

Cores 0–3 are Cortex-A53 and 4–7 are Cortex-A72. The adb shell's inherited A53 restriction was explicitly replaced for the controller with `taskset -c 0-7`; `DRONE_TRACKER_CPU_CONFIG` was unset. All sampled node/decoder thread observations across the six runs used mask `0-7`. Both governors remained `performance`. OSD, images and recording were disabled; box publication and basic diagnostics were enabled. The formal tracker was temporarily stopped. The APM bridge, interceptor and camera engine remained active, together with the recorded background services. The workload description “isolated test only” means there was no second tracker, not an empty operating system.

## Qualified reference and candidate evidence

Three reference and three candidate attempts were declared before execution. Every attempt in the corrected comparison group qualified; no slow qualified run was removed. Each prepared and verified all 1,200 frames, supplied and enqueued all 1,200, reached normal EOF, retained healthy complete diagnostic records, and recovered its test descendants. Input never slowed to match tracker processing.

| Role / run | Predecode | Total attempt | Max supply lateness | Selected / skipped |
|---|---:|---:|---:|---:|
| reference / run-001 | 48.240 s | 84.772 s | 18.679 ms | 1143 / 57 |
| reference / run-002 | 48.172 s | 84.616 s | 7.790 ms | 1099 / 101 |
| reference / run-003 | 48.139 s | 84.564 s | 5.278 ms | 1147 / 53 |
| candidate / run-001 | 48.878 s | 85.374 s | 4.635 ms | 1171 / 29 |
| candidate / run-002 | 48.231 s | 84.454 s | 4.925 ms | 1145 / 55 |
| candidate / run-003 | 48.270 s | 84.663 s | 5.905 ms | 1126 / 74 |

All six preparations retained 2,764,800,000 pixel bytes. Preparation resident memory was approximately 2.66 GiB, with approximately 4.68 GiB OS-available memory after readiness; models and later queue copies consume additional memory. `preparation.json` records each exact memory observation. Preparation and model loading are separate from tracker update duration, while their time still counts toward the 120-second outer limit. None of these attempts reached the 110-second stop point.

The 4,730 sampled node/decoder thread observations all showed mask `0-7`. Every two-window metrics file was exactly recomputed from retained binary events and original input records, after normalizing internal tuples to their JSON list representation. Qualification checks were rerun, and every file hash required by the freeze loader passed. Counts distinguish selected and skipped frames; consumer skips are expected production behavior, not missing input.

Each run records absolute diagnostic submission cost. Sample counts were 11,452–12,110; p95 was 8.013–8.371 µs. This includes clock sampling and notification. It is not an off/basic paired total-overhead result, and it does not complete the imported diagnostic change's separate overhead acceptance. Task 6.2 requires recording observation overhead, not passing that other change's threshold.

The explicit baseline freeze included all three reference runs. `freeze-record.json` identifies the action and file hash. Candidate generation and comparison left that baseline unchanged. Reports are:

- Board: `/home/hzhy/tracking-pd2-20261013/comparison/`.
- Local: `.docker-artifacts/tracking-regression/predecode-board-final-20261013/board-evidence/comparison/`.
- Outputs: `report.md`, `comparison.json` and `comparison.csv`.

They show each run, median/range, candidate-minus-reference differences, percentage changes, percentage points and unavailable/no-event states. The decision is explicitly “未设置判定规则” (no decision rules set). For example, the core-window valid-publication ratio median differs from 0.810811 to 0.959459 even though the build is identical. This illustrates run-to-run variation; it is not evidence of code improvement. No target truth was annotated.

## Configuration refusal and immutable originals

A separate `fault-injection/` directory contains **synthetic comparator test copies**, excluded from both measured cohorts and the baseline. No changed-configuration hardware run occurred. All three copied candidate records deliberately changed `comparison_group.test_driver.configuration.queue_capacity` from 32 to 33. Their `identity.json`, `identity-preflight.json` and `run-config.json` were changed; only those file hashes in copied `completion.json` were recalculated. `mutation-record.json` records every change and all original hashes.

The comparator returned `incomparable` and named the exact configuration leaf for all three copied records. It produced no performance verdict. Original candidate files and frozen baseline hashes remained unchanged before and after this fault injection. The derived copies must never be frozen or treated as additional independent measurements.

## Restoration and retained attempts

Before each service stop, an independent systemd restoration timer was armed and verified active. Each isolated attempt also had an exit restoration trap. `restore-reference-001.json` through `restore-candidate-003.json` all confirm the original service states and no cleanup errors. `final-protected-state.json` confirms:

- Formal `drone-tracker.service` is active; diagnostics are inactive, matching original states.
- No isolated test node, worker, controller or attempt process remains; all six restoration timers are inactive.
- `/opt/drone_tracker/current` still resolves to `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`.
- Camera and CPU configuration hashes remain `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c` and `4f46293201355131c1e72c8a72cdfaafa2c3121eaa48fe0b5ad69ac442e4f01b`.
- Runtime content, machine identity and kernel remain unchanged. Formal deployment, system libraries, governors and persistent service configuration were not changed.

Before the workload fix, the first predecode group ran two attempts. Its first supplied all 1,200 frames but failed qualification because one frame was 55.107 ms late, above the unchanged 50 ms budget. Its second qualified. Neither entered the corrected baseline: the planned third reference and candidates were cancelled because the workload identity needed repair, not because of their speed. Both attempts and their source/build identities are preserved under `predecode-board-20261013/`; `initial-cohort-status.json` records the cancellation.

The earlier five streaming failures remain unchanged under `board-evidence/`, with the original 132-file manifest SHA-256 `3d6265e07add0a10e29fe19e4fd13f999206506fb6747a387e4de32f67c62c5f`. The separate earlier repair manifest remains `2fb1477c7ab5f7877b27bc1c57314accee3cac675cd385b0b0da15db1db3e2cb`. The earlier all-core streaming attempt supplied 519/1,200 frames and is not reclassified as predecode acceptance. Its full historical acceptance document is retained as `predecode-board-final-20261013/acceptance-before-final.md`.

## Evidence limits

Large recordings, models, exports and raw measurements remain outside Git in ignored artifact directories. The board evidence root is `/home/hzhy/tracking-pd2-20261013`; the local copy and its manifest are under `predecode-board-final-20261013/`. Exact measured sources/binaries are preserved separately from later delivery-document edits.

The checks establish this regression tool's acceptance on the specified board and fixture. They do not establish an optimization benefit, annotated tracking accuracy, real-flight behavior, full camera/HDMI/recording workload acceptance, Jetson compatibility or the imported diagnostic change's separate off/basic overhead threshold. No applicable production-board task remains incomplete in this change; independent verification will assess the delivered implementation and evidence.

## Missing context and unchanged watchdog

A delayed positive result with a source tracker observation can confirm agreement even
when its pixels have left the bounded image history. Recovery still requires the source
image. Missing comparison context leaves the unconfirmed-result counter unchanged.
A genuine low-overlap result still increments it. Repeated jumps cannot claim successful
acceptance when recovery pixels are unavailable. A prediction without a tracker observation
cannot establish overlap agreement.

The existing watchdog resets whenever any result is read during Tracking, including an
uncomparable positive result and a completed no-target result. With defaults, 60 no-result
updates do not time out; 61 do. Timeout clears initialization/boxes, resets the counter,
and resumes Detecting on the next frame without adding TargetLost. Detecting has no
watchdog. Faster updates shorten this timeout in seconds. Repeated uncomparable results
can prevent timeout. There is no age cutoff and no freshness guarantee. The fixed footage
has no target annotations, so output continuity cannot establish tracking accuracy.

## Affinity experiment acceptance

Before timed playback, freeze each build identity, common observation patch, compiler/options, dependencies,
input/model hashes, planned run order and exact treatment manifest. Only CPU assignment is the treatment.
Audit each differing driver source against the preserved source patch. Extra tool, metric, budget, queue,
algorithm or input differences make the experiment incomparable. Every side retains its strict three-run check.

For each test-03 clip, report every planned round, qualified/failed status and actual CPU evidence.
Report medians, ranges, candidate-minus-reference differences and percentages without dropping slow or zero-box rounds.
Report update/publication intervals, YOLO and queue timing, recovery count/steps/per-step costs,
valid outputs/clears, supplier/queue conservation, process/system memory and observed temperatures/frequencies.
Keep unknown library threads explicitly unknown. Without a frozen performance threshold, state
“未设置判定规则”; without labelled targets, make no accuracy claim.

The independent restoration guard must be armed before stopping the authorized production service.
Final evidence must verify the original active/running/enabled service state, release link, production config,
models, system Runtime and governors, and absence of test processes. Hardware acceptance uses only
`adb -s 5ceb49407502303d`; host/QEMU checks establish software behavior, not board timing.
