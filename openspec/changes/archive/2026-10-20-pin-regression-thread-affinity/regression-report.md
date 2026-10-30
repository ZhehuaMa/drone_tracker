# Regression-thread CPU affinity: final eighteen-run report

All eighteen fresh planned runs qualified. Each of three test-03 intervals has three unbound references and three pinned candidates. Candidate managed masks and work-point CPUs match the requested mapping. No thresholds were set, so this establishes neither a performance pass nor an accuracy conclusion.

Sequential work 2 performed these measurements. A fresh work-3 agent independently verified them with initial/final CRITICAL 0, WARNING 0, SUGGESTION 0 and no code fixes. Work 4 reused all eighteen runs under user authorization. See [verification-1.md](verification-1.md) and the later-workflow section of [final-validation.md](final-validation.md).

CPU affinity is the permitted set of cores, expressed by a mask. Work-point CPU is the core actually executing a call. PID identifies a process and TID a Linux thread. Check permitted placement, actual execution, and process/thread identity separately.

## Frozen conditions and actual platform

- Board access only through `adb -s 5ceb49407502303d`, without SSH. Board `HZ-RK3576_WuKong Board`, kernel `6.1.141-rt36`, visible memory 8112044 KiB (about 7.74 GiB). This identifies the selected board without rewriting other production-board specifications.
- CPUs 0–3 are Cortex-A53 and 4–7 Cortex-A72; isolation set is 4–7. Only test-thread allowed masks changed. Priority, governors, NPU scheduling, and system isolation stayed unchanged.
- System `/usr/lib/librknnrt.so` Runtime 2.3.2, driver 0.9.8. No foreground `rknn_server`, system-library replacement, or model replacement.
- Both sides use `predecode_all`, decoding/verifying all pixels first. Supplier is CPU 4, preparation 3600 seconds, cleanup 10 seconds, supply-lateness limit 50000 microseconds, queue 32. No warmup skipping, zero-box filtering, or relaxed limits.
- Each interval runs serially as reference 1, candidate 1, reference 2, candidate 2, reference 3, candidate 3. Each run starts a fresh process/state. test-02 is explicitly user-skipped.
- Board calendar time lagged the host; raw records were preserved. Each run maps monotonic origin m0 to system origin e0, retaining original frame spacing/identity. Absolute times use that mapping; order/latency use monotonic time. System time was unchanged.

System Runtime SHA-256: `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.

The three logical roles are YOLO, NanoTrackV3 Backbone, and Head, expressed through four runtime parameters. Actual model contents:

| Runtime parameter | SHA-256 |
|---|---|
| `tracker.yolo_engine` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| `tracker.nano_template_backbone` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `tracker.nano_search_backbone` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `tracker.nano_head` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

Original media, attitude, and algorithm settings were reused read-only. Frozen inputs contain 900 frames for `[20,35)`, 1801 for `[100,130)`, and 2575 for `[165,end)`, including the final frame. Fixture integrity, model content, external dependencies, workload, and observation mode belong to identity checks.

## Source, build and tool identities

Algorithm commit is `4871442dda37d3d64087fda0b7756692c8f7e473`. Label reference “4871442 algorithm baseline + common observation patch + common source-support fix.” It retains unbound startup; candidate adds role binding and early supplier readiness. Executables are distinct ARM64 builds.

| Side | Source SHA-256 | Executable SHA-256 |
|---|---|---|
| Unbound reference | `080cf382dc2b8306c8e75c14b8c21807968a69551bd9fc62f3d7848faaebb37a` | `dcba1fdc854fad8102cf930149d8bc5b19ec2f2542cdb8f2b3422d041f548799` |
| Pinned candidate | `198c42f7a0173f3a980b9a54fa842c29f600ec7048b24a686087c46ad656186d` | `9810ee537e7120baafbba94b69c7f1414c4d6f2e705337f68820cefda72c1251` |

Common supplier-support patch SHA-256: `7073f8d077d6a56d4201fdbc1f80b6a0697cbcc4c95aca7203024269e2e52389`.
Common observation-lifecycle correction SHA-256: `6112ad40a98afe5447c959a6007a39b13014a4821a33f719c53a6db9c2410c29`.

Reconstruct reference by applying `reference-observation-and-tools-final.patch`, then `common-source-support.patch`, to the fixed commit. Candidate uses `candidate-from-base-final.patch`. Full reference patch `reference-from-base-final.patch` is also retained. Reference `source_patch_sha256` identifies observation/tools; the frozen plan records common-support hash separately. Both reconstruct actual source identity.

Common support makes real HostFrame retain verified read-only Gray8 ownership, removing full-image allocation/copy during timed supply. Gray8 is one eight-bit grayscale byte per pixel. The complete cache remains until source stop; frames/read views survive source cleanup. Production deep-copy interfaces/callers retain independent copies. Both builds use the identical fix.

Build image is default amd64 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`, GCC 11.4.0 cross-compiling ARM64, CMake 3.22.1, Release. Test/HDMI/recording options match. Compiler, flags, and preprocessor definitions match for all 84 C++ targets. No backend switch occurred. Project code links statically into the node. Actual external dynamic dependencies and the empty independent project-shared-library set remain in identity. Each side also has 24 library-artifact hashes in `project-library-artifacts.json`.

