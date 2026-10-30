# Independent acceptance: optimize-rk3576-nanotrack-window-preprocessing

Acceptance date: 2026-10-12, using the development host's date. Production-board calendar time was still 2026-08-11, so board file dates do not identify this run's actual date.

This historical report used `openspec-verify-change` to independently inspect planning artifacts, uncommitted implementation, raw records, and board results. Old/new numerical results, model work, and performance thresholds passed for the specified reconstructed input. No missing production implementation was found. Two WARNING issues and unverified scope remained, so this was not an all-checks-passed or unconditional-archive conclusion. Later W2 fixes do not change the findings recorded at this stage.

## Scope and basis

`openspec list --json` confirmed worktree root `/Users/mazhehua/.codex/worktrees/nanotrack-window-preprocessing/drone_tracker`. Selected change was `optimize-rk3576-nanotrack-window-preprocessing`, schema `spec-driven`. Inspection read `proposal.md`, `design.md`, `tasks.md`, `specs/model-input-preprocessing/spec.md`, `validation.md`, and raw `evidence/production-20261012/` records. `$openspec-verify-change <change-name>` selects another change.

Checks followed worktree `AGENTS.md`, `openspec/config.yaml`, C++ norms, and original-repository `AGENTS.local.md` tools. Board access used only ADB serial `5ceb49407502303d`, without SSH/scp. Acceptance changed no implementation, specifications, tasks, commit, sync, archive, or deployment. It added only this report and three supporting evidence files.

Implementation base was `ea9cd822b9717b2759ece8e251bb977c3f88449b`. Scope included `git diff` and untracked test/diagnostic source; HEAD alone did not identify the accepted implementation.

## Three-dimensional scorecard

| Dimension | Observed result |
|---|---|
| Completeness | 20/20 tasks checked, 0 remaining, each with implementation/evidence. Implementation found for 8/8 ADDED requirements; MODIFIED, REMOVED, RENAMED all 0. Standard-memory environment remained partly unverified: W1 |
| Correctness | Code mapped for 8/8 requirements. All 23/23 scenarios had code/report treatment; 22/23 had corresponding tests/measurements/raw evidence. The insufficient-benefit scenario lacked a performance-failure control: W2. Actual-input equality, per-frame results, workload, and thresholds passed |
| Coherence | 6/6 major design decisions matched. Code-pattern inspection found no significant departure. C++ static-tool checks were unverified because the image lacked clang-tidy/clangd and the repository had no command |

All six applicable core checks ran: task completion, spec coverage, implementation mapping, scenario coverage, design adherence, code-pattern consistency. Scenario coverage remained partial and is listed as unverified under the skill rules. Missing optional artifacts created no issue.

## Independent raw-record recomputation

Neither `analysis.json` nor implementation-report verdicts supplied calculation inputs. Conda-base Python standard-library code parsed `frames.csv`, `intervals.csv`, original input identities, manifests, and source fingerprints directly. Output is [verification-independent-analysis.json](../production-20261012/verification-independent-analysis.json).

| Metric | Old path | New path | Verdict |
|---|---:|---:|---|
| Preprocessing batch median | 367.115402 ms | 7.278238 ms | New/old 1.9825477%, below 20% |
| Preprocessing P95 | 373.372192 ms | 7.426194 ms | Retained component observation |
| Complete recovery-batch median | 573.712983 ms | 193.3046045 ms | Decreased |
| Complete recovery-batch P95 | 580.295609 ms | 197.284198 ms | Did not increase |

Even-sample medians average the central two. P95 selects sorted item `ceil(0.95×N)`; with 10 formal batches per path it is the maximum. Parent and child intervals were not summed to invent total recovery time.

Independent checks confirmed:

