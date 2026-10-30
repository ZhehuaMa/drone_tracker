# Final implementation and evidence consistency record

The sequential-work-2 implementer recorded delivery evidence here; the final section records later work 3–5. Tasks completed 34/34, all eighteen fresh planned runs qualified, and restoration was checked. See independent [verification-1.md](verification-1.md). Original failures and diagnostics remain unchanged.

## Final identities and reconstruction

Evidence root: `/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020`. Algorithm commit: `4871442dda37d3d64087fda0b7756692c8f7e473`. Reference label: “algorithm baseline + common observation patch + common source-support fix.” Source identity is the actual build-input manifest; newly authored OpenSpec reports are not runtime source identity.

| Side | Final source SHA-256 | Final executable SHA-256 |
|---|---|---|
| ref | `080cf382dc2b8306c8e75c14b8c21807968a69551bd9fc62f3d7848faaebb37a` | `dcba1fdc854fad8102cf930149d8bc5b19ec2f2542cdb8f2b3422d041f548799` |
| cand | `198c42f7a0173f3a980b9a54fa842c29f600ec7048b24a686087c46ad656186d` | `9810ee537e7120baafbba94b69c7f1414c4d6f2e705337f68820cefda72c1251` |

`final-source-consistency.json` rechecks the development tree, independent reference tree, and delivered binaries. `{ref,cand}-source-export.json` retains per-file inputs; source archives and exact patches are retained with the evidence. Rebuild reference by applying observation/tool patches then the common support fix to the fixed commit; rebuild candidate from its complete patch.

CPU affinity specifies permitted cores; work-point CPU records where execution actually occurred. The new receiver defaults off and is installed only by regression. Production `cpu-affinity.yaml`, public role implementation, two parameter files, launch, and `HybridTracker` algorithm files matched the fixed commit byte-for-byte. Shared YOLO/node files added only common, default-disabled sampling hooks.

Existing snapshot → request/skip → arbitration order remains in unchanged `src/hybrid_tracker/hybrid_tracker.cpp`. The common HostFrame fix adds owned read-only retention without changing production copy interfaces. Event layout, CRC, request/recovery definitions, queue, budgets, and lateness limits remain in force.

## Automated checks and platform limits

- Candidate used `./build.sh --test --artifacts-dir <new-evidence-root>/candidate-build`: 57/57 CTests and native OS address-space-limit check 1/1 passed.
- Reference used the same entry with separate caches. Initial result was 56/57 because packaging lacked an ignored empty install directory. Adding the same existing test-environment directory used by candidate/old diagnostic reference made the affected CTest pass 1/1. All 57 eventually passed; the first failure log remains. No source fix or test removal occurred.
- Each side ran 88 unique Python SDK cases: 87 native and 1 actual ARM middleware case, without skips. These included 8 affinity/evidence/experiment and 6 controller failure/deadline/cleanup cases. Original SDK logs were extracted from full CTest without needless repetition.
- Default amd64 image was `025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`, GCC 11.4.0/CMake 3.22.1, ARM64 output. Compiler, flags, and definitions matched across 84 C++ targets. No fallback ARM64 image was used.
- clang-tidy/clangd were unavailable. Jetson, camera, HDMI, long-duration load, flight, and human-labeled accuracy checks did not run. x86/QEMU results do not establish board timing performance. test-02 was explicitly skipped.

Full build/test logs are under the evidence root. Compile/dependency/source manifests are `*-compile-inputs.json`, `compiler-options-equality.json`, `project-library-artifacts.json`, `common-build-entrypoints.json`, and `board/control/formal-preflight-identities.json`.

## Complete specification-scenario mapping

The table gives English descriptions of all 23 scenarios in this change's delta specification, which contains added or modified requirements. m0 is monotonic playback origin; e0 is corresponding system-time origin. Together they map original frame intervals. Supplier means the input thread, Tracking the update thread, logger the independent collector, and supervision external controllers. Negative tests establish rejection; one healthy board run is not fault injection. A synthetic twenty-step recovery does not imply twenty steps occurred in a particular formal run.