Before stopping services and before/after each run, the controller checks 15 actually loaded regression modules: absolute origins, hashes, and lazily imported `cpu_evidence.py`. It does not import old `preflight.py`, which mutates `sys.path`. Controller, status helper, restoration/environment scripts, original configuration, and plan are frozen/rechecked. Raw proof is `board/control/loaded-tool-audit.jsonl`.

Ordinary comparison still rejects CPU-policy differences. The three explicit treatment manifests permit only exact policy leaves and three reviewed driver-source differences. Three runs per side retain strict consistency/integrity checks, and both original comparison groups remain complete.

## Raw per-run summary

p95 is each run’s 95th percentile; the next section aggregates the three p95 values by median/range. MiB is 1024×1024 bytes; RSS is resident physical memory. Catch-up steps replay tracking after an earlier detection. No steps means no duration event, not zero milliseconds. Every run, zero-box result, and slow value is retained.

| Interval | Side/run | Updates/detections | Publications/valid | Recovery attempts/steps | Update p95 ms | YOLO p95 ms | Request queue p95 ms | Maximum supply lateness ms | Node peak MiB |
|---|---|---|---|---|---|---|---|---|---|
| wlcb-104206-020-035 | ref/1 | 900/33 | 900/0 | 0/0 | 0.031235 | 462.303 | 14.2563 | 0.073704 | 2194.92 |
| wlcb-104206-020-035 | cand/1 | 900/62 | 900/0 | 0/0 | 0.019044 | 247.487 | 15.7637 | 0.129368 | 2187.39 |
| wlcb-104206-020-035 | ref/2 | 900/33 | 900/0 | 0/0 | 0.030188 | 464.292 | 16.3567 | 0.141459 | 2194.8 |
| wlcb-104206-020-035 | cand/2 | 900/62 | 900/0 | 0/0 | 0.018239 | 246.103 | 15.216 | 0.223935 | 2187.4 |
| wlcb-104206-020-035 | ref/3 | 900/33 | 900/0 | 0/0 | 0.030392 | 463.642 | 15.8002 | 0.073038 | 2194.71 |
| wlcb-104206-020-035 | cand/3 | 900/62 | 900/0 | 0/0 | 0.018699 | 246.609 | 15.3337 | 0.123788 | 2187.35 |
| wlcb-104206-100-130 | ref/1 | 1801/66 | 1801/0 | 0/0 | 0.032117 | 463.219 | 15.683 | 0.118687 | 4176.02 |
| wlcb-104206-100-130 | cand/1 | 1801/123 | 1801/0 | 0/0 | 0.019044 | 245.932 | 16.1342 | 0.122751 | 4175.7 |
| wlcb-104206-100-130 | ref/2 | 1801/66 | 1801/0 | 0/0 | 0.03319 | 462.39 | 16.2749 | 0.136812 | 4175.89 |
| wlcb-104206-100-130 | cand/2 | 1801/124 | 1801/0 | 0/0 | 0.018952 | 245.297 | 15.0406 | 0.150338 | 4175.7 |
| wlcb-104206-100-130 | ref/3 | 1801/66 | 1801/0 | 0/0 | 0.031112 | 463.038 | 15.1341 | 0.140168 | 4176.09 |
| wlcb-104206-100-130 | cand/3 | 1801/124 | 1801/0 | 0/0 | 0.018837 | 245.324 | 15.7222 | 0.148366 | 4175.7 |
| wlcb-104206-165-end | ref/1 | 2516/95 | 2515/199 | 3/60 | 15.5723 | 539.552 | 16.4653 | 0.088594 | 5862.14 |
| wlcb-104206-165-end | cand/1 | 2549/179 | 2549/410 | 6/51 | 8.50782 | 280.116 | 42.0519 | 0.224013 | 5888.89 |
| wlcb-104206-165-end | ref/2 | 2545/94 | 2545/140 | 2/30 | 15.1913 | 528.713 | 15.9608 | 0.087476 | 5871.86 |
| wlcb-104206-165-end | cand/2 | 2572/180 | 2572/319 | 2/10 | 8.32472 | 292.927 | 32.8434 | 0.242435 | 5872.95 |
| wlcb-104206-165-end | ref/3 | 2551/93 | 2550/68 | 1/24 | 0.037721 | 464.395 | 16.1421 | 0.086181 | 5871.57 |
| wlcb-104206-165-end | cand/3 | 2572/183 | 2572/310 | 3/15 | 8.30874 | 287.015 | 32.5224 | 0.199701 | 5872.99 |

