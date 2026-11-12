# Verification report: gate-tracking-output-with-fresh-verification

## Scope and initial verdict

Date: 2026-11-12. Repository: `/home/mzh/.codex/worktrees/a6cf/drone_tracker`.
Source base: `1474c7f1e4376c3c560758ece60835a5d7f6cc00` plus the working-tree implementation described in [validation-2026-11-12.md](validation-2026-11-12.md).

This is the independent audit before the fixes recorded below. Initial issue counts: **0 CRITICAL, 2 WARNING, 0 SUGGESTION**. A later fresh agent must verify any fixes; this report does not infer a zero-warning verdict from applied changes.

| Dimension | Result |
| --- | --- |
| Completeness | OpenSpec reports 52/52 tracked tasks checked; implementation/evidence mapping exists for 77 ADDED/MODIFIED requirements. The single REMOVED watchdog requirement was checked against its usable main-spec baseline, and no active frame-count cleanup branch remains. Six RENAMED requirements also have MODIFIED bodies and are checked under those bodies. |
| Correctness | All four deltas and their 130 scenarios were reviewed against code, tests and dated evidence. One cancellation race can turn cancelled coverage into an adverse verdict. Required fallback/restart scenarios lack direct controlled coverage. |
| Coherence | Shared model/platform boundaries, source identity, clock mappings, separate permission, loss ordering and historical metric definitions are retained. The cancellation race diverges from the design's neutral-cancellation rule. No unrelated pattern issue is raised. |

## Requirement and scenario mapping

| Capability | ADDED/MODIFIED requirements | Scenarios | Principal implementation and evidence |
| --- | ---: | ---: | --- |
| `tracking-output-validity` | 32 | 43 | `src/hybrid_tracker/output_validity.h`; `HybridTrackerImp::update()` and `catchupTracker()`; `output_validity_test.cpp`, `diagnostic_tracker_test.cpp`; ROS expiry/publication tests; final board recovery episodes. |
| `detection-tracking-arbitration` | 24 | 49 | `yolo_detector.h/.cpp`; source snapshots, replay measurements and plan ledger in `hybrid_tracker.cpp`; geometry, preprocessing, scheduler and round tests. The cancellation/fallback coverage findings below apply here. |
| `target-observations` | 12 | 22 | `publishTrackingObservation()`, `drainTrustLoss()` and `notifyObservationOff()` in `src/drone_tracker_node.cpp`; `TargetObservationBuilder`; controlled ROS loss-ordering tests and paired bag audits. |
| `rk3576-tracking-regression` | 9 | 16 | Format-4 diagnostic records/inspector, `scheduler_metrics.py`, `validity_metrics.py`, replay report version 3, historical-format tests, retained exact-pixel crop and isolated scheduler/combined comparisons. |

Initialization remains provisional; B's replay measurements are distinct from A's original request snapshot. Freshness is anchored to the original image, and publication rechecks actual age and identity. Partial misses remain neutral, completed adverse plans act once, and the removed no-result watchdog has no active branch. Current tests exercise D1/D2 overlap, the D32/B/T41/D41 sequence, expired clocks, candidate deadlines, T/T/D service, unread capacity, and independent loss-before-recovery ordering.

## Findings

### CRITICAL: 0

No missing requirement implementation or unchecked task was found. The checked tasks do not override the correctness findings below.

### WARNING: 2

1. **A cancelled plan's final in-flight call still advertises completed coverage.** At the audited source, `cancelPlan()` (`src/hybrid_tracker/yolo_detector.cpp:292`) drops the plan and emits cancellation, but the active job's `m_complete_coverage` was already set at dispatch (`:440`). Completion (`:478–492`) publishes that original flag even if a history-loss control cancelled the plan during the call. `HybridTrackerImp::update()` (`src/hybrid_tracker/hybrid_tracker.cpp:659`) can therefore classify that fresh result with retained same-image metadata as completed no-target/disagreement and increment or revoke. The deltas require cancelled stages/plans to remain neutral. Recommendation: carry cancellation disposition through publication without freeing active device storage; withhold completed adverse coverage and candidate adverse authority for that cancelled plan, retain original identities and allowed positive seed/confirmation uses, and add a barrier-controlled last-stage cancellation regression.