- 24 batches, 600 frames: two warmup and ten formal batches per path, 12 pairs with alternating order. All warmups excluded from formal statistics.
- 11256 timing intervals belong to 600 frame operations, with unique IDs, valid/complete boundaries, and children inside parents. Per-frame `preprocess_ns` and `image_prepare_ns` match sums from raw intervals.
- Actual `run` intervals and ancestry identify 1176 NPU executions. Every batch has 1 template, 24 search, and 24 Head calls, matching frame records. Search-direct Run belongs directly to Inference; template/Head retain model ancestors. Production source supports this attribution. Missing SearchBackbone parent counts neither prove failure nor justify invented calls.
- All 12 pairs match per-frame timestamps, success, initialization, boxes, and scores. Float32 records use 9 significant digits; the tool also performs direct exact floating comparison internally.
- All 25 manifest entries match `fixture-original.csv`/`input-identity-original.json` in order, video indices 4146–4170, timestamps, paths, and pixel hashes. Timestamps were not generated from FPS. Board `--check-inputs` decoded/checked all 25 again successfully.
- All 289 current source/header/CMake/template fingerprints match retained board identities. Restoring class/namespace/FloatInput references makes the old full-frame reference byte-identical to the original commit's `.cpp`.
- `exact-inputs.txt` records exact equality for 25 trajectory inputs and 4730187 elements, independently counted as `3×127×127+24×3×255×255`. During real recovery, the equality checker compares each production vector to old reference before the model consumes production input, outside performance timing. Full float arrays were not saved, so offline elementwise recomparison is unavailable. Code identity, records, and board numeric checks jointly support the result.
- The other-window dataset has 192 records; all eight side lengths report equality. New processing is slower at side 2048, and the report does not generalize small-window ratios to large windows.

## Requirement and scenario mapping

Paths below are relative to the historical worktree. Scenario descriptions are translated from the delta specification; the original specification identifiers are unchanged. Numerical entry is `src/platform/rk3576/tests/nanotrack_preprocess_test.cpp`; board reruns used `baseline_nanotrack_preprocess_tests`.

| Requirement/location | Scenario description | Implementation/test/evidence | Result |
|---|---|---|---|
| Local conversion/padding, spec.md:5 | Small window inside large image | `nanotrack_preprocessor.cpp:56`–90 converts only intersection; actual 1920×1200 recovery inputs/windows | Passed |
| Same | Partly beyond left boundary | Wide-integer intersection/destination offsets, full mean padding, four-side/corner matrix | Passed; literal side-154 scenario cross-supported by same branch/other windows |
| Same | Initialization retains full-image mean | Unchanged `TargetMean::initialize`; `reuseAndFailures()` changes outside-window pixels | Passed |
| Numerical equality, spec.md:21 | Fractional interpolation | `cv::resize` still consumes Float32; `numeric()` checks fractions/exact old equality | Passed |
| Same | Odd/even sizes and fractional center | Fixed side 5/6, center `(2.25,1.75)`, independent crop origin | Passed |
| Same | Same model-input contract | Existing validation/three-plane copy; shape/channel/independent-error tests | Passed; old tiny floating tail differences retained |
| Full coverage, spec.md:37 | Inside window changes to outside window | `window_.setTo` fills outside window; same-size reuse/direct comparison | Passed |
| Same | Entirely outside image | Fixed `(20,18,5,5)` and other cases; no Rect for empty intersection, still maps | Passed |
| Same | Window larger than source | Fixed side 257/origin `(-126,-127)` and measured side 2048 | Passed |
| Stride/mapping, spec.md:53 | Padded image rows | 7×5 image, stride 11 bytes, distinct padding, interior/edge matrix | Passed |
| Same | Empty intersection with invalid mapping | `reuseAndFailures()` outside image/wrong stride, PlatformTypeMismatch/view release | Passed |
| Same | Result outlives source mapping | Expired weak source, zero mapping count, retained readable input | Passed |
| Lease/mean, spec.md:69 | Search result retained during another preparation | Search InputBusy/result protection; unchanged `ReusableInput::acquire` | Passed |
| Same | New template blocked by occupied cache | Template resets first, InputBusy then search ContractMismatch; succeeds after release | Passed |
| Same | Recover after search failure | Failed mapping then legal search; committed mean retained | Passed |
| Same | Result survives preprocessor destruction | Source release, two resets, destruction, retained shared_ptr read | Passed |
| Recovery work, spec.md:89 | One initialization plus twenty-four catch-up frames | Real NanoDcfTracker; raw 600 frames/1176 Run; controlled tests | Passed |
| Same | Middle historical-frame update fails | `recovery_regression_test.cpp:133`, real HybridTracker/detection thread, both paths continue | Passed |
| Same-condition measurement, spec.md:101 | Warmup separate from formal measurement | `recover()` allocation/reset outside; loading/decoding/writes outside; independent plan/tag audit | Passed |
| Same | Traceable input/model identity | 25 originals, pixel rerun, three hashes, code/system Runtime identity | Passed for measured board; standard-memory discrepancy W1 |
| Numerical/performance, spec.md:113 | Faster stage benefits complete recovery | Independent median/P95/input/work recomputation meets all limits | Passed in measured environment |
| Same | Insufficient benefit | `main.cpp:243` rejects, 256–257 writes failure/returns 1; output-difference control exists | Partial: performance-failure control missing, W2 |
| Same | Limits for other windows/historical field result | 192 other-window records; validation.md states 2048 regression/reconstruction limits | Passed |

