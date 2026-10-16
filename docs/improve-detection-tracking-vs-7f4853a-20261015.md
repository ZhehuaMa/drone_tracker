# Video regression against 7f4853a — 2026-10-15

This is a fresh post-verification comparison of the implemented working tree with exact Git commit `7f4853a4876d4242d67480ffae64767d9d97573d`. Three independent qualified runs per version were recorded after the OpenSpec verification. No earlier task or implementation-cohort runs were reused as measurements.

## Scope and provenance

- Board: `HZ-RK3576_WuKong Board`; ADB serial `5ceb49407502303d`; machine ID `1fe0a397f2ae4343896c7cf9d253a608`; kernel `6.1.141-rt36`. Access used ADB only.
- Execution/report date is the host/client date, 2026-10-15. The board wall clock was stale (2026-08-14). Raw wall times were preserved; all measurement intervals use the board monotonic clock.
- System RKNN Runtime 2.3.2 and RKNPU driver 0.9.8; Runtime SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.
- Frozen fixture `wlcb-1513-loss-recovery`: 1,200 Gray8 frames, 1920×1200, the original video's 55–75 second interval. Full measurement window `[0,20)` seconds; core `[13.5,17.5)` seconds.
- Existing balloon YOLO and FP16 NanoTrack models were used without conversion. Exact model, fixture, configuration, source and loaded-library hashes appear in every run's `identity.json`.
- CPU affinity 0–7 and performance governors on both clusters. Formal tracker/interceptor services were stopped per attempt; diagnostics basic mode, visualization/OSD/recording disabled. Input predecoding, 32-frame queue, 50 ms maximum supply lateness and 120-second total deadline were unchanged.
- Attempt order: reference 001, candidate 001, reference 002, candidate 002, reference 003, candidate 003, reference 004, candidate 004, reference 005, candidate 005, reference 006, candidate 006. Every attempt is retained in `attempt-ledger.json`; no run was selected by speed.
- The fresh reference build and reused candidate build were independently checked against exact executable fingerprints and source. All 275 baseline production source files match the named commit. All 187 checked candidate production/harness files match the verified implementation snapshot. No algorithm or production source was changed by this task.
- The reference is exact baseline production C++, with the same current regression collection harness overlaid for measurement. Both versions use the current diagnostic worker for append-only event-format compatibility. This measurement adapter does not change baseline algorithms. Export hashes therefore identify measured source trees, rather than an untouched Git checkout.

| Identity | Reference | Candidate |
|---|---|---|
| Regression executable SHA-256 | `b2635c301edd299d773ad357505cd2b9c2058aba1b3b6c28cfa925344847d372` | `07b48c8ef98018dcf097598cc7a1640656d9b62e7125e188f8c6543dc9748874` |
| Exported source SHA-256 | `6c6db274473be835349c7cf1df8e343b97008fab6a2db7f4c9d1b66bc6975c57` | `1fce3ccd2827b947ffd6da320a5f5133d319cb6a6aee92e91b7ce56c5e349b5f` |

Shared diagnostic worker SHA-256: `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5`.

The historical `code.dependency_commit=e555f7a335660b6efd89bd5c4f757522834a6b03` field is an inherited, fixed planning label. It is not baseline ancestry evidence. Exact Git commit, exported source hashes, all 275 untouched production source files and executable fingerprints identify this reference. The raw label is preserved and must not be interpreted as showing the newer local-window optimization exists in 7f4853a.

The baseline build overlays only the frozen measurement harness and build workflow. Its root CMake file adds the regression target and excludes two unrelated standalone benchmark targets whose Git identity generation cannot run in an exported archive. No baseline `src/` file changed. The first failed build and complete successful ARM64 cross-build logs are retained. The source archive, full overlay diff and per-file manifest make this build reviewable.

## Measured differences

Full-window median valid-publication ratio changed from 35.692% to 55.972%. Median request-queue-wait P95 changed from 472.725 ms to 40.622 ms. Median maximum publication gap changed from 414.194 ms to 176.176 ms; the candidate's largest observed gap was 229.504 ms. Detection-execution P95 increased from 520.086 ms to 562.407 ms. The smaller queue wait therefore does not mean faster neural inference. These are continuity and timing measurements, not tracking-accuracy scores.

