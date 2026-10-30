# Documentation reconciliation verification

Date: 2026-10-30. Reviewed checkout: `rk3576`, starting at `8c8925a`.

## Delivered scope

Corrected the affected operational guides, repository instructions, camera-template comments, five main specifications, and eleven authored historical reports. Removed obsolete descriptions instead of retaining conflicting append-only updates. Corrected prose is English. The earlier non-English requirement titles appear only as exact identifiers in delta removal records, where OpenSpec needs them to identify the replaced contracts.

The main specifications now cover production shared-memory transfer, session ownership and failure behavior, continuous-preview dispatch, current benchmark limitations, and discovery-based Python-test partitioning. The eight omitted requirements from the archived shared-memory adoption are represented in the corrected model-session and NanoTrack contracts. Native tensor queries are explicitly required during current session loading; only shared-memory operations are optional for ordinary execution.

`AGENTS.local.md` now contains current local environment and workflow guidance. It contains no test-case descriptions, result counts, or historical deployment/test narratives. It remains Git-ignored and was updated on disk.

`.docker-artifacts`, its backups, and `/tmp` are documented as disposable. Historical paths describe provenance and do not guarantee current availability. Required evidence must be copied to separately managed storage before cleanup. This task did not clear artifact directories.

## Verification results

| Check | Result |
|---|---|
| `openspec validate reconcile-current-documentation --strict --json` | Passed without warnings |
| Strict validation of each corrected main spec | All five passed without warnings: `rk3576-model-session`, `rk3576-nanotrack-backend`, `platform-factory`, `nanotrackv3-search-direct-benchmark`, and `rk3576-build-workflow` |
| `openspec validate --all --json --report findings` before archival | All 29 items passed: 26 main specs and three active changes |
| Repository Markdown links and fragments | No missing local targets or heading fragments; repaired the 38 definite broken Markdown links identified in the audit |
| English-language check | Corrected prose is English; exact historical removal identifiers are preserved as noted above |
| Historical identity preservation | All 60 SHA-256 occurrences in the eleven corrected archive documents are unchanged |
| Historical task preservation | All 34 task checkbox states and numeric task IDs are unchanged |
| Historical regression tables | Numerical tokens in the long affinity and consume-before-request report tables are unchanged |
| Camera-template preservation | Noncomment YAML content matches the original file; the intrinsic-value comment now describes 1333.3 |
| Patch scope | Existing-file changes are Markdown or camera-template comments; added change metadata and artifacts are documentation only |
| `git diff --check` | Passed |

Normal validation still reports 25 pre-existing requirement-length warnings in unrelated specifications and active changes. Those warnings make a global strict run fail; they do not affect the six strict checks above. This change does not rewrite unrelated capabilities to remove those warnings.

## Synchronization and archival

On 2026-10-30, the `openspec-sync-specs` workflow compared every operation in all five deltas with its main specification. All 41 added requirement blocks and the one modified requirement block matched exactly. All 32 removed requirement titles were absent. Synchronization was already complete, so this check required no further main-spec edits.

After that comparison, `openspec archive reconcile-current-documentation --skip-specs --yes --json` archived the change to `openspec/changes/archive/2026-10-30-reconcile-current-documentation/`. The `--skip-specs` flag prevented the CLI from replaying operations that had already been applied. The archive preserved `.openspec.yaml` and all seven completed tasks.

Post-archive normal validation passed all 28 remaining items: 26 main specifications and two active changes. It retained the same 25 unrelated requirement-length warnings. The diagnostic-process change remains at 39 of 40 completed tasks; the replay change remains at 31 of 33. Archival does not establish acceptance for either change.

`openspec validate --archived --json --report findings` passed the task-completion check for all 43 archived changes without findings. The moved change's local Markdown links resolve, its metadata hash is unchanged, and `git diff --check` passed.

Archive verification completed before the parent commit; Git history records the commit.

## Evidence and execution limits

The checks establish documentation structure, navigation, language, preserved values, and consistency with reviewed source. They do not establish new model, camera, HDMI, timing, resource, or flight acceptance. No C++/Python/shell implementation, runtime parameter value, model, CPU assignment, service, or deployed release was changed. No build or hardware test was run for this documentation-only patch.

Three sub-agents handled guide correction, instruction/history cleanup, and independent review. A second source review corrected inherited session-query/completion wording and retained the benchmark's FP16 evidence requirement before final validation.

Temporary command output was written outside the repository during verification. It is disposable. This tracked report records the results without claiming permanent retention of those temporary files. Frozen raw evidence, earlier archived delta specs, and the unfinished acceptance states of unrelated active changes remain unchanged.