The numerical matrix has 18 fixed geometries × 4 patterns × template/search, plus existing cases. Exact old-reference comparison and independent double-precision four-neighbor checks run separately. The latter uses neither production geometry nor resize and retains `5e-4`. The implementation report discloses attempted but unretained side-511 random input and side-257 0/255 checkerboard where the old path also exceeds ideal-reference tolerance. Low-contrast checkerboard success does not guarantee arbitrary high-contrast/large-window ideal error.

## Design, isolation and resource boundaries

All six decisions map to code: existing geometry/local intersection; local Float32 then original linear resize; separate serial workspace reuse; old-reference plus independent interpolation checks; real recovery without changing replay; same-condition board A/B.

Production semantics changed only in RK3576 preprocessing `.cpp/.h`. Public tracking, RKNN submission, thresholds, models, queues, ROS, production configuration, and Jetson were unchanged. Whole-image mean remains. Intersection uses int64_t and nonempty boundaries stay within source/window dimensions. Empty intersection is not converted into a negative Rect.

Local scope releases mappings before resize. Consumers never borrow workspace. Template/search outputs retain separate shared_ptr caches and are not overwritten before the last reference releases. Exceptions/returns release mappings/workspace references. Template mean commits only on complete success; failed template stays invalid and search failure preserves committed mean. Calls remain serial; no new concurrency semantics.

Old reference links only into `nanotrack_window_support` and tests. The new diagnostic target has no install rule, and the retained production-node link command contains neither reference nor window-support library. Parent CMake builds diagnostics without linking them into production. Baseline changes only reuse test source and include the new untracked directory in code identity; MJPEG/pure-NanoTrack scope is unchanged. Naming, guards, ownership, and neighboring patterns match. External queue code was untouched.

## Tests, failures and rerun interpretation

Independent ADB reran `nanotrack_window_tests`, `nanotrack_window_help`, `recovery_regression_tests`, `baseline_nanotrack_preprocess_tests`: 4/4 passed. The 25-frame read-only identity check passed. See [verification-board-readonly.txt](../production-20261012/verification-board-readonly.txt). These did not run NPU performance batches or disturb original measured data.

Existing full ARM64-container logs ended at 47/47. Initial native-board identity suite was 19/20, with one incremental-source-identity failure; it must not become a first-attempt 20/20. Lagging board calendar made system dependencies appear in the future, triggering rebuilding without source changes. `immutable-system-make --assume-old` lists only `/usr/include` dependencies, not source/identity files. Recursive Make wrapper rerun passed 1/1 while retaining modify/add/delete/stage/commit/Git-failure assertions. This proves the explicitly isolated rerun, not unconditional incremental success under the wrong clock.

