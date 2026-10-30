# Tasks

This checklist records `pin-regression-thread-affinity`. CPU affinity is the set of cores on which a thread may execute. Placement observation records both that mask and the actual CPU at each work point. Implementation uses algorithm behavior from `4871442dda37d3d64087fda0b7756692c8f7e473`. Tasks required completion before archival; creating the plan itself did not establish implementation or hardware results.

## 1. Regression configuration and policy isolation

- [x] 1.1 Add optional regression-policy and common-observation configuration in `tests/tracking_regression/configuration.py`, run schema 3, and suite schema. Parse version-1 `cpu_affinity` and `cpu_observation` into immutable snapshots. Preserve old configurations and give full field paths for invalid fields.
- [x] 1.2 Isolate regression roles from production policy in `run_batch.py` and launch adapters. Generate private policy; validate online cores, initial masks, effective cgroups, and external-policy conflicts. Inspectable outputs must show no write to `deployment/cpu-affinity.yaml` or priority/frequency changes.
- [x] 1.3 Cover omitted fields, supplier-only settings, valid mappings, boolean/negative/offline cores, conflicting ranges, and rejected environment overrides in `test_configuration.py`, `test_supplier_cpu.py`, or neighboring checks. New and existing checks pass.
- [x] 1.4 Document single-file activation in `tests/tracking_regression/README.md` and examples: supplier 4, Tracking 5, YOLO 6, logger 7, control 1, supervision 0–3, and disabling. Examples pass the same validator.

## 2. Safe startup, release and cleanup

- [x] 2.1 Prevent supplier inheritance of Tracking's single core in `realtime_source.{h,cpp}`. Add a test-only early-ready handshake; the source owns and parks the bound supplier. No input records or playback clock appear before or after readiness until release.
- [x] 2.2 Apply roles in legal order in regression `main.cpp` and `run_batch.py`: prepare and ready the supplier within the validated initial mask, then bind control, construct models, start Tracking, and release playback. Read back supplier 4, control 1, Tracking 5, YOLO 6, logger 7; supervision enters 0–3 before release.
- [x] 2.3 Retain cleanup for preparation/model/readiness failure, cancellation, and repeated stop in source/control paths. Keep failure causes and reclaim created threads/isolated children. No-policy mode retains its original lifecycle.
- [x] 2.4 Extend `realtime_source_test.cpp`, `node_round_test.cpp`, and controller checks for supplier 4/Tracking 5, initial mask excluding 4, cancellation before model readiness, model failure after supplier readiness, EOF, and repeated stop. Prove no early supply or leftover threads.
- [x] 2.5 Explain readiness versus playback in `diagnostic_contract.md` or README: cache completion, supplier readiness, model readiness, role confirmation, clock creation, release, cleanup. Documentation matches observed output.

## 3. Bounded CPU placement evidence

- [x] 3.1 Add a default-disabled work-point receiver at existing measurement/diagnostic and necessary node/tracker/YOLO boundaries. Cover update start/end, recovery initialization, every catch-up step, key YOLO stages, and necessary publication points. Export PID/TID, role, monotonic time, actual CPU, round/update/request identities without changing decisions or event layout.
- [x] 3.2 Bound hot-path observation with preallocated per-writer capacity. Export capacity, rules, cost, overflow/loss after business shutdown. Review confirms no hot-path allocation, file writes, or thread scans, and no overwriting when full.
- [x] 3.3 Add external thread snapshots at most once per second and at model-ready/playback-ended boundaries. Preserve PID/TID, name, mask, scheduling, and visible CPU. Distinguish registered roles, unknown library threads, preparation decoder threads, and threads exiting during reads.
- [x] 3.4 Add evidence schema 1 and extend `collect.py` to check requested/actual masks, point identities, pre-clock setup, capacity, and missing evidence. Binding errors, required-point gaps, and overflow fail placement. Existing CRC/event-integrity failures remain failures.
- [x] 3.5 Test synthetic update/recovery/YOLO, cross-writer evidence, overflow, unknown threads, exit races, and wrong masks. Observation is symmetric; absent records cannot invent CPU proof.
- [x] 3.6 Explain allowed masks, call-time CPU, and external last_cpu in `metrics.md`, `diagnostic_contract.md`, and evidence fields. Include identity joins for one twenty-step recovery; disclose unknown library ownership and observation cost.

## 4. Controlled affinity comparison

- [x] 4.1 Add a frozen treatment manifest, schema 1. Resolve/snapshot/protect `compare.cases.<case_id>.affinity_experiment`. Record exact builds, compile options, common observation patch, and limited allowed-difference fields. Reject wildcards and whole-object exclusions.
- [x] 4.2 Keep ordinary comparison strict. Update `identity.py`, `run_batch.py`, `baseline.py`, `compare.py`, and necessary output schemas to accept differences only in explicit experiments. Preserve original groups and allowed differences in reports; all other conditions and three-run consistency per side remain exact.
- [x] 4.3 Extend `test_compare.py`, `test_identity.py`, `test_variable_comparison.py`, and `test_suite.py`. Cover rejected ordinary CPU differences, valid declaration, manifest mismatch, mixed builds, changed input/model/algorithm/dependencies/queue/budgets/metrics, extra driver differences, missing qualified rounds, and historical evidence not becoming new results. Existing integrity negative controls still fail.
- [x] 4.4 Explain treatment/report boundaries in README, `acceptance.md`, and manifest examples: CPU allocation is the experiment, tool changes require per-file audits, and absent thresholds give no performance pass. Examples pass parsing/comparison checks.