Each cell is the median across three runs, followed by `[minimum, maximum]`. P95 means the 95th percentile within one run; the table then aggregates those three P95 values. Unavailable values remain missing when a historical event field is absent or any run has no duration event. They are never replaced with zero or omitted to improve an aggregate. Qualification establishes complete fixed-input evidence; it does not establish acceptable performance. No performance thresholds were supplied.

### Full window

| Measure | Reference | Candidate |
|---|---|---|
| Valid publication ratio (%) | 35.692 [31.102, 35.751] | 55.972 [55.788, 56.076] |
| Publication count | 975.000 [971.000, 979.000] | 1149.000 [1147.000, 1152.000] |
| Maximum actual publication gap (ms) | 414.194 [412.227, 473.979] | 176.176 [173.408, 229.504] |
| Maximum invalid publication span (ms) | 9990.458 [9988.090, 10363.548] | 7919.227 [7916.531, 7948.474] |
| Clear decisions | 1.000 [1.000, 2.000] | 1.000 [1.000, 1.000] |
| Completed recoveries | 0.000 [0.000, 1.000] | 0.000 [0.000, 0.000] |
| Unrecovered clear decisions | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| Completed recovery latency p95 (ms) | Unavailable | Unavailable |
| Publication frame age p95 (ms) | 40.151 [39.596, 40.251] | 32.881 [32.851, 33.007] |
| Update duration p95 (ms) | 31.672 [24.385, 32.095] | 17.183 [17.179, 17.435] |
| Maximum update duration (ms) | 397.414 [395.396, 456.909] | 159.420 [156.399, 228.285] |
| Positive-result history misses | 3.000 [3.000, 3.000] | 0.000 [0.000, 0.000] |
| Catchup replay steps | 23.000 [23.000, 53.000] | 20.000 [18.000, 22.000] |
| Maximum catchup duration (ms) | 359.803 [355.279, 424.393] | 151.375 [147.847, 206.098] |
| Request queue wait p95 (ms) | 472.725 [461.477, 474.751] | 40.622 [30.802, 46.213] |
| Detection execution p95 (ms) | 520.086 [519.911, 520.999] | 562.407 [560.608, 563.324] |
| Capture-to-consumption age p95 (ms) | Unavailable | 628.889 [628.449, 632.998] |
| Superseded requests | Unavailable | 575.000 [567.000, 577.000] |
| Available source comparison records | Unavailable | 41.000 [36.000, 48.000] |
| Unavailable recovery records | Unavailable | 0.000 [0.000, 0.000] |
| Selected frames | 975.000 [971.000, 979.000] | 1149.000 [1147.000, 1152.000] |
| Skipped frames | 225.000 [221.000, 229.000] | 51.000 [48.000, 53.000] |
| Maximum supply lateness (ms) | 5.714 [4.800, 5.781] | 4.918 [4.695, 8.502] |

### Core window

| Measure | Reference | Candidate |
|---|---|---|
| Valid publication ratio (%) | 100.000 [85.915, 100.000] | 100.000 [100.000, 100.000] |
| Publication count | 158.000 [142.000, 159.000] | 228.000 [227.000, 229.000] |
| Maximum actual publication gap (ms) | 36.374 [35.896, 473.979] | 19.697 [19.420, 20.262] |
| Maximum invalid publication span (ms) | 0.000 [0.000, 765.627] | 0.000 [0.000, 0.000] |
| Clear decisions | 0.000 [0.000, 1.000] | 0.000 [0.000, 0.000] |
| Completed recoveries | 0.000 [0.000, 1.000] | 0.000 [0.000, 0.000] |
| Unrecovered clear decisions | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] |
| Completed recovery latency p95 (ms) | Unavailable | Unavailable |
| Publication frame age p95 (ms) | 46.678 [41.641, 48.708] | 33.360 [33.349, 33.585] |
| Update duration p95 (ms) | 32.962 [32.707, 33.056] | 17.490 [17.435, 17.791] |
| Maximum update duration (ms) | 35.202 [34.702, 456.909] | 18.508 [18.198, 19.078] |
| Positive-result history misses | 0.000 [0.000, 1.000] | 0.000 [0.000, 0.000] |
| Catchup replay steps | 0.000 [0.000, 23.000] | 0.000 [0.000, 0.000] |
| Maximum catchup duration (ms) | Unavailable | Unavailable |
| Request queue wait p95 (ms) | 470.994 [414.463, 472.725] | 30.802 [30.520, 46.864] |
| Detection execution p95 (ms) | 520.119 [515.877, 520.253] | 560.214 [558.928, 564.690] |
| Capture-to-consumption age p95 (ms) | Unavailable | 632.998 [623.330, 636.238] |
| Superseded requests | Unavailable | 28.000 [26.000, 39.000] |
| Available source comparison records | Unavailable | 26.000 [13.000, 28.000] |
| Unavailable recovery records | Unavailable | 0.000 [0.000, 0.000] |
| Selected frames | 158.000 [142.000, 158.000] | 228.000 [227.000, 229.000] |
| Skipped frames | 83.000 [81.000, 97.000] | 11.000 [11.000, 13.000] |
| Maximum supply lateness (ms) | 3.353 [3.226, 4.153] | 3.364 [3.268, 3.476] |

