# Implementation and hardware acceptance record

## Conclusion and scope

Implementation tasks for `consume-yolo-result-before-request` are complete. Both normal interactions perform result snapshot → current request/interval skip → old-result arbitration. A snapshot is one complete local value taken once from the shared result slot for the current update. Unread-result protection, pending replacement, models, production parameters, ROS interfaces, and watchdog formulas are unchanged.

This record covers 16 tasks in tasks.md. Final version-two experiment ran 9 references and 9 candidates: all 18 qualified and reclaimed resources. Three independent comparisons are `comparable`; thresholds are `not_set`, so suite status is `not_passed`, not a performance pass. No human labels establish accuracy or require a particular frame hit. At this implementation report’s close, no main-spec sync, archival, commit, deployment, or requested next-stage independent verification had been performed by this implementer.

## Implementation and controlled verification

Detecting reads once after prediction/history insertion, then requests the current image. Tracking first obtains current NanoTrack observation/source metadata, reads once, applies the original request interval, then arbitrates the saved result. Later completions cannot replace the local snapshot and there is no second read. Existing early returns for invalid input, repeated time, and tracker jumps remain unchanged.

The snapshot-handoff test hook is compiled only with `DRONE_TRACKER_DIAGNOSTIC_TESTING`. It runs after taking the snapshot and outside the detector lock. Condition-variable waits have deadlines/release paths; production gains no configuration, inference thread, or lock-order change.

| Requirement | Implementation/test evidence | Board evidence |
|---|---|---|
| Take completed result first | Two `hybrid_tracker.cpp` interactions; `testDetectingSnapshotOrder`, `testTrackingSnapshotOrderAndEarlyReturns` | All 558 successful candidate reads precede current request/skip |
| Current submission before old-result arbitration | Local `has_result` branches at both points; `testRequestOverlapsRecoveryAndRoundCleanup` | Reads with history lookup/initialization finish the current decision first |
| Handle saved result once per update | A/B identity, round, cleanup in `testCompletionAfterSnapshot` | At most one successful read per update; old/current request identities separate |
| Completion between read/submission remains protected | Empty-snapshot completion case; original `testUnreadResultAdmission` assertions retained | Zero board rejections cannot replace controlled-race proof |
| Cadence, search, early returns unchanged | Tracking interval skips, source snapshots, early returns, watchdog boundaries, Off/Basic/Detail, cleanup | Current source update precedes late-result arbitration; `RequestSkipped` retained |

Test source is `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp`; test-only access is in `diagnostic_test_access.h`. A slow-recovery test proves worker progress on the submitted current request while initialization blocks. OFF cleanup starts after update returns; no concurrent-reset contract is added. Existing image-eviction/pending-replacement checks retain identity and exactly-once accounting.

## Build record

Default x86_64 image cross-compiled ARM64, image SHA-256 `025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`. Candidate ran `./build.sh --test --artifacts-dir /home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020`: 57/57 CTests in 168.65 seconds; independent native memory-limit check 1/1 in 5.81 seconds. Diagnostics, recovery, preprocessing, and model contracts passed. Full log: [final-build-test.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/final-build-test.log).

The first full build failed because existing packaging tests required ignored `.docker-artifacts/install/drone_tracker`, absent in the new worktree. After creating an isolated empty fixture directory, the full rerun passed without assertion/default/backend changes. `.docker-artifacts/` is disposable and that fixture may need recreation. Initial failure: [first-build-test.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/first-build-test.log).

Reference fixes commit `7fbb0a2e006f12654a7db0f6d9d152244ec07e0d` in an independent clone, same image/test options. Its 4/4 regression checks and native memory-limit check passed. First build failed because shared clone objects were not mounted; completing the clone’s objects then rebuilding passed. Failure: [reference-first-build-failure.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/reference-first-build-failure.log); final logs are in `reference-build`. Both measured regression binaries came from test-enabled builds, not ordinary release outputs lacking that entry.

