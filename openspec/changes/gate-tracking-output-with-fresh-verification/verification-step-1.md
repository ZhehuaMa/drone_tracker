# Verification step 1 — 2026-11-10

Using change: `gate-tracking-output-with-fresh-verification`. The explicit change selection controls this run.

## Initial verification

| Dimension | Result |
| --- | --- |
| Completeness | 42/43 tasks complete; all 53 added/modified requirements mapped to implementation. Six requirement renames are also modified in their deltas and are assessed against that final text. |
| Correctness | Six implementation/reporting divergences below. Production-board recovery restoration is not verified. |
| Coherence | Shared model/platform boundaries and single-worker scheduling retained. Reporting clock and historical-metric inconsistencies require correction. |

Initial counts: **1 CRITICAL, 6 WARNING, 0 SUGGESTION**.

### CRITICAL

1. **Task 8.3 incomplete.** `tasks.md:69` requires actual-model recovery-verification and valid-admission durations. The retained final video episode did not restore output. Complete an identified production-board recovery run and retain both measured durations before marking this task done. The existing report correctly leaves these unavailable; controlled protocol tests do not substitute for this evidence.

### WARNING

1. **Expired permission can be renewed before timer servicing.** `src/hybrid_tracker/output_validity.h`, `OutputValidity::confirm`, checks new evidence but does not expire the previous permission under the same lock. A fresh result can therefore renew the old instance at its expired deadline. Revoke/register loss before accepting any such renewal and test the exact boundary without calling `expire()` first.
2. **Original source diagnostic fields were lost.** `src/hybrid_tracker/hybrid_tracker.cpp`, `SourceContext`, no longer sets historical image-availability/generation values or prediction metadata. Arbitration also omits overlap/threshold diagnostics. Restore the submitted snapshot's original metadata and retain active overlap diagnostics without replacing it with replay evidence.
3. **Suppressed samples can count as valid admissions.** `src/drone_tracker_node.cpp`, `publishTrackingObservation`, records successful `ValidAdmission` before checking the builder's strictly increasing timestamp guard. A timestamp-suppressed image can incorrectly become the first recovery admission. Distinguish permission eligibility from actual increasing-time pair admission while preserving independent state transitions and LOS resets.
4. **OFF metrics count trust-loss events.** `tests/tracking_regression/metrics.py`, `off_notification_count`, counts every `ControlBboxEnd`, including the newly introduced loss notifications. Add an explicit loss-pair marker and separate metric, preserving the existing OFF definition and historical formats.
5. **Missing replay recovery values are sentinel strings.** `src/replay/replay_report.cpp`, `summary`, replaces null sentinels only for timing summary fields. Recovery `verification_ns` and `to_verification_ms` remain strings. Serialize missing recovery values as JSON null and test the raw report.
6. **Replay measurement windows use another clock origin.** `src/replay/replay_report.cpp`, `frame`, records windows from relative `submit_ns`, while evidence and recovery fields use absolute monotonic execution time. Retain window endpoints in the same execution domain and test their relation to per-image anchors and decision times.

## Coverage evidence

The four capability deltas, proposal, design, tasks, shared tracker/detector, value-only validity state, node observation/timer paths, replay parser/runner/report, diagnostics encoder/exporter, regression collectors/metrics/comparison, and affected tests/docs were inspected. Mapping includes provisional acquisition and replay association, stale-seed limits, unique candidates, coordinate conversion, trusted-filter separation, counter authority, independent expiry, ordered paired loss, OFF/ON isolation, exact clock evidence, availability/recovery reporting, and historical compatibility.

The existing dated validation report retains the configured ARM64 68/68 suite, final affected checks, native controlled board checks, model/input/runtime identities, and the incomplete actual-model recovery episode. Full Jetson hardware/SDK execution is unavailable and is explicitly outside this run's evidence. Production recovery restoration remains not verified rather than passing.

## Apply outcome

All six WARNING groups received focused fixes and regression tests:

