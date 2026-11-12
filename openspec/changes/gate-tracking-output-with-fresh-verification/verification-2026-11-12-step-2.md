# Verification report: gate-tracking-output-with-fresh-verification

## Scope and verdict

Date: 2026-11-12. Repository: `/home/mzh/.codex/worktrees/a6cf/drone_tracker`.

This fresh verification follows the repairs in [step 1](verification-2026-11-12-step-1.md). It reviews the current uncommitted implementation against the proposal, design, tasks, and all four specification deltas. The base revision is `1474c7f1e4376c3c560758ece60835a5d7f6cc00`; the working-tree source is identified by the retained manifests described below.

**Final issue counts: 0 CRITICAL, 0 WARNING, 0 SUGGESTION.** No implementation fixes were needed in this iteration. The coordinator owns the separate verify-fix summary.

| Dimension | Result |
| --- | --- |
| Completeness | 52/52 tracked tasks checked; implementation and evidence found for 77 ADDED/MODIFIED requirements. One REMOVED requirement was checked against its usable baseline and its behavior is absent. Six renamed requirements also have MODIFIED bodies and were checked against those bodies. |
| Correctness | The 77 requirement bodies and 130 scenarios map to current source, controlled tests, or retained target evidence. Both step-1 warning groups are resolved. No applicable artifact-level check was skipped. |
| Coherence | The implementation follows the specified bounded worker, separate seed/confirmation eligibility, elapsed clocks, publication ordering, model boundaries, and historical-report meanings. No significant code-pattern deviation was found. |

## Requirement and scenario mapping

| Capability | ADDED/MODIFIED requirements | Scenarios | Reviewed implementation and evidence |
| --- | ---: | ---: | --- |
| `tracking-output-validity` | 32 | 43 | `output_validity.h`; shared tracker acquisition, retained replay measurements, counters and elapsed limits; injected-clock and tracker tests; independent ROS expiry/publication tests; identified-board recovery records. |
| `detection-tracking-arbitration` | 24 | 49 | `yolo_detector.h/.cpp`; source comparisons, plan ledger and unique candidate association in `hybrid_tracker.cpp`; geometry, preprocessing, cadence, scheduling, capacity, cancellation, fallback and round tests. |
| `target-observations` | 12 | 22 | `publishTrackingObservation()`, `drainTrustLoss()`, `notifyObservationOff()`, and the paired observation builder; controlled publication/loss/OFF tests; recorded bbox/LOS bag audits. |
| `rk3576-tracking-regression` | 9 | 16 | Format-4 diagnostic producer/reader/inspection paths; scheduler and validity metrics; version-3 standalone report; historical-format tests; exact-pixel crop and isolated scheduler/combined comparisons, with declared evidence limits. |

The REMOVED requirement is `Watchdog counting and reset behavior remain unchanged`. Its main-spec baseline describes incrementing an initialized track's no-result frame count and clearing it on any reply. The revised tracker has no active no-result watchdog counter/reset or frame-threshold cleanup branch. Candidate expiry, confirmation expiry, usable adverse-result limits, actual failures, and shutdown remain separate. Historical watchdog records remain available as historical evidence.

## Independent checks of the step-1 repairs

### Cancelled result authority

`cancelPlan()` records the cancelled plan identity and reason in a fixed four-entry value-only array (`src/hybrid_tracker/yolo_detector.cpp:301`). These records own neither images nor device storage. The bounded active, unread, and locally taken result roles limit the number of relevant cancelled identities.

`publishResult()` samples cancellation while holding the detector mutex (`yolo_detector.cpp:221`). `get_result()` checks again at take (`:190`), covering cancellation after publication but before consumption. A cancelled result retains its original source, job, candidates, and attempt provenance, while its completed-coverage authority is removed. The worker completes device access and releases its session before another call can execute; result taking does not free device storage.

Tracker arbitration checks `plan_cancellation_reason` before the ambiguity/completed-adverse branch (`src/hybrid_tracker/hybrid_tracker.cpp:659`). Thus a cancelled final empty or disagreeing stage, or a cancelled ambiguous regional stage, cannot revoke permission or spend the adverse-result budget. A separately eligible unique positive still uses the normal same-image, attempt, freshness, and validity checks. Cancellation alone supplies no permission. The parent outcome guard (`:692`) prevents that stage from emitting a second terminal verdict for its cancelled parent.

`testCancelledLastStageIsNotCompleteEvidence()` (`diagnostic_tracker_test.cpp:1746`) cancels during the seventh device call and verifies retained identity with removed completed coverage. `testCancellationAfterPublicationPreservesUnreadIdentity()` (`:1767`) checks the unread interval. `testRegionalPlanVerdicts()` (`:1402`) checks cancelled disagreement and ambiguity neutrality, preserved unique positive confirmation, and absence of a second parent outcome. Inference barriers are bounded at two seconds.

### Fallback, feedback and restart coverage

`testFirstMissFallbackAndPartialFeedback()` (`diagnostic_tracker_test.cpp:1787`) starts from a targeted miss with no existing discovery plan. It checks the immutable initial-job fallback link, later-image misses adding no plan, fixed-source completion, and nonmatching-positive feedback enabling broader search. `testCancelledRestartRotatesFirstTile()` (`:1834`) cancels an active source, then checks a new source/plan identity and a different initial tile. These directly cover the scenario gaps reported in step 1.