## Devices, authorization and historical attempts

The first SSH-only authorized board serial was `77646fe8a08c7fb8`, memory 3997632 KiB, no swap. Frozen middle/tail caches require 4149504000/5932800000 bytes, above total RAM. No OOM-risk replay ran and cache conditions were unchanged. Its service was never stopped: PID 5067, active/running/enabled. The separate blocked report remains [implementation-report-ssh-blocked.md](implementation-report-ssh-blocked.md), without deletion or relabeling as success.

The user then authorized a different near-8-GiB board over ADB. Every final ADB command specified `adb -s 5ceb49407502303d`; no other SSH alias was used. Board HZ-RK3576_WuKong Board, kernel 6.1.141-rt36, memory 8112044 KiB, no swap. Hostname and cloned machine-id match between boards, so hardware serials distinguish them. Evidence belongs to this 8 GiB board and does not replace unexecuted 4 GiB acceptance. System Runtime was 2.3.2, RKNPU driver 0.9.8.

First ADB environment wrapper omitted `AMENT_PREFIX_PATH`. Two short reference rounds completed predecode but failed node creation with `package 'drone_tracker' not found, searching: [/opt/ros/humble]`; both were unqualified. A third was interrupted during preparation without a complete verdict. No candidate/valid playback occurred. Tests stopped and production restored to PID 5293, active/running/enabled, before fixing the wrapper to existing `/home/hzhy/cvr16/install` and checking package discovery read-only. Failures/interruption remain in [first ADB evidence](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-20261020/board-aborted).

Only the experiment environment wrapper changed; program, models, input, deadlines, and formulas stayed unchanged. Fresh version-two suite `consume-yolo-result-before-request-test03-environment-v2` declared a complete new 18-run plan without selectively replacing failed rounds. All following statistics come from that complete suite; initial failures are outside its reference baseline.

## Frozen conditions and identities

Final host root is `/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020`; isolated board root `/home/hzhy/cyr20b`. Before first run, freeze config/source/binary/shared worker/input identities in [frozen-plan.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/frozen-plan.json). SHA-256 identifies actual file content, not just branch names. Candidate export includes program/test/build inputs but excludes OpenSpec reports; later report edits do not change measured source identity.

Intervals are test-03 20–35 seconds, 100–130 seconds, and 165 seconds to end, three runs per side per interval. Both use the same verified fixtures: fixed pixels, frame logs, attitude, and source-video PTS (presentation timestamps). Each fully predecodes Gray8, with P=3600-second preparation, C=10-second cleanup, supplier CPU 4, lateness 50000 microseconds, queue 32. All 18 requested CPU 4 and read back `[4]`; every maximum lateness was below 50 ms. test-02 was neither prepared nor replayed.

Controller uses CPUs 0–7; other services remain, while formal tracking stops temporarily. No extra contention or scheduler sampler was added; governors, system Runtime, and models stayed unchanged. Load `/opt/ros/humble/setup.bash`, then existing ament prefix, production bundled ROS libraries, and isolated tools. Both sides use frozen ffmpeg/ffprobe 4.4.2. Tool/algorithm fingerprints remain in each `identity.json` and frozen configuration.