No supplied frame exceeded 50 milliseconds of lateness. All processes exited normally and resources were reclaimed. `input_late_count` counts any positive delay, not only violations; limits are checked separately from planned/actual supply times. Per-run queue rejection, residual input, skipped frames, and clearing remain in `analysis/per-run-summary.csv` without statistical removal.

## Three-run comparison by interval

Differences are candidate minus reference; percentages divide by reference median. Brackets show each side’s three-run minimum/maximum. A new event over zero baseline gives null percentage. If any run has no event, that duration is not aggregated across runs. Intervals are neither pooled nor used to offset each other. Celsius temperature has only an absolute difference; its relative percentage is null.

### wlcb-104206-020-035

All six interval runs are fresh qualified results. Complete windows/metrics: [JSON](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-020-035/comparison.json)、[CSV](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-020-035/comparison.csv)、[Chinese details](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-020-035/report.md).

| Metric/unit | Three reference runs | Three candidate runs | Reference median [range] | Candidate median [range] | Difference | Percentage |
|---|---|---|---|---|---|---|
| `full.update_duration_ms.p95`/ms | 0.031235; 0.030188; 0.030392 | 0.019044; 0.018239; 0.018699 | 0.030392 [0.030188, 0.031235] | 0.018699 [0.018239, 0.019044] | -0.011693 | -38.4739% |
| `full.detector_duration_ms.p95`/ms | 462.303; 464.292; 463.642 | 247.487; 246.103; 246.609 | 463.642 [462.303, 464.292] | 246.609 [246.103, 247.487] | -217.033 | -46.8105% |
| `full.request_queue_wait_ms.p95`/ms | 14.2563; 16.3567; 15.8002 | 15.7637; 15.216; 15.3337 | 15.8002 [14.2563, 16.3567] | 15.3337 [15.216, 15.7637] | -0.466535 | -2.95271% |
| `full.source_capture_to_consumption_ms.p95`/ms | 483.332; 483.326; 483.324 | 266.71; 266.707; 266.707 | 483.326 [483.324, 483.332] | 266.707 [266.707, 266.71] | -216.618 | -44.8183% |
| `full.catchup_attempt_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.catchup_step_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.catchup_duration_ms.p95`/ms | null; null; null | null; null; null | null | null | null | null |
| `full.catchup_step_duration_ms.p95`/ms | null; null; null | null; null; null | null | null | null | null |
| `full.recovery_update_duration_ms.p95`/ms | null; null; null | null; null; null | null | null | null | null |
| `full.recovered_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.unrecovered_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.publication_count`/count | 900; 900; 900 | 900; 900; 900 | 900 [900, 900] | 900 [900, 900] | 0 | 0% |
| `full.valid_publication_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.clear_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.max_no_publication_gap_ms`/ms | 17.5641; 17.7165; 17.567 | 16.7928; 16.7961; 16.7815 | 17.567 [17.5641, 17.7165] | 16.7928 [16.7815, 16.7961] | -0.774156 | -4.40689% |
| `full.input_rejected_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.input_residual_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.frame_skipped_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.input_supply_lateness_ms.max`/ms | 0.073704; 0.141459; 0.073038 | 0.129368; 0.223935; 0.123788 | 0.073704 [0.073038, 0.141459] | 0.129368 [0.123788, 0.223935] | 0.055664 | 75.5237% |
| `preparation_ms`/ms | 38007; 38005.5; 37861.1 | 37863.1; 37864.3; 37810.4 | 38005.5 [37861.1, 38007] | 37863.1 [37810.4, 37864.3] | -142.4 | -0.374683% |
| `node_peak_mib`/MiB | 2194.92; 2194.8; 2194.71 | 2187.39; 2187.4; 2187.35 | 2194.8 [2194.71, 2194.92] | 2187.39 [2187.35, 2187.4] | -7.40625 | -0.337445% |
| `logger_peak_mib`/MiB | 3.13281; 3.13281; 3.13281 | 3.13281; 3.11719; 3.13281 | 3.13281 [3.13281, 3.13281] | 3.13281 [3.11719, 3.13281] | 0 | 0% |
| `minimum_available_mib`/MiB | 5492.82; 5493.23; 5492.11 | 5500.74; 5500.2; 5500.77 | 5492.82 [5492.11, 5493.23] | 5500.74 [5500.2, 5500.77] | 7.92578 | 0.144294% |
| `cpu_point_count`/count | 10659; 10659; 10659 | 12138; 12138; 12138 | 10659 [10659, 10659] | 12138 [12138, 12138] | 1479 | 13.8756% |
| `cpu_point_cost_p50_ns`/ns | 99; 99; 99 | 69; 69; 69 | 99 [99, 99] | 69 [69, 69] | -30 | -30.303% |
| `cpu_point_cost_p99_ns`/ns | 1101; 1267; 1152 | 828; 897; 874 | 1152 [1101, 1267] | 874 [828, 897] | -278 | -24.1319% |
| `cpu_point_cost_max_ns`/ns | 31802; 12712; 1587 | 7567; 1242; 1817 | 12712 [1587, 31802] | 1817 [1242, 7567] | -10895 | -85.7064% |
| `submission_cost_p99_us`/us | 8.947; 8.946; 8.763 | 5.635; 5.911; 6.21 | 8.946 [8.763, 8.947] | 5.911 [5.635, 6.21] | -3.035 | -33.9258% |
| `unknown_timed_threads`/count | 18; 18; 18 | 11; 11; 11 | 18 [18, 18] | 11 [11, 11] | -7 | -38.8889% |
| `unknown_model_ready_threads`/count | 11; 11; 11 | 11; 11; 11 | 11 [11, 11] | 11 [11, 11] | 0 | 0% |
| `exited_during_read_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `temperature_max_c`/C | 46.23; 48.076; 48.076 | 49.922; 49.922; 49.922 | 48.076 [46.23, 48.076] | 49.922 [49.922, 49.922] | 1.846 | null |

### wlcb-104206-100-130

All six interval runs are fresh qualified results. Complete windows/metrics: [JSON](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-100-130/comparison.json)、[CSV](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-100-130/comparison.csv)、[Chinese details](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-100-130/report.md).

| Metric/unit | Three reference runs | Three candidate runs | Reference median [range] | Candidate median [range] | Difference | Percentage |
|---|---|---|---|---|---|---|
| `full.update_duration_ms.p95`/ms | 0.032117; 0.03319; 0.031112 | 0.019044; 0.018952; 0.018837 | 0.032117 [0.031112, 0.03319] | 0.018952 [0.018837, 0.019044] | -0.013165 | -40.9908% |
| `full.detector_duration_ms.p95`/ms | 463.219; 462.39; 463.038 | 245.932; 245.297; 245.324 | 463.038 [462.39, 463.219] | 245.324 [245.297, 245.932] | -217.714 | -47.0185% |
| `full.request_queue_wait_ms.p95`/ms | 15.683; 16.2749; 15.1341 | 16.1342; 15.0406; 15.7222 | 15.683 [15.1341, 16.2749] | 15.7222 [15.0406, 16.1342] | 0.03918 | 0.249825% |
| `full.source_capture_to_consumption_ms.p95`/ms | 483.328; 483.335; 483.339 | 266.706; 266.709; 266.705 | 483.335 [483.328, 483.339] | 266.706 [266.705, 266.709] | -216.629 | -44.8196% |
| `full.catchup_attempt_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.catchup_step_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.catchup_duration_ms.p95`/ms | null; null; null | null; null; null | null | null | null | null |
| `full.catchup_step_duration_ms.p95`/ms | null; null; null | null; null; null | null | null | null | null |
| `full.recovery_update_duration_ms.p95`/ms | null; null; null | null; null; null | null | null | null | null |
| `full.recovered_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.unrecovered_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.publication_count`/count | 1801; 1801; 1801 | 1801; 1801; 1801 | 1801 [1801, 1801] | 1801 [1801, 1801] | 0 | 0% |
| `full.valid_publication_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.clear_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.max_no_publication_gap_ms`/ms | 17.9375; 17.4329; 17.9457 | 16.8151; 16.82; 16.801 | 17.9375 [17.4329, 17.9457] | 16.8151 [16.801, 16.82] | -1.12237 | -6.25711% |
| `full.input_rejected_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.input_residual_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.frame_skipped_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.input_supply_lateness_ms.max`/ms | 0.118687; 0.136812; 0.140168 | 0.122751; 0.150338; 0.148366 | 0.136812 [0.118687, 0.140168] | 0.148366 [0.122751, 0.150338] | 0.011554 | 8.44517% |
| `preparation_ms`/ms | 74537.4; 74367.9; 74209.2 | 74292.8; 74314.4; 74159 | 74367.9 [74209.2, 74537.4] | 74292.8 [74159, 74314.4] | -75.1 | -0.100984% |
| `node_peak_mib`/MiB | 4176.02; 4175.89; 4176.09 | 4175.7; 4175.7; 4175.7 | 4176.02 [4175.89, 4176.09] | 4175.7 [4175.7, 4175.7] | -0.3125 | -0.00748321% |
| `logger_peak_mib`/MiB | 3.61719; 3.44141; 3.44531 | 4.20312; 3.44922; 3.44922 | 3.44531 [3.44141, 3.61719] | 3.44922 [3.44922, 4.20312] | 0.00390625 | 0.113379% |
| `minimum_available_mib`/MiB | 3503.96; 3503.41; 3503.56 | 3505.18; 3503.21; 3505.12 | 3503.56 [3503.41, 3503.96] | 3505.12 [3503.21, 3505.18] | 1.55859 | 0.044486% |
| `cpu_point_count`/count | 21352; 21352; 21352 | 24259; 24310; 24310 | 21352 [21352, 21352] | 24310 [24259, 24310] | 2958 | 13.8535% |
| `cpu_point_cost_p50_ns`/ns | 121; 99; 99 | 69; 69; 69 | 99 [99, 121] | 69 [69, 69] | -30 | -30.303% |
| `cpu_point_cost_p99_ns`/ns | 1171; 1074; 1197 | 943; 966; 943 | 1171 [1074, 1197] | 943 [943, 966] | -228 | -19.4705% |
| `cpu_point_cost_max_ns`/ns | 29661; 52601; 27843 | 21850; 18354; 10304 | 29661 [27843, 52601] | 18354 [10304, 21850] | -11307 | -38.1208% |
| `submission_cost_p99_us`/us | 8.571; 9.051; 9.145 | 5.888; 5.819; 5.842 | 9.051 [8.571, 9.145] | 5.842 [5.819, 5.888] | -3.209 | -35.4546% |
| `unknown_timed_threads`/count | 18; 18; 18 | 11; 11; 11 | 18 [18, 18] | 11 [11, 11] | -7 | -38.8889% |
| `unknown_model_ready_threads`/count | 11; 11; 11 | 11; 11; 11 | 11 [11, 11] | 11 [11, 11] | 0 | 0% |
| `exited_during_read_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `temperature_max_c`/C | 48.076; 48.076; 48.076 | 49.922; 50.845; 50.845 | 48.076 [48.076, 48.076] | 50.845 [49.922, 50.845] | 2.769 | null |