## Attempt accounting, watchdog and memory

Five infrastructure attempts failed before playback: reference 001–003 and candidate 001–002. The private shared diagnostic worker lacked executable mode after copying ADB-pulled files. Their controller logs show `Permission denied`; measurement columns are unavailable. Execute permissions were corrected without changing contents, models, parameters, source hashes or timing limits. On-board modes and hashes for both roles were checked before subsequent stops. After the infrastructure correction, reference qualification was 3/3 and candidate qualification was 3/4. The candidate had one measured supply-lateness rejection. These failures are separate from any later measured input-budget rejection. All attempts remain in the chronological ledger.


Qualification rates for all attempted runs: reference: 3/6 qualified (50.0%); candidate: 3/6 qualified (50.0%). The formal metric tables include only the first three runs per version that passed the unchanged qualification gates. Rejected attempts' partial metrics are not valid comparison evidence. A lower qualification rate is a limitation even when a later attempt succeeds.

| Run | Qualified | Total seconds | Observed watchdog events | Observed supersessions | Preparation peak RSS (KiB) |
|---|---|---|---|---|---|
| reference/run-001 | False | 5.989 | Unavailable | Unavailable | Unavailable |
| candidate/run-001 | False | 5.979 | Unavailable | Unavailable | Unavailable |
| reference/run-002 | False | 5.989 | Unavailable | Unavailable | Unavailable |
| candidate/run-002 | False | 6.041 | Unavailable | Unavailable | Unavailable |
| reference/run-003 | False | 5.989 | Unavailable | Unavailable | Unavailable |
| candidate/run-003 | True | 85.488 | 0 | 575 | 2785400 |
| reference/run-004 | True | 83.542 | 0 | 0 | 2785408 |
| candidate/run-004 | False | 86.999 | 0 | 468 | 2785416 |
| reference/run-005 | True | 83.778 | 0 | 0 | 2785384 |
| candidate/run-005 | True | 85.496 | 0 | 577 | 2785416 |
| reference/run-006 | True | 83.619 | 0 | 0 | 2785404 |
| candidate/run-006 | True | 85.838 | 0 | 567 | 2785416 |

Rejected attempt `reference/run-001`: preflight_or_collection_failed; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.

Rejected attempt `candidate/run-001`: preflight_or_collection_failed; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.

Rejected attempt `reference/run-002`: preflight_or_collection_failed; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.

Rejected attempt `candidate/run-002`: preflight_or_collection_failed; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.

Rejected attempt `reference/run-003`: preflight_or_collection_failed; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.

Rejected attempt `candidate/run-004`: frame 10: supply lateness budget exceeded; frame 11: supply lateness budget exceeded; frame 12: supply lateness budget exceeded; frame 13: supply lateness budget exceeded; frame 14: supply lateness budget exceeded; frame 1: supply lateness budget exceeded; frame 2: supply lateness budget exceeded; frame 3: supply lateness budget exceeded; frame 4: supply lateness budget exceeded; frame 5: supply lateness budget exceeded; frame 6: supply lateness budget exceeded; frame 7: supply lateness budget exceeded; frame 8: supply lateness budget exceeded; frame 9: supply lateness budget exceeded; metrics: record incomplete; node exit 1; source: supply_lateness_exceeded; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.