2. **The controlled scheduler suite lacks direct first-miss fallback and rotated-restart assertions.** `testOneCallSchedulingAndCapacity()` (`src/hybrid_tracker/tests/diagnostic_tracker_test.cpp:1609`) starts with an existing discovery plan. It therefore does not exercise a targeted call creating the sole fallback slot, later misses failing to add another slot, nonmatching-positive feedback releasing that fallback, or cancellation rotating the next plan's first tile. These are explicit task 3.8/3.9 and design scenarios. Recommendation: add deterministic tests that inspect immutable initial-job linkage, fixed source, one-slot ownership, asynchronous partial feedback, cancellation during a call, rotated tile order and no cross-image coverage.

### SUGGESTION: 0

No suggestions are raised.

## Evidence and limits

The retained final apply evidence reports `ROS_DOMAIN_ID=212 ./build.sh --test` exit 0, 68/68 CTest checks, focused 5/5, replay SDK 3/3 and native production-board 5/5. The audit read the final report and inspected its implementation/test references; it does not relabel prior passing tests as testing any future fix.

The identified production board, system Runtime 2.3.2 checksum, driver 0.9.8, exact model/input hashes, separate source/execution clocks and bounded run commands are recorded. Formal combined availability 3/724, live-camera availability 0/501, and three completed of five temporary recovery episodes are reported directly. No versioned performance threshold exists, so these low values are limitations rather than an invented threshold failure. Accuracy, angular accuracy and flight safety remain unmeasured.

Finite-video EOF seals its already-ended recording before a later OFF notification; the report explicitly excludes that absent post-EOF pair from recorded-OFF counts. Controlled active-round OFF/loss ordering and camera OFF pairs have evidence. The audit does not introduce a new requirement to keep an EOF-ended bag open indefinitely. Physical Jetson SDK/hardware and clang tooling remain unavailable, as recorded in the apply report; no physical Jetson or lint pass is claimed.

Initial assessment: **No critical issues; 2 warnings require correction in this verify-fix run.** No proposal/design/spec/task check was skipped. Physical Jetson and unavailable lint execution remain explicit validation limits.

## Apply follow-up

Both warnings were applied under the repo-local `openspec-apply-change` skill. Tasks 3.8, 3.9 and 3.10 were reopened while correcting their behavior/coverage and restored only after the checks below passed. No requirement, timeout default, profile meaning, or acceptance criterion was weakened.

The detector now keeps four bounded value-only cancellation records. They hold plan identities and reasons, never pixels or SDK storage. Publication and take sample those records without changing original source or candidate provenance. A cancelled active or unread result cannot supply completed adverse coverage, an ambiguity-based adverse action, or a second parent verdict. Its separately eligible positive evidence still follows seed or unique same-image confirmation checks. Active device calls finish before their input/session storage is released.

Added barrier-controlled tests cover:

- Cancellation during the seventh, final inference call, and cancellation after a positive result is already unread.
- Fresh retained comparisons staying neutral for cancelled disagreement and ambiguity, while unique positive confirmation remains eligible.
- Exactly one initial targeted-miss fallback, immutable initial-job linkage, later misses adding no plans, and complete fixed-image coverage.
- Nonmatching-positive feedback enabling fallback without a full-decision barrier.
- Cancelled restarts using a new source/ledger and a rotated first tile.
- Cancelled plans receiving no later second parent verdict from stage evidence.

Changed implementation: `src/hybrid_tracker/yolo_detector.h`, `src/hybrid_tracker/yolo_detector.cpp`, `src/hybrid_tracker/hybrid_tracker.cpp`. Changed tests: `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp`. Updated guides: `src/hybrid_tracker/README.md`, `src/diagnostics/README.md`. This report and the three reopened task boxes are the only other files changed by this iteration. The coordinator separately owns the run summary.

### Validation commands and results

All ROS processes used the randomly selected session domain `ROS_DOMAIN_ID=212`. The existing configured amd64 SDK image remained `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`; it generated ARM64 output without changing backends.