### wlcb-104206-165-end

All six interval runs are fresh qualified results. Complete windows/metrics: [JSON](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-165-end/comparison.json)、[CSV](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-165-end/comparison.csv)、[Chinese details](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/analysis/comparisons/final-affinity/wlcb-104206-165-end/report.md).

| Metric/unit | Three reference runs | Three candidate runs | Reference median [range] | Candidate median [range] | Difference | Percentage |
|---|---|---|---|---|---|---|
| `full.update_duration_ms.p95`/ms | 15.5723; 15.1913; 0.037721 | 8.50782; 8.32472; 8.30874 | 15.1913 [0.037721, 15.5723] | 8.32472 [8.30874, 8.50782] | -6.86661 | -45.2009% |
| `full.detector_duration_ms.p95`/ms | 539.552; 528.713; 464.395 | 280.116; 292.927; 287.015 | 528.713 [464.395, 539.552] | 287.015 [280.116, 292.927] | -241.698 | -45.7144% |
| `full.request_queue_wait_ms.p95`/ms | 16.4653; 15.9608; 16.1421 | 42.0519; 32.8434; 32.5224 | 16.1421 [15.9608, 16.4653] | 32.8434 [32.5224, 42.0519] | 16.7013 | 103.464% |
| `full.source_capture_to_consumption_ms.p95`/ms | 578.667; 566.626; 483.36 | 325.136; 325.047; 324.948 | 566.626 [483.36, 578.667] | 325.047 [324.948, 325.136] | -241.579 | -42.6347% |
| `full.catchup_attempt_count`/count | 3; 2; 1 | 6; 2; 3 | 2 [1, 3] | 3 [2, 6] | 1 | 50% |
| `full.catchup_step_count`/count | 60; 30; 24 | 51; 10; 15 | 30 [24, 60] | 15 [10, 51] | -15 | -50% |
| `full.catchup_duration_ms.p95`/ms | 450.466; 263.255; 379.754 | 235.002; 63.1852; 60.2947 | 379.754 [263.255, 450.466] | 63.1852 [60.2947, 235.002] | -316.569 | -83.3615% |
| `full.catchup_step_duration_ms.p95`/ms | 16.6321; 17.335; 17.6222 | 24.115; 14.5726; 11.6457 | 17.335 [16.6321, 17.6222] | 14.5726 [11.6457, 24.115] | -2.76238 | -15.9353% |
| `full.recovery_update_duration_ms.p95`/ms | 456.712; 285.539; 387.489 | 242.763; 67.8774; 64.9291 | 387.489 [285.539, 456.712] | 67.8774 [64.9291, 242.763] | -319.611 | -82.4827% |
| `full.recovered_count`/count | 1; 0; 0 | 4; 1; 2 | 0 [0, 1] | 2 [1, 4] | 2 | null |
| `full.unrecovered_count`/count | 0; 1; 0 | 0; 0; 0 | 0 [0, 1] | 0 [0, 0] | 0 | 0% |
| `full.publication_count`/count | 2515; 2545; 2550 | 2549; 2572; 2572 | 2545 [2515, 2550] | 2572 [2549, 2572] | 27 | 1.0609% |
| `full.valid_publication_count`/count | 199; 140; 68 | 410; 319; 310 | 140 [68, 199] | 319 [310, 410] | 179 | 127.857% |
| `full.clear_count`/count | 1; 1; 0 | 4; 1; 2 | 1 [0, 1] | 2 [1, 4] | 1 | 100% |
| `full.max_no_publication_gap_ms`/ms | 473.374; 286.717; 404.148 | 259.444; 84.5453; 81.5846 | 404.148 [286.717, 473.374] | 84.5453 [81.5846, 259.444] | -319.603 | -79.0806% |
| `full.input_rejected_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.input_residual_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `full.frame_skipped_count`/count | 58; 30; 24 | 26; 3; 3 | 30 [24, 58] | 3 [3, 26] | -27 | -90% |
| `full.input_supply_lateness_ms.max`/ms | 0.088594; 0.087476; 0.086181 | 0.224013; 0.242435; 0.199701 | 0.087476 [0.086181, 0.088594] | 0.224013 [0.199701, 0.242435] | 0.136537 | 156.085% |
| `preparation_ms`/ms | 107709; 107373; 106949 | 107376; 107312; 107168 | 107373 [106949, 107709] | 107312 [107168, 107376] | -61 | -0.0568113% |
| `node_peak_mib`/MiB | 5862.14; 5871.86; 5871.57 | 5888.89; 5872.95; 5872.99 | 5871.57 [5862.14, 5871.86] | 5872.99 [5872.95, 5888.89] | 1.42188 | 0.0242163% |
| `logger_peak_mib`/MiB | 3.59375; 3.61719; 3.44922 | 3.44141; 3.61719; 3.60938 | 3.59375 [3.44922, 3.61719] | 3.60938 [3.44141, 3.61719] | 0.015625 | 0.434783% |
| `minimum_available_mib`/MiB | 1801.61; 1803.99; 1803.18 | 1779.16; 1794.43; 1804.01 | 1803.18 [1801.61, 1803.99] | 1794.43 [1779.16, 1804.01] | -8.74609 | -0.485038% |
| `cpu_point_count`/count | 30353; 30456; 30442 | 35294; 34940; 35094 | 30442 [30353, 30456] | 35094 [34940, 35294] | 4652 | 15.2815% |
| `cpu_point_cost_p50_ns`/ns | 99; 99; 99 | 69; 69; 69 | 99 [99, 99] | 69 [69, 69] | -30 | -30.303% |
| `cpu_point_cost_p99_ns`/ns | 1198; 1122; 1298 | 1104; 897; 874 | 1198 [1122, 1298] | 897 [874, 1104] | -301 | -25.1252% |
| `cpu_point_cost_max_ns`/ns | 37093; 36914; 42683 | 20746; 6026; 16537 | 37093 [36914, 42683] | 16537 [6026, 20746] | -20556 | -55.4175% |
| `submission_cost_p99_us`/us | 8.955; 8.76; 8.993 | 6.877; 6.325; 6.348 | 8.955 [8.76, 8.993] | 6.348 [6.325, 6.877] | -2.607 | -29.1122% |
| `unknown_timed_threads`/count | 18; 18; 18 | 11; 11; 11 | 18 [18, 18] | 11 [11, 11] | -7 | -38.8889% |
| `unknown_model_ready_threads`/count | 11; 11; 11 | 11; 11; 11 | 11 [11, 11] | 11 [11, 11] | 0 | 0% |
| `exited_during_read_count`/count | 0; 0; 0 | 0; 0; 0 | 0 [0, 0] | 0 [0, 0] | 0 | 0% |
| `temperature_max_c`/C | 48.076; 48.076; 48.076 | 52.692; 52.692; 51.768 | 48.076 [48.076, 48.076] | 52.692 [51.768, 52.692] | 4.616 | null |

Interpret recovery attempts with actual step counts. Changed work amounts prevent attributing total recovery-duration differences solely to scheduling. These data describe fixed-input distributions, not isolation of NPU/cache/memory-bandwidth contention.

## Actual CPUs, threads and observation cost

Supplier is the frame-input thread, Tracking the update thread, logger the independent diagnostic process, control the main thread, and supervision external controllers. All nine candidates read back supplier 4, Tracking 5, YOLO 6, logger 7, control 1, supervision 0–3. Managed updates, initialization, every actual catch-up step, YOLO boundaries, and publications were joined by round/update/request. Required points were complete with zero overflow/missing records. Reference retains 0–7 except supplier 4; it is not a CPU-1 reference.

The table’s work-point sets come from actual call records. Logger uses complete-process/thread snapshots. Supplier uses entry readback and per-frame wake/copy/lock CPU samples. Supervision is read back before playback origin. External `/proc` last_cpu is the most recent core observed discretely; it cannot replace call-time CPU.

| Interval | Side/run | Tracking call CPUs | YOLO call CPUs | control call CPUs | Unknown model-ready TIDs | Unknown playback TIDs |
|---|---|---|---|---|---|---|
| wlcb-104206-020-035 | ref/1 | 0,1,2,3 | 0,1,2,3 | 0 | 11 | 18 |
| wlcb-104206-020-035 | cand/1 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-020-035 | ref/2 | 0,1,2,3 | 0,1,2,3 | 1 | 11 | 18 |
| wlcb-104206-020-035 | cand/2 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-020-035 | ref/3 | 0,1,2,3 | 0,1,2,3 | 2 | 11 | 18 |
| wlcb-104206-020-035 | cand/3 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-100-130 | ref/1 | 0,1,2,3 | 0,1,2,3 | 0 | 11 | 18 |
| wlcb-104206-100-130 | cand/1 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-100-130 | ref/2 | 0,1,2,3 | 0,1,2,3 | 1 | 11 | 18 |
| wlcb-104206-100-130 | cand/2 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-100-130 | ref/3 | 0,1,2,3 | 0,1,2,3 | 0 | 11 | 18 |
| wlcb-104206-100-130 | cand/3 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-165-end | ref/1 | 0,1,2,3 | 0,1,2,3 | 2 | 11 | 18 |
| wlcb-104206-165-end | cand/1 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-165-end | ref/2 | 0,1,2,3 | 0,1,2,3 | 3 | 11 | 18 |
| wlcb-104206-165-end | cand/2 | 5 | 6 | 1 | 11 | 11 |
| wlcb-104206-165-end | ref/3 | 0,1,2,3 | 0,1,2,3 | 1 | 11 | 18 |
| wlcb-104206-165-end | cand/3 | 5 | 6 | 1 | 11 | 11 |

Unknown TIDs are observed library/middleware threads without managed-role registration; no RKNN/YOLO attribution is guessed. Decoder/pre-registration data remain preparation evidence. Stable masks are checked only after registration. Per-thread read-start times avoid false changes when a scan crosses registration. Exit races stay explicit and missing masks do not prove placement. Unknown TIDs/masks/last_cpu distributions and per-run temperature/frequency/memory are in `analysis/cpu-and-memory.json` and `thread-snapshots.jsonl`.

Each writer preallocates 262144 records. The hot path neither scans threads, writes files, nor allocates observer storage; masks are read only at entry registration. Per-point `cost_ns` measures internal monotonic-clock/CPU queries and record costs, excluding first registration and the whole receiver-call envelope. Separate submission costs include diagnostic clocks/notifications. These are not complete observation-off/on overhead comparisons. Both builds enable observation symmetrically, but it can still perturb execution.

External supervision takes snapshots at most once per second, plus model-ready/playback-ended boundaries. Node peak is the maximum RSS from preparation and thread snapshots. Minimum system-available memory uses the same discrete samples, not continuous absolute extrema. MiB is 1024×1024 bytes.

## Automated checks, historical failures and omitted scope

| Check | Result from this plan |
|---|---|
| Candidate `./build.sh --test` | 57/57 CTests; native OS memory limit 1/1 |
| Reference `./build.sh --test` | Initial 56/57; affected packaging CTest 1/1 after adding existing empty install directory. All 57 eventually passed, first failure log retained; native OS 1/1 |
| Both Python SDK groups | 88 unique cases each: 87 native + 1 actual ARM middleware; no skips, including affinity and controller failure/cleanup checks |
| Compiler/options equality | All 84 C++ targets match |
| Final board runs | 18/18 fresh planned runs qualified with all original integrity/deadline limits |

Initial reference packaging required ignored empty `.docker-artifacts/install/drone_tracker`, absent in a fresh clone but present for candidate/earlier diagnostic reference. Only that existing test-environment directory was added before rerunning the affected test; source and binaries were unchanged. `reference-build.log`, `reference-full-ctest-initial.log`, and `reference-package-fixture-recheck.log` remain. Artifact-directory retention is not permanent.

Initial host analysis started before ADB transfer completed and rejected the missing reference directory. Keep that attempt in `analysis-incomplete-transfer-attempt/` and `analysis-incomplete-transfer-console.log`. After copying, freeze/comparison reran and all three intervals passed identity/integrity without changing board records.

The original formal 210.9 ms pixel-copy wait, diagnostic failures, and import-order issue remain under their original evidence root and do not occupy any of the new eighteen-run slots. See [failure-investigation.md](failure-investigation.md) for causality/limits and [validation.md](validation.md) for the failed delivery. Those historical progress/conclusions do not describe this new plan.

clang-tidy/clangd were unavailable. Jetson hardware, camera, HDMI, long load, flight, and human-labeled accuracy did not run. This experiment compares regression CPU roles on fixed video. Containers/QEMU do not replace board timing; all eighteen runs here used the identified physical RK3576 board.

## Restoration and complete evidence

Independent `afffinal20-restore.timer` was active/verified before services stopped and renewed before each run. Final service PID **11692**, active/running/enabled; restore timer inactive; no test leftovers. All 91 protected files, models, Runtime, deployment link, and governors matched original values.

Protected-file mapping SHA-256: `2027df07543b1eaf29c30dd2ecd7ffd4e98b9ccecb688eabbda12842c4884ce4`. Deployment: `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`.

Complete evidence root: `/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020`. Restoration proof: [final-board-proof.json](/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/final-board-proof.json). Raw before/after states and guard completion are under `board/control/`.

Formal configurations/order are `board/control/{ref,cand}.json`, `plan.json`, `frozen-plan.json`, and per-interval `*-treatment.json`. Analysis entries are `analysis/freeze-config.json` and `analysis/compare-config.json`; all raw runs are under `board/r/runs/`. Interval JSON/CSV/Chinese reports preserve every metric/allowed difference. Additional CPU/memory/observer-cost comparisons are `analysis/supplementary-comparison.{json,csv}`.

Final source consistency and requirement mapping are in [final-validation.md](final-validation.md). A later verification changing build/runtime code requires refreezing and eighteen fresh runs; read-only verification may confirm reuse of this evidence.