All qualified raw exports passed checksum validation and exact metric recomputation. The comparison reports `comparable` with no comparison-identity differences. Any failed attempt and its qualification reasons remain in `raw-summary.json` and the ledger.

For a measured rejected attempt, counts describe only retained partial records. Preflight-only attempts have no measurement evidence. They cannot establish the absence of later events and are excluded from formal comparison metrics.

Across the qualified candidate runs, the raw log contains 0 comparable source-context events after the source image was absent, and 3 unavailable source-context events. An event count does not establish that a recovery succeeded.

RSS is physical memory reported by Linux. These observations were made during preparation, before playback; they are not a runtime memory curve. The decoded pixel cache is 2,764,800,000 bytes. This specific board reports 8,112,044 KiB RAM, rather than the 3.8 GiB configuration described in repository guidance. This run does not establish the smaller configuration's memory acceptance.

The watchdog retains its existing result-count behavior. Any read result resets the no-result counter, including an uncomparable positive result. Its strict default boundary is update 61 after 60 updates without a result. The video counts above establish only the events observed in these runs. Boundary behavior, metadata-only recovery and pixel-lifetime guarantees rely on the separate controlled implementation tests; no freshness cutoff is inferred from this footage.

## Interpretation and limitations

A valid publication carries a tracking flag and finite positive-size box. This footage has no human target labels. Higher continuity can include tracking the wrong object, so these data cannot establish detection precision, tracking accuracy or genuine-loss recognition delay. The gap metric uses actual publication-call return times, including window edges; it does not establish subscriber receipt. Recovery latency measures clear-decision to next valid publication, with unresolved events retained as censored observations.

The reference predates the local-window preprocessing optimization (dependency commit e555f7a). This comparison includes that earlier optimization and subsequent changes. Three runs show observed variability, not a confidence interval. The combined change does not isolate individual algorithm contributions. Synchronous initialization and replay still block publication. Results apply to this board, fixture, models and isolated workload. They do not establish camera, HDMI, long-duration load, flight, Jetson or other-board acceptance.

## Service restoration and retained evidence

Before every service stop, `run_one.sh` armed and checked an independent 180-second systemd restoration timer. Its EXIT trap also ran `restore_guard.py`, terminated cohort processes and restored the original states. Every per-attempt restoration record confirms success. Final snapshots confirm tracker and interceptor active/running, diagnostics inactive, the original release `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`, unchanged production configuration, model/Runtime hashes and CPU governors. No persistent deployment change was made.

Host evidence: `/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/.docker-artifacts/improve-vs-7f4853a-20261015`.
Board evidence: `/home/hzhy/improve-vs-7f4853a-20261015`.

- `reference/run-*` and `candidate/run-*`: raw `.dtd` segments, decoded event exports, publication/input records, run identities, preparation memory observations, metrics, checksums and deadline outcomes.
- `comparison/comparison.json`, `comparison/comparison.csv`, `comparison/report.md`: all full/core metrics with per-run values, absolute/relative changes, availability and sample counts.
- `baseline/baseline.json`: immutable baseline manifest for the three fresh reference runs.
- `exact-git.tar`, `baseline-source-audit.json`, `measurement-build-overlay.diff`, `overlay-manifest.json`: full measurement/build overlay and untouched baseline production source audit.
- `independent-source-binary-audit.json`, `build-identities.json`: exact source/build provenance; `independent-audit.json`: final recomputation, identity and restoration checks. `final-board-source-binary-audit.json` verifies all 382 reference and 405 candidate exported source files, all retained executable hashes and no remaining restoration timers. `execution-notes.md` explains build and preflight failures.
- `service-before.json`, `service-after.json`, `restore-ROLE-NNN.json`, `final-service-status.txt`: recovery evidence.
- `run_cohort.py`, `run_one.sh`, `attempt.py`, `plan.json`, `run-config.json` and `analysis-tools/manifest.json`: execution policy and hashed offline analysis tools.

To recompute a new comparison without overwriting this evidence, run the retained baseline and compare tools with new output directories. Each board attempt uses `adb -s 5ceb49407502303d shell 'bash /home/hzhy/improve-vs-7f4853a-20261015/run_one.sh ROLE NNN'`.