| Scenario | Automated evidence | Delivery/board evidence and limits |
|---|---|---|
| Comparing changed models | `test_identity.test_true_model_content_change_is_incomparable`, ordinary model negative controls | Explicit experiments still check actual model content; all three comparison identities match |
| Same case name with changed interval | `test_variable_comparison.test_cases_versions_and_ranges_cannot_mix` | Three intervals and original frame ranges freeze independently |
| Undeclared affinity difference | `test_affinity.test_formal_comparison_ordinary_rejects_and_explicit_checks_builds` | Formal runs declare treatment; ordinary comparison remains strict |
| Declared experiment with other differences | `test_affinity.test_strict_leaves_and_common_driver` | Exact permitted leaves retained; all other group fields match |
| Requested regression mapping | Configuration/initial-range/external-policy tests; `tracking_regression_round_tests` | Nine candidates read back roles/work points 4/5/6/7/1, supervision 0–3; production policy bytes unchanged |
| Old supplier-only configuration | `test_supplier_cpu.py`, no-policy source/round CTest | Reference fixes only supplier 4; other managed masks are 0–7 |
| External policy cannot override configuration | `test_affinity.test_online_initial_cgroup_and_environment` | Private policy derives from immutable config; launch removes old external setting |
| Different supplier/Tracking cores | `parkedSupplierLifecycle`, source CTest | Candidate supplier 4 ready before control narrowing; Tracking 5 registered before m0 |
| Kernel permits only CPU 5 | Initial/cgroup negative controls and source excluded-core check | Kernel masks not expanded; initial 0–7 and effective range recorded |
| Model construction fails after preparation | Parked-source lifecycle, node-round failure cleanup, controller cleanup | Failure creates no playback clock and threads stop; eighteen formal runs leave no process |
| Twenty-step recovery | `test_affinity.test_evidence_requires_exact_points_masks_and_capacity`, including missing-point control | Original CatchupStep/CPU points match by round/update/request/frame for every run |
| Observation overflow | Same evidence test with overflow=1; C++ observer-array checks | Capacity 262144/writer across eighteen runs; all overflow=0 |
| Preparation and registered-role masks separated by time | Before/at/after-registration, scan-crossing, invalid-time controls | observed_mono_ns retained; post-registration mismatch still fails; new tool identities match |
| Extra inference-library threads | `test_affinity.test_unknown_threads_and_exit_races_are_not_managed_proof` | Unknown TIDs/masks/last_cpu retained per run without guessed roles or bulk rebinding |
| Predecode threads end | Source predecode/release and preparation/clock checks | Preparation and model-ready/playback thread lists kept separately; decoder work is not continuous playback compute |
| Audited tool differences | Strict-leaf and extra-driver-file controls | Same 15 module origins/hashes and control manifest checked before maintenance and each run |
| Damaged diagnostics | collect/compare CRC, pairing, normal-end, altered-evidence controls | Format/limits unchanged; eighteen raw .dtd/events.bin hashes pass |
| Three existing intervals | Multi-case config/freeze/comparison checks | Three fresh references plus three fresh candidates per interval, 18/18 total |
| Slow or zero-box round | `test_compare.test_slow_and_empty_rounds_never_dropped`, unqualified-run controls | All legal observations retained, no zero-box filtering; guard stops plan after any failure |
| Correct binding with variable results | No-default-pass and explicit-threshold comparator checks | Medians/ranges/differences/percentages/workload reported per interval; thresholds unset |
| Verified pixels supplied on original timeline | `supplierReusesVerifiedPixels`, `retainedHostFrameOwnership`, original source accounting | Common support identical; m0/e0, frame totals, queue dispositions, 50 ms limit preserved |
| Frame/view survives source cleanup | `retainedHostFrameOwnership`, retained-supplier lifecycle | View survives source/frame destruction; production deep-copy APIs remain independent |
| External buffer without retainable ownership | Borrowed-buffer and invalid-Gray8 `retainedHostFrameOwnership` controls | Borrowed external buffer rejected; an unowned pointer cannot masquerade as owned frame storage |

## Three intervals, restoration and conclusion limits