The worker still has one active call, one replaceable pending-image slot, one fixed-image plan, one unread result, and one locally consumed result. Taking releases capacity before arbitration. The two-targeted/one-discovery selection remains bounded and discovery proceeds under repeated new input. None of these operations renews evidence anchors or changes confirmation deadlines.

## Other correctness and coherence checks

- Seed acceptance requires original pixels, current-round/source identity, an undisposed job, structural validity, and a usable clock. Expired-A provenance is preserved when a result seeds B. Early acceptance precedes initialization. B's independently produced same-image replay measurement is required for later-image verification; neither the seed nor A's old comparison substitutes. Active replacements are protected.
- Partial regions, missing comparisons, stale evidence, wrong-round work, and disposed jobs remain neutral. Unique overlap determines agreement after clipping; an unrelated higher-score candidate does not replace the match. A completed applicable plan acts adversely once. The filter-to-rectangle conversion preserves the physical center and floating geometry.
- `OutputValidity` checks immutable evidence clocks and exact deadlines under its short value lock. Confirmation cannot revive an already expired verified instance. Candidate deadlines are measured from seed acceptance. The old frame-count boundaries have no cleanup authority. The appearance/motion stubs and both jump counters supply no hidden acceptance or rejection authority.
- Revocation and pending-loss registration occur together. Observation admission drains preceding loss attempts under publication serialization, then rechecks identity and actual age. The validity lock is released before I/O. Bbox, LOS, bag and state publication attempts retain independent outcomes. OFF drains pending loss before its separate notification. Captured/pose reference times are preserved; an event cannot relabel a valid image sample.
- Versioned diagnostics distinguish requests, stage jobs, plans, seeds, confirmations and cancellations. Historical whole-cascade counts and watchdog events retain their meanings. Unknown formats are rejected; missing evidence remains unavailable. Availability excludes loss/OFF event pairs, and recovery separates verification, valid admission and topic attempts. Incomplete episodes and unset performance criteria remain explicit.
- The affected parameter templates retain defaults 100/1000/10 ms and starting interval 1; interval 4 remains configurable. Shared code does not acquire platform SDK dependencies. NanoTrack device-feature transfer and model preprocessing contracts are unchanged.

## Verification commands and retained validation

This iteration performed read-only source/evidence verification and wrote only this report. It did not repeat passing hardware or broad test runs without a source change or unresolved test concern.

| Check | Result |
| --- | --- |
| `openspec status` and `openspec instructions apply` for this exact change | Spec-driven schema; all context files readable; task tracking configured; 52/52 complete, `all_done`. |
| `openspec list --json` | Existing root resolved to this worktree before report creation. |
| `openspec validate gate-tracking-output-with-fresh-verification --strict` | Exit 0. |
| `git diff --check` | Exit 0. |
| Preferred conda Python SHA-256 comparison against `verify-step-1/source-files.json` | All eight current source/test/doc/task/report files match. |
| Retained follow-up evidence manifest SHA-256 | Matches `bf69ff3d5143dff11b691155b663aa11efffc6d24f0fec75ad0482bb8b2ce6b9`. |
| Exact-source full SDK log, `verify-step-1/accepted-full-sdk.log` | Retained final `ROS_DOMAIN_ID=212 ./build.sh --test` exited 0; 68/68 CTest checks passed in 193.19 s. |
| Native follow-up log and bag audit | Five final controlled executables passed; actual-model node and inspector exited 0. Four loss pairs were recorded in both bags, with zero paired timestamp mismatches and loss-before-recovery ordering passed. Services are recorded active afterward. |

Follow-up evidence root: `/home/mzh/nickluo/validation-retained/fresh-verification-20261112-a6cf/verify-step-1/`. The original dated validation report and original crop/scheduler comparisons remain scoped to their earlier binary hashes. The cancellation follow-up supplies its own final source/binary identities and actual-model run. It does not relabel original measurements.

## Validation limits and final assessment

The post-fix video uses the explicitly temporary confidence 0.40 / confirmation 200 ms profile, domain 212, identified models/input, system Runtime 2.3.2 and driver 0.9.8. It produced 78/696 valid bbox outputs, two completed recovery episodes and two incomplete episodes. This is not the formal default profile. Original formal 3/724 and live-camera 0/501 availability remain reported limitations. No versioned performance threshold was supplied, and no performance, accuracy, angular-accuracy, subscriber-delivery, or flight-safety pass is inferred.

Finite-video EOF already seals its completed recording before a later OFF notification. The missing post-EOF OFF pair is explicitly disclosed and excluded from recorded-OFF claims; all trust-loss pairs are present. Active-round OFF/loss ordering and both camera OFF pairs have separate evidence. The specifications do not require keeping an EOF-ended bag open indefinitely.

Physical Jetson hardware/full SDK validation and clang tooling were unavailable. Their execution is not claimed. The artifact-level completeness, implementation mapping, scenario coverage, design adherence and code-pattern checks above were performed; these hardware/tool limitations do not become passing executions.

**All artifact-level checks passed. Final counts are 0 CRITICAL, 0 WARNING, 0 SUGGESTION.** The change is ready for the separate synchronization/archive workflow subject to its own controls; this iteration did not synchronize or archive it.