| Identity | Reference | Candidate |
|---|---|---|
| Actual source | `ac6a9a144069dc14b34f2cbf15dee7ec7aafa5e0637882bcf2eba3b7300c3f23` | `cee0a286671250ee421a3e8864227d5b0daf99112d3d8d990cc5c5fd9c0e9a8f` |
| Actual regression executable | `b8bb900d209feefe0518b53a3d1ac714a6bfe6b1b471e5429766811eb0eac781` | `0afd257edcc8a01f3cc99d99069ca3d9445d800d1f37496ab07c65025ff34f4d` |
| Shared diagnostic worker | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` |

Reference/candidate source and regression-binary hashes differ; the diagnostic worker is identical. Logical models are YOLO, NanoTrack Backbone, and Head; template/search Backbone each have a file:

| Model role | SHA-256 |
|---|---|
| `tracker.yolo_engine` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| `tracker.nano_template_backbone` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `tracker.nano_search_backbone` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `tracker.nano_head` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

System `/usr/lib/librknnrt.so` SHA-256: `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`. No container Runtime replaced it.

## Production restoration

Before version two, service was active/running/enabled, PID 5293. Before each stop, create/verify independent `cyr20b-restore.timer`; timeout kills test groups and starts production. The maintenance controller also cleans/restores in finally. After all 18 runs, restore service before freezing baselines, comparing, and analyzing read-only.

Final service was **active/running/enabled, PID 6869**; restore timer inactive, no test node/worker leftover. Release remained `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`; production config SHA-256 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`. Release/config/Runtime/models/governors/boot-id match preflight. Evidence: [postcheck.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/postcheck.json), [finished.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/finished.json), [postcheck-console.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/postcheck-console.log). Version-two maintenance/run duration was 2280.636 seconds.

The parent agent briefly queried ADB read-only early, then stopped and used host control logs. Frozen workload-snapshot, event-integrity, and supply limits remained exact and all 18 runs qualified. Without continuous process inventories, these transient queries cannot be proven completely timing-neutral; disclose them with results.

## Per-run output

Valid publication means the program marked the box valid, not a human-confirmed correct target. Counts/ratios use each half-open monotonic window `[m0, m0+D)`, D being fixture duration. Cleanup publications outside it do not enter the denominator. Durations below are update/detection means in milliseconds. Superseded means a newer input replaces an accepted pending request before execution.

| Interval | Program/run | Valid/total publications | Valid ratio | Mean update ms | Mean detection ms | Initializations | Rejections | Replacements | Maximum supply lateness ms | Outside-window publications |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| wlcb-104206-020-035 | Reference/1 | 0/900 | 0.0000% | 0.0290437 | 462.281 | 0 | 32 | 834 | 2.348 | 0 |
| wlcb-104206-020-035 | Reference/2 | 0/894 | 0.0000% | 0.0295247 | 464.507 | 0 | 32 | 828 | 2.64347 | 0 |
| wlcb-104206-020-035 | Reference/3 | 0/900 | 0.0000% | 0.0290492 | 464.21 | 0 | 32 | 834 | 2.60036 | 0 |
| wlcb-104206-100-130 | Reference/1 | 0/1801 | 0.0000% | 0.0289792 | 460.902 | 0 | 65 | 1669 | 2.24949 | 0 |
| wlcb-104206-100-130 | Reference/2 | 0/1801 | 0.0000% | 0.0289729 | 461.183 | 0 | 65 | 1669 | 2.58484 | 0 |
| wlcb-104206-100-130 | Reference/3 | 0/1801 | 0.0000% | 0.0291784 | 460.915 | 0 | 65 | 1669 | 2.73321 | 0 |
| wlcb-104206-165-end | Reference/1 | 197/2516 | 7.8299% | 1.63815 | 461.153 | 3 | 84 | 2191 | 7.02997 | 2 |
| wlcb-104206-165-end | Reference/2 | 205/2530 | 8.1028% | 1.58919 | 465.812 | 2 | 85 | 2198 | 14.5946 | 1 |
| wlcb-104206-165-end | Reference/3 | 267/2530 | 10.5534% | 1.96574 | 444.093 | 2 | 82 | 2151 | 5.19014 | 1 |
| wlcb-104206-020-035 | Candidate/1 | 0/900 | 0.0000% | 0.0301751 | 471.378 | 0 | 0 | 867 | 3.22918 | 0 |
| wlcb-104206-020-035 | Candidate/2 | 0/900 | 0.0000% | 0.0302014 | 468.491 | 0 | 0 | 867 | 3.51138 | 0 |
| wlcb-104206-020-035 | Candidate/3 | 0/900 | 0.0000% | 0.0302632 | 468.069 | 0 | 0 | 867 | 2.68864 | 0 |
| wlcb-104206-100-130 | Candidate/1 | 0/1801 | 0.0000% | 0.0306501 | 467.183 | 0 | 0 | 1735 | 2.62983 | 0 |
| wlcb-104206-100-130 | Candidate/2 | 0/1798 | 0.0000% | 0.0312687 | 462.956 | 0 | 0 | 1732 | 3.01624 | 0 |
| wlcb-104206-100-130 | Candidate/3 | 0/1801 | 0.0000% | 0.0300411 | 460.859 | 0 | 0 | 1734 | 2.47152 | 0 |
| wlcb-104206-165-end | Candidate/1 | 353/2495 | 14.1483% | 2.76744 | 487.542 | 3 | 0 | 2142 | 4.08776 | 1 |
| wlcb-104206-165-end | Candidate/2 | 76/2549 | 2.9816% | 0.664393 | 465.553 | 1 | 0 | 2399 | 10.1231 | 1 |
| wlcb-104206-165-end | Candidate/3 | 317/2459 | 12.8914% | 2.77872 | 466.209 | 3 | 0 | 2129 | 9.32305 | 1 |