Initial `failed-run-01` mistakenly counted the absent search parent stage as model calls and failed. Its exact-input/first-pair outputs remain, but it contributes none of the final ten pairs. The diagnostic fix checks actual Run and rejects context-free Run. Final binary identity matches independent ADB inspection. Final second-run events were recomputed without reusing the first diagnostic's inferred counts.

`openspec validate optimize-rk3576-nanotrack-window-preprocessing --strict` and `git diff --check` passed again. No assertions or implementation changed to obtain this verdict.

## Issues and actionable recommendations

### CRITICAL — required before archival

0 issues. No unimplemented requirement, unchecked task, or prohibited algorithm change was found.

### WARNING — should be addressed

1. **W1: standard production memory partly unverified.** ADB confirmed board `HZ-RK3576_WuKong Board`, kernel `6.1.141-rt36`, Runtime fingerprint, driver `0.9.8`, and deployed identity, but `MemTotal=8112044 kB` is about 7.74 GiB rather than the then-documented 3.8 GiB in `AGENTS.md`. `validation.md:30` disclosed this. Results belong to the identified ADB board and do not establish the 3.8 GiB standard configuration. Check procurement/asset specifications and reserved-memory configuration. If this was nonstandard, repeat numerical/A/B acceptance on the specified 3.8 GiB board; if documentation was stale, maintainers should correct it separately from hardware evidence. This verification neither guessed the cause nor changed the standard.
2. **W2: missing independent performance-failure control.** `window_preprocess/main.cpp:243`–257 had correct failure/exit logic, but `window_preprocess/tests/recovery_test.cpp` covered statistics/output/work differences without directly testing 30% preprocessing ratio or rising recovery P95. Add fixed-statistic negative controls to the same production verdict: ratio 0.30, nondecreasing recovery median, and increased P95 must reject. Reports/exit status cannot claim success. Do not manufacture benefit through frame skipping or changed models. This scenario had code inspection and partial failure testing, not full test coverage.

### SUGGESTION — optional improvement

0 issues. Historical neighboring style and out-of-scope work were not added as blockers.

## Unverified and inapplicable scope

- **Partly unverified scenario coverage:** 1 of 23 lacked performance-failure controls; see W2.
- **Unverified standard-memory consistency:** measured/documented values differ without explained cause; see W1.
- **Unverified C++ static tools:** image lacks clang-tidy/clangd and no project-approved command exists. Manual review/builds do not replace them.
- **Unexecuted unconditional board incremental rerun after clock repair:** system time was unchanged; only isolated system-dependency results exist.
- **No fresh complete ten-pair NPU A/B:** original raw/source/binary evidence supports independent recomputation. Verification reran only inputs and relevant numeric/flow tests, not another performance sample.
- **Inapplicable to this change:** Jetson execution, original uncompressed camera input/full historical workload, field approximately 388 ms decomposition, long stress, NPU device memory, production deployment. They were not prerequisites for this small-window change and are not passed by containers/reconstructed replay.
- No exhaustive elementwise proof for all legal inputs or ideal double-precision error guarantee for arbitrary high-contrast images. The measured matrix/trajectory is not an exhaustive proof.

## Production restoration and final assessment

Production remained running during verification; no performance measurement or camera capture started. Required cleanup restarted original `drone-tracker.service` through ADB, ending **active/running, MainPID=6107**. current remained `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`. Production binary/config/system Runtime fingerprints matched original evidence. See [verification-service-final.txt](../production-20261012/verification-service-final.txt). No optimized binary was deployed.

Checks found CRITICAL 0, WARNING 2, SUGGESTION 0. Specified reconstructed input on this measured board met numerical, workload, and performance conditions. Performance-failure scenario coverage, standard memory, and static-tool checks remained incomplete. This is neither “all checks passed” nor “unconditional archival ready.” Verification ended at the report without archival, synchronization, or commit.
