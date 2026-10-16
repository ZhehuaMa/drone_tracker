# Tasks

All tasks below are implementation or validation work for a later apply request. No task is complete merely because these planning artifacts exist. Use the remote worktree and follow its AGENTS.local.md. Preserve the watchdog and defer any maximum result-age limit.

## 1. Retain source-frame comparison metadata

- [x] 1.1 Preserve bounded value-only source snapshots in the request/result types in `src/hybrid_tracker/yolo_detector.h` and the tracker boundary; completion means each accepted request carries a diagnostics-independent round generation, frame/update identities, original capture time, source boxes, and validity without an owning image reference in the metadata.
- [x] 1.2 Finalize genuine source-frame observations in `src/hybrid_tracker/hybrid_tracker.cpp` before submission; completion means held previous-frame boxes are not mislabeled as current observations and submitted snapshots cannot be rewritten by later updates.
- [x] 1.3 Prove eviction and lifetime behavior in `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp` and `preprocess_contract_test.cpp`; controlled delayed results retain metadata after image eviction, while weak references show metadata alone does not retain pixels.
- [x] 1.4 Document snapshot ownership and validity at the request/result boundary; completion means comments explain disposal at consumption, supersession, result overwrite, and round cleanup.

## 2. Handle unavailable context without changing the watchdog

- [x] 2.1 Eliminate cross-time fallback comparisons in `src/hybrid_tracker/hybrid_tracker.cpp`; completion means overlap and jump decisions use only valid source-frame operands and missing context leaves the unconfirmed-result counter unchanged.
- [x] 2.2 Separate historical comparison from recovery eligibility in the tracker; completion means metadata-only agreement can confirm healthy tracking, missing pixels do not trigger initialization, and unavailable jump recovery cannot be recorded as successful acceptance.
- [x] 2.3 Prove arbitration behavior with controlled model inputs in `diagnostic_tracker_test.cpp`; cover asymmetric source/current boxes, missing tracker versus prediction fields, metadata-only agreement, low overlap without pixels, jump tolerance without pixels, and five genuine no-target results.
- [x] 2.4 Prove unchanged watchdog and state behavior in tracker tests; cover counts 60 and 61, reset by an uncomparable positive reply, reset by a no-target reply, existing timeout/early-return outputs, Detecting inactivity, and existing partial-replay acceptance.
- [x] 2.5 Document unavailable-context outcomes and the unchanged watchdog limitation in `tests/tracking_regression/acceptance.md`; completion means the document explicitly excludes an age cutoff and any freshness guarantee.

## 3. Refresh pending detector input safely

- [x] 3.1 Replace occupied unstarted requests atomically in `src/hybrid_tracker/yolo_detector.cpp`; completion means one worker and one pending slot remain, invalid input preserves the old request, and unread-result/stop/cleanup protections remain.
- [x] 3.2 Offer replacement input from both tracker phases in `hybrid_tracker.cpp`; completion means Detecting offers every processed frame and Tracking retains the configured interval without a pending-busy short circuit.
- [x] 3.3 Prove pickup/replacement interleavings in `preprocess_contract_test.cpp` with controlled barriers; verify complete input associations, unchanged executing input, newest eligible pickup, explicit supersession, and progress under continuous replacement.
- [x] 3.4 Prove cleanup and memory bounds in detector/tracker tests; repeated replacement and ON/OFF cycles release old images and metadata, wait for executing access, and reject old results despite repeated frame numbers.
- [x] 3.5 Document the request admission and supersession contract in `yolo_detector.h`; completion means pending occupancy, execution, and unread-result protection are explained separately.

## 4. Reconcile diagnostics and regression reports

- [x] 4.1 Preserve interpretable raw evidence in `src/diagnostics/` and producer sites; completion means supersession links, metadata availability, image availability, source operands, and unavailable recovery have documented records without renumbering old identifiers or blocking business threads.
- [x] 4.2 Reconcile new outcomes in `tests/tracking_regression/collect.py`, `metrics.py`, `compare.py`, and affected schemas/readers; completion means cancellations are distinct from inference and no-target counts, old metric definitions remain identifiable, and absent historical fields stay unavailable.
- [x] 4.3 Prove accounting and compatibility in diagnostic and regression tests; fixtures cover repeated supersession, missing terminal records, metadata-only comparisons, unchanged old logs, unsupported formats, and equal algorithm behavior across diagnostic modes.
- [x] 4.4 Document event payloads and metric definitions in `diagnostic_contract.md`, `metrics.md`, and `README.md` under `tests/tracking_regression/`; completion means the raw evidence needed to recompute every new metric is named.

## 5. Verify integrated behavior and controlled performance

- [x] 5.1 Establish integrated build/test results from the remote worktree using `./build.sh --test`; retain logs identifying the SDK image and output architecture, and report actual failures without substituting another backend.
- [x] 5.2 Establish reference/candidate provenance for an isolated production-board comparison; completion means exact source/executable hashes and matching fixture, model, configuration, board, Runtime/driver, scheduling, workload, and diagnostic identities are recorded before runs.
- [x] 5.3 Measure behavior on the same HZ-RK3576_WuKong Board using the existing regression tools; obtain at least three qualified runs per version, retain rejected attempts, and preserve the existing 120-second total deadline and supply-lateness budget.
- [x] 5.4 Deliver a reproducible comparison report covering full clip and core window; verify valid output, actual publication gaps, recovery steps/time, request wait/age, supersession, memory, and unchanged watchdog outcomes from raw records, without claiming accuracy from unannotated footage.

## Workflow follow-up

- Review implementation against both delta specs before syncing or archiving.
- Treat production deployment and any later watchdog or result-age policy as separate work.