All per-run data/percentiles are in [round-summary.csv](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/round-summary.csv). Each tail run has 1–2 cleanup publications outside the window, retained in raw records and `all_process_*` supplements without changing canonical formulas. Lower publication counts for short reference 2 and middle candidate 2 remain; no rounds were selected by outcome.

## Three-run comparisons for each interval

Each cell is three-run median [minimum, maximum]. Difference is candidate median minus reference; relative change divides by reference. Zero reference makes relative change undefined, not infinite or zero. No initialization/catch-up event means absent duration, not zero ms. Longest invalid span/maximum publication gap use established formulas. Valid-ratio differences use ratio units; multiply by 100 for percentage points.

[supplemental-comparisons.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/supplemental-comparisons.json) retains every array, same-index round difference, and percentile median/range/relative change. Recordings are not pooled or averaged. Asynchronous repetitions make same-index differences descriptive, not a deterministic paired causal effect.

### wlcb-104206-020-035

| Metric | Reference | Candidate | Difference | Relative change % |
|---|---|---|---:|---:|
| Valid publications | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Total publications | 900 [894, 900] | 900 [900, 900] | 0 | 0 |
| Valid ratio | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Longest invalid output ms | 14985.7 [14985.5, 14985.7] | 14985.6 [14985.5, 14985.6] | -0.108697 | -0.000725338 |
| Maximum publication gap ms | 18.1111 [18.073, 127.709] | 18.4801 [18.3506, 18.6422] | 0.369018 | 2.03753 |
| Mean update ms | 0.0290492 [0.0290437, 0.0295247] | 0.0302014 [0.0301751, 0.0302632] | 0.00115212 | 3.96609 |
| Update p95 ms | 0.038793 [0.038035, 0.040323] | 0.040752 [0.039623, 0.040846] | 0.001959 | 5.04988 |
| Mean detection ms | 464.21 [462.281, 464.507] | 468.491 [468.069, 471.378] | 4.28093 | 0.922197 |
| Detection p95 ms | 464.723 [462.553, 466.567] | 471.157 [470.956, 484.068] | 6.43361 | 1.3844 |
| Initializations | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Mean initialization ms | No event/undefined | No event/undefined | No event/undefined | No event/undefined |
| Catch-up attempts | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Catch-up steps | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Mean catch-up ms | No event/undefined | No event/undefined | No event/undefined | No event/undefined |
| Mean queue wait ms | 8.66828 [8.39593, 8.73373] | 7.24142 [6.86828, 7.25726] | -1.42686 | -16.4607 |
| Rejections | 32 [32, 32] | 0 [0, 0] | -32 | -100 |
| Unread-result rejections | 32 [32, 32] | 0 [0, 0] | -32 | -100 |
| Replacements | 834 [828, 834] | 867 [867, 867] | 33 | 3.95683 |
| Preparation duration ms | 37127.4 [37108.1, 37163.9] | 37438.4 [37403.6, 37553.7] | 311 | 0.837656 |
| Preparation peak RSS MiB | 2064 [2063.98, 2064] | 2063.98 [2063.96, 2064] | -0.015625 | -0.000757027 |
| System available after preparation MiB | 5605.98 [5604.89, 5607.7] | 5608.5 [5608.35, 5608.54] | 2.52734 | 0.045083 |

