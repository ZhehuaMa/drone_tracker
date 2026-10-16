# Video regression against cb977c2 — 2026-10-15

This is a fresh post-verification comparison of the implemented working tree with exact Git commit `cb977c204615ade46a62d198d821ed93de449108`. Three independent qualified runs per version were recorded after the OpenSpec verification. Earlier implementation-cohort results were not reused as measurements.

## Scope and provenance

- Board: `HZ-RK3576_WuKong Board`; ADB serial `5ceb49407502303d`; machine ID `1fe0a397f2ae4343896c7cf9d253a608`; kernel `6.1.141-rt36`. Access used ADB only.
- Execution/report date is the host/client date, 2026-10-15. The board wall clock was stale (2026-08-14). Raw wall times were preserved; all measurement intervals use the board monotonic clock.
- System RKNN Runtime 2.3.2 and RKNPU driver 0.9.8; Runtime SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.
- Frozen fixture `wlcb-1513-loss-recovery`: 1,200 Gray8 frames, 1920×1200, the original video's 55–75 second interval. Full measurement window `[0,20)` seconds; core `[13.5,17.5)` seconds.
- Existing balloon YOLO and FP16 NanoTrack models were used without conversion. Exact model, fixture, configuration, source and loaded-library hashes appear in every run's `identity.json`.
- CPU affinity 0–7 and performance governors on both clusters. Formal tracker/interceptor services were stopped per attempt; diagnostics basic mode, visualization/OSD/recording disabled. Input predecoding, 32-frame queue, 50 ms maximum supply lateness and 120-second total deadline were unchanged.
- Attempt order: reference 001, candidate 001, reference 002, candidate 002, reference 003, candidate 003, candidate 004. Every attempt is retained in `attempt-ledger.json`; no run was selected by speed.
- Reused builds were independently checked against exact executable fingerprints and source. All 240 baseline C/C++ source files match the named commit. All 187 checked candidate production/harness files match the verified implementation snapshot. No algorithm or production source was changed by this task.
- The reference is exact baseline production C++, with the same current regression collection harness overlaid for measurement. Both versions use the current diagnostic worker for append-only event-format compatibility. This measurement adapter does not change baseline algorithms. Export hashes therefore identify measured source trees, rather than an untouched Git checkout.

| Identity | Reference | Candidate |
|---|---|---|
| Regression executable SHA-256 | `bad61138c886dc94811a6376c2f74ad08f6c8bae4e2a99a2dae81eb9feaaab63` | `07b48c8ef98018dcf097598cc7a1640656d9b62e7125e188f8c6543dc9748874` |
| Exported source SHA-256 | `51c9de649aa8df896bb30e8474bf5666459685ba1f4257922f7023699d5e7ce9` | `1fce3ccd2827b947ffd6da320a5f5133d319cb6a6aee92e91b7ce56c5e349b5f` |

Shared diagnostic worker SHA-256: `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5`.

## Measured differences

Full-window median valid-publication ratio changed from 31.838% to 58.585%. Median request-queue-wait P95 changed from 516.479 ms to 54.564 ms. Median maximum publication gap changed from 268.417 ms to 175.264 ms; the candidate's largest observed gap was 234.639 ms. These are continuity and timing measurements, not tracking-accuracy scores.

Each cell is the median across three runs, followed by `[minimum, maximum]`. P95 means the 95th percentile within one run; the table then aggregates those three P95 values. Unavailable values remain missing when a historical event field is absent or any run has no duration event. They are never replaced with zero or omitted to improve an aggregate. Qualification establishes complete fixed-input evidence; it does not establish acceptable performance. No performance thresholds were supplied.

### Full window