## 5. Automated checks and auditable two-sided builds

- [x] 5.1 Export reference from `4871442dda37d3d64087fda0b7756692c8f7e473` with common observation patch. Retain exact patches and file manifests. Separately preserve the common supplier-support fix; review confirms it adds no binding, early supplier, or algorithm changes. Label it “algorithm baseline + common observation patch + common source-support fix.”
- [x] 5.2 Obtain final-candidate checks with `./build.sh --test --artifacts-dir <independent-directory>` and necessary targeted regressions. Save platform/image/toolchain/full results. Retain the full suite/native OS checks and language norms; repair failures and rerun affected checks.
- [x] 5.3 Build two distinct ARM64 programs with the same compiler/options/external libraries. Retain source, executable, project-library, compile/dependency manifests and SHA-256. Confirm unbound reference, enabled candidate policy, and identical observation logic.
- [x] 5.4 Freeze single-file configuration, exact treatment/build manifests, common-tool sources, and planned order. Manifests match actual files, source media/models remain read-only, and `test-02` is explicitly user-skipped.

## 6. Eighteen identified-board runs and restoration

- [x] 6.1 Create independently recoverable board windows using only `adb -s 5ceb49407502303d`. Record board/serial/machine/kernel, actual 7.74 GiB memory, CPU topology/masks, system Runtime 2.3.2 hash, and driver 0.9.8. Confirm independent restoration guard before stopping services and preserve initial protected files/service states.
- [x] 6.2 Complete six fresh `wlcb-104206-020-035` runs, alternating three unbound references and three pinned candidates serially. Fix supplier 4, `predecode_all`, preparation 3600 seconds, cleanup 10 seconds, lateness 50000 microseconds, and queue 32. Preserve raw diagnostics/integrity, thread/work-point placement, memory, exits, and every planned failure.
- [x] 6.3 Complete six fresh `wlcb-104206-100-130` runs under the same frozen conditions/evidence rules with independent initialization. Only three new qualified runs per side establish a complete interval comparison.
- [x] 6.4 Complete six fresh `wlcb-104206-165-end` runs under the same conditions, retaining the final original frame and independent initialization. Require three new qualified results per side; historical tail results do not substitute.
- [x] 6.5 Stop test processes and restore original services even after failures. Preserve unchanged Runtime/production files/release link, no leftovers, and guard completion.
- [x] 6.6 Freeze each interval's reference and deliver explicit-experiment JSON/CSV/Chinese reports with every round, medians/ranges/differences/percentages. Cover updates, YOLO, queue waits, recovery work, output, queues, memory, and actual CPUs. Disclose unknown threads, measurement cost, failures, unset thresholds, and restoration evidence; claim neither accuracy nor compulsory improvement.

## 7. Final consistency

- [x] 7.1 Map proposal/design/all delta scenarios to final code, automated checks, and eighteen runs. Run `openspec validate pin-regression-thread-affinity --strict`. Record actual source/binary identities, omitted checks and reasons, unchanged production policy, and compatible snapshot → request/skip → arbitration order.

## Workflow follow-up

- Sequential work 2 implements and measures; work 3 independently checks implementation/specifications. Work 4 may reuse measurements only if verification changed no code and confirmed complete evidence, with its reuse rationale recorded.
- If independent verification changes build/runtime code, work 4 must rebuild both sides and complete eighteen fresh comparisons against final source. Preserve prior results as history.
- Only after implementation, verification, regression, and restoration succeed may work 5 synchronize/archive/commit under the user's authorized workflow. Earlier stages must not do so.

## 8. Failure investigation and common supplier repair

After the first failure, the user explicitly authorized investigation, repair, and continuation. The fix invalidated old final identities for 5.1–5.4, so those tasks reopened. Original records remain historical facts.

- [x] 8.1 Retain auditable wait evidence separating original rounds, trace-disturbed rounds, real-pipeline kernel-wait samples, and unreplicated magnitudes. Explain causal inference and uncaptured lock owners.
- [x] 8.2 Share read-only cached storage in both builds through an owned-retention interface on real HostFrame, used by regression supply. Preserve separate production deep-copy APIs. Targeted checks verify pixels, stride, timestamp, backend type, and reads after source cleanup.
- [x] 8.3 Prove removal of full-image allocation using same-condition old/new real-pipeline diagnostics: supply durations, faults, and system calls. Confirm the repaired program with the unchanged 50 ms limit. Record automated checks, restoration, and the still-pending final eighteen-run boundary at that stage.
- [x] 8.4 Validate masks against role-registration time. Record per-thread snapshot-read start to avoid treating legal preparation masks as playback changes. Post-registration changes/invalid-time negative controls still fail. Both builds share the same observer repair and obtain fresh binding confirmation.

## Completion evidence for the resumed implementation

The fresh plan completed 18/18 qualified runs on 2026-10-20; the checklist was 34/34. Work 3's fresh agent independently verified the result with initial/final CRITICAL 0, WARNING 0, SUGGESTION 0 and no build/runtime/tool code changes; see [verification-1.md](verification-1.md). Under user authorization, work 4 reused these eighteen runs; [final-validation.md](final-validation.md) records the rationale and later workflow state. It also maps all 23 specification scenarios and final identities/check boundaries. [regression-report.md](regression-report.md) contains each interval's rounds and three-run statistics.

Original [validation.md](validation.md) and [failure-investigation.md](failure-investigation.md) retain failed/diagnostic results, not rewritten successes. New raw evidence was stored at `/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/`.