Only `adb -s 5ceb49407502303d` reached the identified board. Intervals were `[20,35)`, `[100,130)`, `[165,end)`, each with three reference/candidate pairs in alternating order: eighteen runs total. Conditions fixed full predecode, supplier 4, P=3600 seconds, C=10 seconds, lateness 50000 microseconds, queue 32.

All eighteen had completion, CRC/event integrity, work-point identity, masks, external snapshots, memory, and exit/cleanup evidence. Actual common-module origins/hashes were checked before stopping services and before/after every run. The earlier preflight import-order issue did not enter these tools. No extra ADB probes disturbed timing.

Final production service PID was **11692**, active/running/enabled, independent restore timer inactive, with no test processes. All 91 protected files, models, system Runtime, deployment link, and governors retained original values. Proof: `/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/final-board-proof.json`.

Every round, median/range, candidate-minus-reference difference, and percentage is in [regression-report.md](regression-report.md) and external `analysis/` JSON/CSV/Chinese details. Zero output, missing recovery events, slow rounds, and observer costs were not removed. Unset thresholds establish no performance pass, accuracy gain, or complete contention isolation.

[validation.md](validation.md) preserves original formal failures; [failure-investigation.md](failure-investigation.md) preserves diagnostics/common fixes. Their progress, binaries, and unexecuted conclusions apply to earlier versions, not this fresh plan.

Initial host analysis lacked a reference directory because ADB transfer was unfinished. The failed attempt remains separate. After transfer, freeze/comparison reran without changing board evidence. Final strict OpenSpec and `git diff --check` results are under the evidence root. This section records work 2's completion; later independent verification and evidence reuse follow.

## Later workflow state — 2026-10-20

1. Work 3 used a fresh independent agent. Initial/final counts were CRITICAL 0, WARNING 0, SUGGESTION 0: no serious gap, spec departure, or suggestion. No fixes were made; only [verification-1.md](verification-1.md) was added. The verifier independently checked all eighteen raw diagnostics, CPU placement, metrics, and three comparisons, and matched 433 candidate build inputs, both binaries, and actual tool identities.
2. Work 4 skipped rerunning under the user's explicit condition: work 2 had 18/18 fresh qualified final-source runs, and work 3 changed no build/runtime file and independently confirmed complete evidence. Reuse included no historical failures, diagnostic rounds, or old-version substitution. All three comparisons stayed `comparable` and decisions `not_set`, meaning no performance thresholds.
3. Work 5 synchronized main specs, archived, and updated workflow documents before staging related files and creating an authorized local commit. It changed no runtime source/tool/binary/model/deployment, made no board connection, and did not push. Historical archive path was `/home/mzh/nickluo/drone_tracker-worktrees/regression-cpu-affinity/openspec/changes/archive/2026-10-20-pin-regression-thread-affinity/`; sync target was `openspec/specs/rk3576-tracking-regression/spec.md`. Git history supplies the actual commit; a planned operation is not a completed result.

Independent-verification limits still apply: no fresh complete build or board run, unavailable clang-tidy/clangd, and no Jetson/camera/HDMI/long-load/flight/labeled-accuracy validation. Board state refers to retained restoration proof; its PID was not reread during later document editing. External `handoff.json` preserves work-2 handoff, including historical “not independently verified” fields. This section and independent report provide subsequent state.

Work 5's archive checks completed: one modified and eight added requirements synchronized; existing main-spec title, Purpose, and unrelated requirements were preserved verbatim. Before archival, `openspec validate pin-regression-thread-affinity --strict` passed. After sync, `openspec validate rk3576-tracking-regression --type spec --strict` passed. Archived validation `openspec validate --archived --strict --json` passed 37/37. Named-change validation searches only active directories and cannot select archive names/path names. The same installed OpenSpec `Validator(true)` separately validated archived `proposal.md` and `validateChangeDeltaSpecs` strictly: 0 errors, 0 warnings, 0 notices for each.

Repository-wide `openspec validate --specs` passed 25/25; only other existing capabilities had requirement-length warnings above 500 characters and were unchanged. At that historical check, 36 local Markdown links resolved without stale active-directory targets; historical path prose retained its original meaning. All 433/433 candidate build inputs still matched the formal manifest. These were documentation/specification/archive checks, without repeating already passed builds or hardware runs.