- `output_validity.h` now samples the injected production decision clock under its short lock during confirmation. It revokes an expired previous permission and registers the loss before any renewal can occur. Exact-deadline and after-deadline tests omit prior timer servicing deliberately.
- `hybrid_tracker.cpp` restores submitted source-pixel availability, generation, original prediction and validity flags, history bounds, and overlap/threshold diagnostic fields. Replay comparison provenance remains separate. A real submitted-snapshot test checks the retained fields.
- `drone_tracker_node.cpp` excludes timestamp-suppressed pairs from successful `ValidAdmission` records without changing independent state transitions or permission. A real-node test records successful admission followed by a suppressed sample and requires `[true,false]` admission evidence.
- Loss pairs now carry `TrustLossObservation` (262144) in addition to `ControlObservation`. Regression metrics preserve OFF counts and expose a separate loss count. Historical formats report that new loss metric unavailable. A paired-record test covers both kinds together.
- Replay missing recovery fields serialize as standard JSON null. A separate raw-output test checks both missing fields and rejects leaked sentinels. OpenCV 4.5's test reader cannot parse JSON null; repository `run.json` readers using it exist only in replay tests. Production code writes the report, and regression tooling uses JSON parsers. Unavailable timing fields already used JSON null before this fix, so the standard-JSON output contract is preserved.
- Replay measurement windows now use immutable image admission anchors and decision times in the same monotonic domain as recovery evidence. A report test checks 5000000000/5010000000 ns endpoints despite a separately relative 25 ns scheduler submission value.

Changed files for this apply step: `src/hybrid_tracker/output_validity.h`, `src/hybrid_tracker/hybrid_tracker.cpp`, `src/diagnostics/event.h`, `src/drone_tracker_node.cpp`, `src/replay/replay_report.cpp`, `tests/tracking_regression/metrics.py`, their focused tests in `output_validity_test.cpp`, `diagnostic_tracker_test.cpp`, `ros_video_test.cpp`, `replay_test.cpp`, and `test_observations.py`, plus `src/diagnostics/README.md`, the regression metrics guide, this report, and the dated validation report's snapshot notice.

### Validation after apply

Platform: x86_64 compiler host with the configured SDK image `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`. Output is ARM64; target execution in these checks used the workflow's QEMU emulator. No model identities or target-board inputs were changed.

| Check | Result |
| --- | --- |
| Configured build backend functions, `--test`, `DRONE_TEST_REGEX=ros_video\|output_validity\|diagnostic_tracker\|tracking_regression_python\|observation` | ARM64 package build exit 0; 9/9 selected CTest checks passed, 40.90 s. Required native resource precheck also passed. |
| Standalone SDK CMake build and replay/validity CLI subset | Build succeeded; CLI and validity checks passed. Initial replay test exposed OpenCV's test-reader null limitation; the separate raw JSON test resolves that fixture limitation. Final replay test 1/1 passed, 3.85 s, exit 0. |
| Preferred conda Python, `unittest discover` for observation/validity/existing metric suites | 8 + 5 + 17 tests passed, all exit 0. The configured SDK regression Python suite also passed above. |
| `git diff --check` | Exit 0. |
| C++ lint/language-server checks | Not run: no configured lint entry or compile database was found; `clang-tidy` and `clangd` are absent on the host and selected image. |
| Target-board checks of these follow-up fixes | Not run. Earlier board evidence retains its original program hash and snapshot meaning. |

Logs and the follow-up node binary are retained under `/home/mzh/nickluo/validation-retained/fresh-verification-20261110/`, with `verify-step1-` names. The code/configuration/test manifest `verify-step1-source-files.json` has SHA-256 `968dcdbc2e7c95f3863a2e4799f232dd89680551ff88f1ca6d6007f736640dab`. Documentation and summary edits are outside this code manifest. Base revision remains `91d75e1` with the implementation and these fixes applied.

### Remaining blocker and final assessment

Task 8.3 remains incomplete. The retained actual-model recovery episode did not restore output, so its successful local-verification and valid-admission durations are unavailable. No further board profiles were run during this step. The task checkbox remains open; defaults and acceptance requirements were not weakened.

The initial verification counts remain **1 CRITICAL, 6 WARNING, 0 SUGGESTION**. Six warning groups were fixed and their targeted tests pass. Those fixes are **not a fresh independent verification verdict**. The unresolved known issue is the CRITICAL task-8.3 evidence gap. Production recovery restoration and full Jetson hardware/SDK execution remain not verified. Fix the remaining task before archiving; no all-clear or performance acceptance is claimed.