| Check | Command and outcome |
| --- | --- |
| Cancellation reproduction before production fix | `ROS_DOMAIN_ID=212 ./build.sh --test`: exit 8; 67/68 checks passed. The only failure was `cancelled last in-flight tile advertised completed adverse coverage`. `fresh-verify-step1-repro.log` and `fresh-verify-step1-reproduction-source.patch` retain the case. |
| Focused correction | The existing `scripts/lib/build_backend.sh` workflow, parsed with `--test` and `DRONE_TEST_REGEX=diagnostic_tracker_tests\|output_validity_tests\|tracking_geometry_tests\|observation_output_tests\|recovery_regression_tests\|tracking_regression_python`: exit 0, 6/6 checks passed. A second focused run after fixture repair passed 6/6 in 16.13 s. These runs predate the final parent-verdict assertion. |
| Final exact frozen source | `ROS_DOMAIN_ID=212 ./build.sh --test`: exit 0; **68/68 CTest checks passed in 193.19 s**, plus the workflow's native resource-limit check. |
| Native identified-board controlled checks | Five final ARM64 executables under `timeout --signal=TERM --kill-after=3s 60s`: output validity, geometry, observation output, target observation and diagnostic tracker with the diagnostic inspector argument. **5/5 exited 0**. These are native controlled-model tests, not actual NPU inference acceptance. |
| Post-fix actual-model video | Retained `board_probe.py` with `FRESH_BIN_DIR=.../verify-step-1/bin`, `FRESH_RUN_SUFFIX=-verify-step1-temp` and `timeout --signal=INT --kill-after=5s 175s`: node and inspector both exited 0; 1,414 observer messages. |
| Existing diagnostic/bag analysis | Frozen collector/metric modules scanned the retained native records without scan issues or duplicate stage dispositions. No result overwrites or at/after-deadline valid admissions were found. Independent raw-header bag checks found zero paired timestamp mismatches and preserved loss-before-recovery ordering. |

The first extended verdict test reached its unrelated 100 ms confirmation deadline exactly. That fixture-only failure is retained in `fixture-expiry-failure.log` and `native-board-fixture-failure.log`. The test now uses an explicitly longer test lifetime to isolate plan verdicts; exact-expiry behavior remains covered separately. The final production settings are unchanged. Every inference barrier waits at most two seconds; no unbounded test wait or device stall was observed.

### Post-fix board scope

Read-only preflight reidentified `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 ARM64 and kernel `6.1.141-rt36`. System `/usr/lib/librknnrt.so` still has SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`, matching Runtime 2.3.2; driver is 0.9.8. Model and input hashes match the original dated report. Every noninteractive board command sourced `/opt/ros/humble/setup.bash`. A preliminary service-query command used the nonexistent short name `diagnostics`; the retained native logs use the actual `drone-tracker-diagnostics.service` name and show all four services active before/after.

The new actual-model run uses the unchanged temporary confidence 0.40 / confirmation 200 ms profile and identified November 4-derived input. It is not the formal default profile. The 17.921136965 s window contains 696 consumed images: **78 valid, 614 invalid, 4 suppressed and 0 absent** (11.207% bbox availability). Four recovery episodes include **two complete and two incomplete**. Complete revocation-to-verification / first-valid-admission times are 47.842181/47.872380 ms and 158.269022/158.305133 ms. Both topic attempts succeeded for those admissions. `actual-model-metrics.json` retains exact records and denominators.

The paired-bag audit found 697 bbox/LOS pairs and all four trust-loss pairs in both bags. Its only absent diagnostic pair is the previously documented first-round post-EOF OFF pair after that finite-video bag had already sealed. This remains distinct from loss-event recording and active-round OFF behavior. No subscriber-delivery claim is made.

Board services were restored active, including `drone-tracker`, `drone-tracker-diagnostics`, `drone-interceptor` and `apm-bridge`. The release link, model files, system Runtime and service definitions were not changed. No camera probe, crop retuning or new formal-profile performance comparison was performed in this follow-up. Original formal 3/724 and live-camera 0/501 results retain their original binary scope. No overall performance, accuracy, angular-accuracy or flight-safety pass is inferred. Physical Jetson and clang tools remain unavailable.

### Retained evidence and iteration outcome

Follow-up evidence is retained at `/home/mzh/nickluo/validation-retained/fresh-verification-20261112-a6cf/verify-step-1/`, including failed and passing logs, frozen source/test hashes, final binaries, raw diagnostic journals, closed bags, metrics and `evidence-sha256.json`. The original apply evidence inventory is unchanged. Retention paths and hashes do not promise indefinite storage.

Both reported warnings have concrete fixes and passing checks, with no remaining application blocker. **Initial verification counts remain 0 CRITICAL, 2 WARNING, 0 SUGGESTION. Post-fix independent counts are not verified.** A fresh iteration must verify this source before the coordinator can mark the loop complete.