### wlcb-104206-100-130

| Metric | Reference | Candidate | Difference | Relative change % |
|---|---|---|---:|---:|
| Valid publications | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Total publications | 1801 [1801, 1801] | 1801 [1798, 1801] | 0 | 0 |
| Valid ratio | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Longest invalid output ms | 29995.3 [29994.8, 29995.6] | 29995.1 [29994.8, 29995.1] | -0.247773 | -0.000826039 |
| Maximum publication gap ms | 18.0175 [17.9892, 18.263] | 18.2237 [18.0535, 72.1565] | 0.206277 | 1.14487 |
| Mean update ms | 0.0289792 [0.0289729, 0.0291784] | 0.0306501 [0.0300411, 0.0312687] | 0.00167096 | 5.76606 |
| Update p95 ms | 0.037652 [0.037047, 0.03907] | 0.039884 [0.038868, 0.042099] | 0.002232 | 5.92797 |
| Mean detection ms | 460.915 [460.902, 461.183] | 462.956 [460.859, 467.183] | 2.04098 | 0.442811 |
| Detection p95 ms | 463.987 [463.081, 464.09] | 465.588 [462.674, 470.393] | 1.60091 | 0.345034 |
| Initializations | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Mean initialization ms | No event/undefined | No event/undefined | No event/undefined | No event/undefined |
| Catch-up attempts | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Catch-up steps | 0 [0, 0] | 0 [0, 0] | 0 | No event/undefined |
| Mean catch-up ms | No event/undefined | No event/undefined | No event/undefined | No event/undefined |
| Mean queue wait ms | 8.58091 [8.23703, 8.76913] | 8.24766 [7.5309, 8.29258] | -0.333256 | -3.88369 |
| Rejections | 65 [65, 65] | 0 [0, 0] | -65 | -100 |
| Unread-result rejections | 65 [65, 65] | 0 [0, 0] | -65 | -100 |
| Replacements | 1669 [1669, 1669] | 1734 [1732, 1735] | 65 | 3.89455 |
| Preparation duration ms | 73692.9 [73601.8, 73827.2] | 74125.9 [74066.2, 74164.1] | 433 | 0.587574 |
| Preparation peak RSS MiB | 4050.37 [4049.17, 4050.39] | 4050.38 [4050.37, 4050.39] | 0.0078125 | 0.000192884 |
| System available after preparation MiB | 3613.96 [3612.41, 3614.54] | 3614.56 [3613.98, 3614.56] | 0.597656 | 0.0165374 |

### wlcb-104206-165-end