| Measure | Reference | Candidate |
|---|---|---|
| Valid publication ratio (%) | 31.838 [11.414, 47.527] | 58.585 [53.182, 58.899] |
| Publication count | 1137.000 [1132.000, 1174.000] | 1159.000 [1100.000, 1163.000] |
| Maximum actual publication gap (ms) | 268.417 [266.490, 309.166] | 175.264 [174.697, 234.639] |
| Maximum invalid publication span (ms) | 9892.825 [8491.274, 10208.938] | 7518.793 [7417.911, 7951.768] |
| Clear decisions | 2.000 [1.000, 3.000] | 1.000 [1.000, 2.000] |
| Completed recoveries | 1.000 [0.000, 2.000] | 0.000 [0.000, 1.000] |
| Unrecovered clear decisions | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| Completed recovery latency p95 (ms) | Unavailable | Unavailable |
| Publication frame age p95 (ms) | 31.343 [26.730, 32.373] | 32.909 [32.530, 33.056] |
| Update duration p95 (ms) | 16.781 [15.959, 16.978] | 17.331 [16.958, 17.333] |
| Maximum update duration (ms) | 251.545 [249.572, 292.314] | 158.385 [157.602, 233.455] |
| Positive-result history misses | 7.000 [6.000, 7.000] | 0.000 [0.000, 0.000] |
| Catchup replay steps | 49.000 [23.000, 51.000] | 9.000 [9.000, 71.000] |
| Maximum catchup duration (ms) | 245.080 [241.608, 285.784] | 150.382 [149.750, 211.101] |
| Request queue wait p95 (ms) | 516.479 [508.003, 525.717] | 54.564 [29.208, 59.308] |
| Detection execution p95 (ms) | 552.707 [551.659, 554.019] | 555.816 [554.153, 558.997] |
| Capture-to-consumption age p95 (ms) | Unavailable | 622.014 [619.854, 628.113] |
| Superseded requests | Unavailable | 555.000 [551.000, 565.000] |
| Available source comparison records | Unavailable | 33.000 [30.000, 50.000] |
| Unavailable recovery records | Unavailable | 0.000 [0.000, 0.000] |
| Selected frames | 1137.000 [1132.000, 1174.000] | 1159.000 [1100.000, 1163.000] |
| Skipped frames | 63.000 [26.000, 68.000] | 41.000 [37.000, 100.000] |
| Maximum supply lateness (ms) | 7.056 [4.383, 10.120] | 7.958 [5.699, 14.070] |

### Core window

| Measure | Reference | Candidate |
|---|---|---|
| Valid publication ratio (%) | 76.617 [0.000, 81.166] | 100.000 [100.000, 100.000] |
| Publication count | 223.000 [201.000, 240.000] | 228.000 [228.000, 230.000] |
| Maximum actual publication gap (ms) | 134.782 [17.688, 309.166] | 19.976 [19.188, 20.441] |
| Maximum invalid publication span (ms) | 796.419 [552.379, 4000.000] | 0.000 [0.000, 0.000] |
| Clear decisions | 1.000 [0.000, 2.000] | 0.000 [0.000, 0.000] |
| Completed recoveries | 1.000 [0.000, 1.000] | 0.000 [0.000, 0.000] |
| Unrecovered clear decisions | 0.000 [0.000, 1.000] | 0.000 [0.000, 0.000] |
| Completed recovery latency p95 (ms) | Unavailable | Unavailable |
| Publication frame age p95 (ms) | 32.945 [1.757, 33.687] | 33.414 [33.375, 33.471] |
| Update duration p95 (ms) | 17.383 [0.037, 17.472] | 17.479 [17.306, 17.520] |
| Maximum update duration (ms) | 117.760 [0.057, 292.314] | 18.761 [17.957, 19.262] |
| Positive-result history misses | 2.000 [2.000, 3.000] | 0.000 [0.000, 0.000] |
| Catchup replay steps | 11.000 [0.000, 35.000] | 0.000 [0.000, 0.000] |
| Maximum catchup duration (ms) | Unavailable | Unavailable |
| Request queue wait p95 (ms) | 516.479 [486.869, 528.524] | 54.564 [28.771, 60.417] |
| Detection execution p95 (ms) | 553.469 [538.073, 559.593] | 553.163 [552.262, 558.997] |
| Capture-to-consumption age p95 (ms) | Unavailable | 592.784 [589.997, 620.696] |
| Superseded requests | Unavailable | 27.000 [26.000, 34.000] |
| Available source comparison records | Unavailable | 19.000 [15.000, 27.000] |
| Unavailable recovery records | Unavailable | 0.000 [0.000, 0.000] |
| Selected frames | 223.000 [200.000, 240.000] | 228.000 [228.000, 230.000] |
| Skipped frames | 17.000 [0.000, 41.000] | 12.000 [11.000, 12.000] |
| Maximum supply lateness (ms) | 3.946 [2.267, 4.383] | 3.411 [2.456, 4.184] |

## Attempt accounting, watchdog and memory

Qualification rates for all attempted runs: reference: 3/3 qualified (100.0%); candidate: 3/4 qualified (75.0%). The formal metric tables include only the first three runs per version that passed the unchanged qualification gates. Rejected attempts' partial metrics are not valid comparison evidence. A lower qualification rate is a limitation even when a later attempt succeeds.

