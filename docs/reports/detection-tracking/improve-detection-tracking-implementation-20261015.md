# Detection and tracking implementation evaluation — 2026-10-15

The OpenSpec change `improve-detection-tracking` is implemented. The final ARM64 build passed all 57 CTests and the separate native Linux memory-limit check. This report records the change’s required hardware evaluation against `cb977c204615ade46a62d198d821ed93de449108`. It precedes the separately requested post-verification comparisons.

## Inputs and provenance

- Board: `HZ-RK3576_WuKong Board`, ADB serial `5ceb49407502303d`, kernel `6.1.141-rt36`. Only ADB was used.
- Runtime: system RKNN 2.3.2; RKNPU driver 0.9.8. System Runtime SHA-256 stayed `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.
- Scheduling: inherited CPU affinity 0–7; both CPU clusters use the performance governor. Both formal tracker and interceptor services were stopped during each attempt and restored afterward.
- Input: frozen `wlcb-1513-loss-recovery`, 1,200 verified Gray8 frames at 1920×1200, original 55–75 second interval. Full measurement window is 20 seconds; core window is [13.5, 17.5) seconds.
- Models: existing balloon YOLO model and FP16 NanoTrack template backbone, search backbone and head under `/opt/drone_tracker/models`. Exact model/configuration/fixture/library hashes are in each run identity.
- Diagnostics: basic mode, unchanged box publication, visualization/OSD/recording disabled. The same current diagnostic worker and frozen run-time collection tools were used for both versions.
- Each version has three independent qualified runs. Every run preserved the 50 ms supply-lateness budget and 120-second total deadline. No board attempt was rejected or selected by speed.
- Reference production C++ was built from the exact baseline commit. Its exported source contains the shared current collection/metric harness. Candidate production C++ matches the implemented working tree. Full exported source and executable fingerprints are retained in `build-identities.json`.
- The initial baseline freeze rejected new metric descriptors. Corrected postprocessing readers are retained separately under `analysis-tools/` with content hashes. No measured raw log, source snapshot, metric file or run identity was rewritten. Final test-only coverage additions vary pixels, capture times and regions across replacement inputs, check executing-input identity, and verify worker progress while a producer continues replacing pending requests.

| Role | Regression executable SHA-256 | Exported source SHA-256 |
|---|---|---|
| reference | `bad61138c886dc94811a6376c2f74ad08f6c8bae4e2a99a2dae81eb9feaaab63` | `51c9de649aa8df896bb30e8474bf5666459685ba1f4257922f7023699d5e7ce9` |
| candidate | `07b48c8ef98018dcf097598cc7a1640656d9b62e7125e188f8c6543dc9748874` | `1fce3ccd2827b947ffd6da320a5f5133d319cb6a6aee92e91b7ce56c5e349b5f` |

## Measured results

Values are the median of three runs, followed by the observed minimum and maximum. Unavailable duration aggregates include windows with no recovery event in one or more runs; those cases remain absent rather than becoming zero-duration samples. Historical fields are also unavailable where the reference did not record them. A qualified run means complete evidence and fixed inputs. It does not imply acceptable performance. No performance decision thresholds were supplied.

Median full-clip valid publication ratio rose from 43.080% to 55.652%. Median queue-wait p95 fell from 521.504 ms to 45.711 ms. The candidate retained a slower qualified outlier: its core-window maximum publication gap reached 352.009 ms, exceeding the reference range of 135.204–150.955 ms. The combined change improves these medians, while tail behavior remains variable. These measurements do not isolate the contribution of each algorithm change.

### Full window

| Measure | Reference | Candidate |
|---|---|---|
| Valid publication ratio (%) | 43.080 [40.383, 50.765] | 55.652 [51.363, 55.893] |
| Publication count | 1149.000 [1111.000, 1156.000] | 1150.000 [1137.000, 1154.000] |
| Maximum actual publication gap (ms) | 269.170 [266.297, 391.584] | 173.525 [172.759, 352.009] |
| Update duration p95 (ms) | 16.784 [16.662, 17.108] | 17.104 [16.907, 17.114] |
| Maximum update duration (ms) | 252.254 [249.472, 390.390] | 156.548 [155.912, 335.024] |
| History misses after positive detection | 5.000 [5.000, 5.000] | 0.000 [0.000, 0.000] |
| Catchup replay steps | 34.000 [26.000, 67.000] | 23.000 [18.000, 36.000] |
| Catchup duration maximum (ms) | 242.755 [240.531, 366.472] | 148.503 [147.820, 328.049] |
| Request queue wait p95 (ms) | 521.504 [508.614, 524.628] | 45.711 [25.170, 57.965] |
| Detection execution p95 (ms) | 552.773 [551.517, 554.698] | 553.594 [553.180, 555.505] |
| Capture-to-consumption age p95 (ms) | Unavailable | 625.172 [616.028, 637.351] |
| Superseded requests | Unavailable | 572.000 [569.000, 608.000] |
| Available source comparison records | Unavailable | 38.000 [27.000, 48.000] |
| Unavailable recovery records | Unavailable | 0.000 [0.000, 0.000] |

### Core window

| Measure | Reference | Candidate |
|---|---|---|
| Valid publication ratio (%) | 81.166 [79.730, 95.909] | 100.000 [91.707, 100.000] |
| Publication count | 222.000 [220.000, 223.000] | 229.000 [205.000, 230.000] |
| Maximum actual publication gap (ms) | 136.532 [135.204, 150.955] | 20.491 [19.909, 352.009] |
| Update duration p95 (ms) | 17.263 [17.090, 17.301] | 17.322 [17.312, 17.459] |
| Maximum update duration (ms) | 119.606 [118.376, 134.164] | 18.682 [18.151, 335.024] |
| History misses after positive detection | 1.000 [1.000, 2.000] | 0.000 [0.000, 0.000] |
| Catchup replay steps | 11.000 [11.000, 12.000] | 0.000 [0.000, 27.000] |
| Catchup duration maximum (ms) | 113.141 [111.427, 127.793] | Unavailable |
| Request queue wait p95 (ms) | 508.614 [488.072, 524.645] | 57.151 [8.282, 62.076] |
| Detection execution p95 (ms) | 552.016 [547.902, 554.290] | 553.548 [552.752, 556.750] |
| Capture-to-consumption age p95 (ms) | Unavailable | 597.531 [592.431, 638.405] |
| Superseded requests | Unavailable | 35.000 [26.000, 42.000] |
| Available source comparison records | Unavailable | 9.000 [9.000, 29.000] |
| Unavailable recovery records | Unavailable | 0.000 [0.000, 0.000] |

## Raw accounting, watchdog and memory

| Role/run | Qualified | Total seconds | Watchdog timeouts | Superseded cancellations | Preparation peak RSS (KiB) |
|---|---|---|---|---|---|
| reference/run-001 | True | 84.531 | 0 | 0 | 2791660 |
| reference/run-002 | True | 84.226 | 0 | 0 | 2791672 |
| reference/run-003 | True | 84.440 | 0 | 0 | 2791684 |
| candidate/run-001 | True | 84.989 | 0 | 572 | 2791668 |
| candidate/run-002 | True | 85.198 | 0 | 569 | 2791688 |
| candidate/run-003 | True | 85.184 | 0 | 608 | 2791688 |

RSS means resident physical memory reported by Linux. These are preparation-time observations before playback, not a runtime memory series. The pixel cache is exactly 2,764,800,000 bytes. The board reports 8,112,044 KiB total system memory; this is the observed board configuration, rather than the 3.8 GiB description in repository guidance. The deterministic weak-reference tests separately show that superseded input and metadata alone do not retain pixels.

No candidate video run exercised an evicted image with a valid source tracker observation. Metadata-only agreement and unavailable recovery are established by controlled tests, rather than claimed as observed video outcomes.

The watchdog is unchanged: any read result resets the tracking no-result counter, including an uncomparable positive result. With defaults, update 60 does not time out and update 61 does. Timeout clears initialization and boxes, without adding a TargetLost transition. The controlled tests passed these boundaries, no-target loss at five results, Detecting inactivity and partial historical replay. Raw observed timeout counts are listed above and preserve the strict existing threshold. There is no result-age cutoff or freshness guarantee.

Unannotated footage cannot establish tracking accuracy or genuine-loss recognition delay. Higher valid-box continuity can include a wrong track. Synchronous initialization and historical replay still block publication. Hardware timing results apply to this identified board, models, workload and fixture. Jetson, camera, HDMI, sustained-load and flight validation were not run. `clang-tidy` and `clangd` were unavailable/unconfigured in the supported environment.

## Reproduction and retained evidence

Host evidence root: `/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/.docker-artifacts/improve-cohort-20261015`.
Board evidence root: `/home/hzhy/improve-detection-tracking-20261015`.
All six run directories contain raw binary segments, decoded events, exact identities, preparation/input-clock records, recomputable metrics, checksums and deadline outcomes. Baseline and comparison outputs are immutable. `raw-summary.json` retains cancellation reasons, metadata-only examples, watchdog counts and memory values. `build-validation.json` records earlier build/test failures and the final passing checks. No Runtime, model, production configuration or deployed release was replaced.

Each board attempt used `run_one.sh ROLE NNN`. Its EXIT trap restores original services, and an independent 180-second systemd timer provides recovery after shell loss. Each attempt retained its restoration record. Final before/after snapshots confirm the same service states, deployment symlink and all configuration/model/Runtime hashes.

```bash
./build.sh --test --artifacts-dir .docker-artifacts/improve-detection-tracking --jobs 16
# Board commands are run through adb shell and load /opt/ros/humble/setup.bash.
python3 analysis-tools/baseline.py --output NEW_BASELINE reference/run-001 reference/run-002 reference/run-003
python3 analysis-tools/compare.py baseline --output NEW_COMPARISON candidate/run-001 candidate/run-002 candidate/run-003
```