| Metric | Reference | Candidate | Difference | Relative change % |
|---|---|---|---:|---:|
| Valid publications | 205 [197, 267] | 317 [76, 353] | 112 | 54.6341 |
| Total publications | 2530 [2516, 2530] | 2495 [2459, 2549] | -35 | -1.3834 |
| Valid ratio | 0.0810277 [0.0782989, 0.105534] | 0.128914 [0.0298156, 0.141483] | 0.0478865 | 59.099 |
| Longest invalid output ms | 36800.9 [36369.9, 36888.4] | 29243.1 [22212.8, 41605.6] | -7557.8 | -20.537 |
| Maximum publication gap ms | 475.755 [461.366, 480.43] | 491.396 [422.128, 539.138] | 15.6409 | 3.28758 |
| Mean update ms | 1.63815 [1.58919, 1.96574] | 2.76744 [0.664393, 2.77872] | 1.12929 | 68.9367 |
| Update p95 ms | 15.5651 [15.5167, 15.7302] | 15.9086 [0.048035, 16.2837] | 0.343476 | 2.2067 |
| Mean detection ms | 461.153 [444.093, 465.812] | 466.209 [465.553, 487.542] | 5.0563 | 1.09645 |
| Detection p95 ms | 539.941 [539.466, 542.767] | 506.076 [476.22, 543.947] | -33.8648 | -6.27194 |
| Initializations | 2 [2, 3] | 3 [1, 3] | 1 | 50 |
| Mean initialization ms | 7.13824 [7.01518, 7.70635] | 7.60055 [7.11845, 8.73013] | 0.462309 | 6.47651 |
| Catch-up attempts | 2 [2, 3] | 3 [1, 3] | 1 | 50 |
| Catch-up steps | 42 [42, 58] | 63 [25, 75] | 21 | 50 |
| Mean catch-up ms | 331.315 [304.568, 333.897] | 395.323 [395.154, 397.383] | 64.0089 | 19.3197 |
| Mean queue wait ms | 8.80394 [8.72116, 10.5532] | 18.0852 [9.10438, 25.0769] | 9.28128 | 105.422 |
| Rejections | 84 [82, 85] | 0 [0, 0] | -84 | -100 |
| Unread-result rejections | 84 [82, 85] | 0 [0, 0] | -84 | -100 |
| Replacements | 2191 [2151, 2198] | 2142 [2129, 2399] | -49 | -2.23642 |
| Preparation duration ms | 106655 [106598, 106714] | 106866 [106523, 107858] | 211 | 0.197834 |
| Preparation peak RSS MiB | 5741.42 [5741.4, 5741.45] | 5741.46 [5722.55, 5741.46] | 0.0390625 | 0.000680363 |
| System available after preparation MiB | 1917.58 [1916.84, 1917.89] | 1917.12 [1916.03, 1918.12] | -0.453125 | -0.0236301 |

Short/middle intervals have zero valid boxes/initializations on both sides. Tail candidate ratios are 14.1483%, 2.9816%, 12.8914%; references 7.8299%, 8.1028%, 10.5534%. Candidate 2 is below same-index reference, and candidate median tail-update duration is higher. Retain this variability; reduced rejection or a rising median does not establish tracking quality.

Preparation RSS is Linux resident physical memory for that process, not system-available memory or an all-process sum. Pixel caches are 2073600000, 4149504000, 5932800000 bytes. Minimum available memory among 4473 maintenance samples was 1726.504 MiB, a discrete observation rather than continuous-run extremum. Preparation memory and full telemetry remain, without compression/on-demand decoding changing conditions.

## Middle-interval request identities for fixture 388/389

fixture_frame_id starts at zero and is not tracker_frame_id/request_id. Fixture 388 corresponds to source frame 6390, PTS 106465935 microseconds; 389 to source 6391, PTS 106482584 microseconds. Replacement below means accepted submission later superseded before execution.

| Program/run | Fixture 388 | Fixture 389 | Initializations/valid boxes in this run |
|---|---|---|---|
| Reference 1 | request 389, accepted/executed/NoTarget, highest stage score 0.291989 | request 390, UnreadResult rejected | 0 / 0 |
| Reference 2 | request 389, accepted/executed/NoTarget, highest stage score 0.291989 | request 390, UnreadResult rejected | 0 / 0 |
| Reference 3 | request 389, UnreadResult rejected | request 390, accepted then superseded, unexecuted | 0 / 0 |
| Candidate 1 | request 389, accepted then superseded, unexecuted | request 390, accepted then superseded, unexecuted | 0 / 0 |
| Candidate 2 | request 386, accepted then superseded, unexecuted | request 387, accepted then superseded, unexecuted | 0 / 0 |
| Candidate 3 | request 389, accepted/executed/NoTarget, highest stage score 0.291989 | request 390, accepted then superseded, unexecuted | 0 / 0 |