| Run | Qualified | Total seconds | Observed watchdog events | Observed supersessions | Preparation peak RSS (KiB) |
|---|---|---|---|---|---|
| reference/run-001 | True | 84.331 | 0 | 0 | 2785412 |
| candidate/run-001 | False | 87.056 | 0 | 638 | 2785396 |
| reference/run-002 | True | 84.433 | 0 | 0 | 2785416 |
| candidate/run-002 | True | 85.132 | 0 | 565 | 2785416 |
| reference/run-003 | True | 84.446 | 0 | 0 | 2785416 |
| candidate/run-003 | True | 85.847 | 0 | 551 | 2785416 |
| candidate/run-004 | True | 85.245 | 0 | 555 | 2785412 |

Rejected attempt `candidate/run-001`: frame 11: supply lateness budget exceeded; frame 12: supply lateness budget exceeded; frame 13: supply lateness budget exceeded; frame 14: supply lateness budget exceeded; frame 15: supply lateness budget exceeded; metrics: record incomplete; node exit 1; source: supply_lateness_exceeded; controller_abnormal_exit. The existing qualification limits were retained, and a fresh attempt used unchanged conditions.


All qualified raw exports passed checksum validation and exact metric recomputation. The comparison reports `comparable` with no comparison-identity differences. Any failed attempt and its qualification reasons remain in `raw-summary.json` and the ledger.

For the rejected attempt, counts describe only retained partial records. They cannot establish the absence of later events and are excluded from formal comparison metrics.

Across the qualified candidate runs, the raw log contains 0 comparable source-context events after the source image was absent, and 5 unavailable source-context events. An event count does not establish that a recovery succeeded.

RSS is physical memory reported by Linux. These observations were made during preparation, before playback; they are not a runtime memory curve. The decoded pixel cache is 2,764,800,000 bytes. This specific board reports 8,112,044 KiB RAM, rather than the 3.8 GiB configuration described in repository guidance. This run does not establish the smaller configuration's memory acceptance.

The watchdog retains its existing result-count behavior. Any read result resets the no-result counter, including an uncomparable positive result. Its strict default boundary is update 61 after 60 updates without a result. The video counts above establish only the events observed in these runs. Boundary behavior, metadata-only recovery and pixel-lifetime guarantees rely on the separate controlled implementation tests; no freshness cutoff is inferred from this footage.

## Interpretation and limitations

A valid publication carries a tracking flag and finite positive-size box. This footage has no human target labels. Higher continuity can include tracking the wrong object, so these data cannot establish detection precision, tracking accuracy or genuine-loss recognition delay. The gap metric uses actual publication-call return times, including window edges; it does not establish subscriber receipt. Recovery latency measures clear-decision to next valid publication, with unresolved events retained as censored observations.

Three runs show observed variability, not a confidence interval. The combined change does not isolate individual algorithm contributions. Synchronous initialization and replay still block publication. Results apply to this board, fixture, models and isolated workload. They do not establish camera, HDMI, long-duration load, flight, Jetson or other-board acceptance.

## Service restoration and retained evidence

Before every service stop, `run_one.sh` armed and checked an independent 180-second systemd restoration timer. Its EXIT trap also ran `restore_guard.py`, terminated cohort processes and restored the original states. Every per-attempt restoration record confirms success. Final snapshots confirm tracker and interceptor active/running, diagnostics inactive, the original release `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`, unchanged production configuration, model/Runtime hashes and CPU governors. No persistent deployment change was made.

Host evidence: `/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/.docker-artifacts/improve-vs-cb977c2-20261015`.
Board evidence: `/home/hzhy/improve-vs-cb977c2-20261015`.

- `reference/run-*` and `candidate/run-*`: raw `.dtd` segments, decoded event exports, publication/input records, run identities, preparation memory observations, metrics, checksums and deadline outcomes.
- `comparison/comparison.json`, `comparison/comparison.csv`, `comparison/report.md`: all full/core metrics with per-run values, absolute/relative changes, availability and sample counts.
- `baseline/baseline.json`: immutable baseline manifest for the three fresh reference runs.
- `independent-source-binary-audit.json`, `build-identities.json`: exact source/build provenance; `independent-audit.json`: final recomputation, identity and restoration checks.
- `service-before.json`, `service-after.json`, `restore-ROLE-NNN.json`, `final-service-status.txt`: recovery evidence.
- `run_cohort.py`, `run_one.sh`, `attempt.py`, `plan.json`, `run-config.json` and `analysis-tools/manifest.json`: execution policy and hashed offline analysis tools.

To recompute a new comparison without overwriting this evidence, run the retained baseline and compare tools with new output directories. Each board attempt uses `adb -s 5ceb49407502303d shell 'bash /home/hzhy/improve-vs-cb977c2-20261015/run_one.sh ROLE NNN'`.