Earlier input-selection skips in candidate 2 explain its different internal-request/fixture offset. All three candidates accepted fixture 389 but none executed it. Acceptance is not a target hit. No inference score for that frame exists in these runs; historical approximately 0.537 cannot be substituted. Threshold stayed 0.5. All six middle runs had zero initialization; medians do not conceal that distribution.

Complete joins: [medium-388-389.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/medium-388-389.json), [critical-events.csv](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/critical-events.csv). Each run retains `frames.csv`, `submissions.csv`, `calls.csv`, `messages.csv`, `input.jsonl`, `events.jsonl`, tracing supply/selection/admission/execution/completion/read through initialization/publication.

## Raw data, order audit and baselines

[call-order-audit.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/call-order-audit.json) covers 571 reference and 558 candidate successful reads. Reference current decisions precede reads; candidate reads precede current request/interval skip. Audit uses monotonic time and strict producer_seq, the same writer’s increasing identity. Every update has at most one successful read. Old-result history/initialization follows current decisions and its source identity stays separate from the current request. No ordering violation occurred. Empty reads emit no `ResultRead`, so code/controlled tests prove empty-snapshot boundaries rather than successful board reads.

Per-run completion-file checksums pass in [integrity.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/integrity.json). Independent raw `.dtd` CRC validation, decoding, input joins, and metric recomputation cover 327201 events across 18 runs. Normalized events match retained files record-for-record, and all 18 metrics match original JSON exactly. Recompute used board-matching Python 3.10.12; internal tuples were compared after JSON array encoding. Read-only analysis overwrote nothing. Script: [recompute_metrics.py](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/recompute_metrics.py); result: [metric-recomputation.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/metric-recomputation.json).

Formal freeze/comparison each ran once after restoration and exited 0. Baselines remain read-only with unchanged hashes before/after comparison. Full manifest: [baseline-integrity.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/baseline-integrity.json). The three `baseline.json` SHA-256 values are:

| Case | SHA-256 |
|---|---|
| wlcb-104206-020-035 | `d93f57307e3a5bed5e67e172fdaf745702dbac75034bee02e6e8483c08df3046` |
| wlcb-104206-100-130 | `8b5cb2515e90aa59edba0a7f3a3e7fc128a64df60d905f70c52ac59fae3f595d` |
| wlcb-104206-165-end | `422b2502a6d77ad6bf9754ab40474287679980b59eae843b20790da97de8fe1b` |

Central report: [report.md](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/report.md). Three independent reports:

- [wlcb-104206-020-035](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/wlcb-104206-020-035/report.md); matching comparison.json and comparison.csv are in the same directory.

- [wlcb-104206-100-130](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/wlcb-104206-100-130/report.md); matching comparison.json and comparison.csv are in the same directory.

- [wlcb-104206-165-end](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/wlcb-104206-165-end/report.md); matching comparison.json and comparison.csv are in the same directory.

## Consistency and unverified limits

Proposal scope, five design decisions, five delta requirements, and tasks.md were checked individually. Evidence covers both source interaction points, controlled concurrency/lifecycle checks, 18 runs of distinct real binaries, per-run identities, and middle-interval joins. Strict OpenSpec validation exited 0 with `Change 'consume-yolo-result-before-request' is valid`; log: `/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/openspec-final-validation.log`. Checked tasks reflect actual completion; historical failures remain.

No Jetson hardware, camera capture, flight, labeled accuracy, or long production-load checks ran. clang-tidy/clangd was neither configured nor executed. Cross-builds do not replace board results, and the 8 GiB board does not establish cache viability on 4 GiB. Bounded environment snapshots prove neither long-term leak freedom nor absence of scheduling interference. Without performance thresholds, claim no performance pass, cross-video average improvement, or guaranteed specific-frame hit.

Final raw evidence, frozen requests, failed attempts, restoration, and limits were retained; production remained on its pre-maintenance version. This report does not automatically authorize later sync, archival, commit, or production release.
