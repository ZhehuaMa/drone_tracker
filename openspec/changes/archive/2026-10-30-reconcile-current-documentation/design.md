# Design

## Context

The same maintainer can read contradictory descriptions of camera lifetime, model transfer, and recording support. This change replaces those descriptions with current behavior and repairs navigation to historical evidence. It does not alter running software. See [proposal.md](proposal.md) for scope.

The OpenSpec CLI resolves this repository as `/home/mzh/nickluo/drone_tracker`. Its archived changes are historical records, not selectable active changes. The archived shared-memory adoption contains eight requirements missing from the main specifications. Current source confirms those operations, with one important qualification: native tensor queries are required during session loading; only shared-memory operations are optional for ordinary sessions.

## Goals / Non-Goals

**Goals:**

- Make each corrected guide and specification independently understandable in English.
- Preserve the supported runtime contracts, numerical values, model roles, ownership rules, and acceptance limits.
- Restore missing specification coverage through a CLI-visible active reconciliation change.
- Keep the one-consumer camera queue compatible with the existing dispatcher, which forwards frames to preview or the latest pending tracking task.
- Make cache and evidence retention responsibilities explicit.

**Non-Goals:**

- Changing code, parameters, models, service state, installed releases, or test behavior.
- Translating the entire archive, changing historical verdicts, or manufacturing missing evidence.
- Treating a current benchmark executable name as proof of a historical CPU-transfer path.

## Decisions

### Use a documentation reconciliation change

**Choice:** Create this change with the CLI, obtain schema instructions, author deltas, then use `openspec-sync-specs` to update the selected main specs. A delta is the list of additions and replacements applied to a current contract.

**Why:** Archived adoption cannot be selected directly with `openspec status --change`. An active change provides concrete artifact paths and current rules without reopening historical implementation work.

**Alternative:** Move the old change out of the archive or silently edit main specs without change context. That would disturb history or omit a reviewable migration.

**Consequence:** All five corrected capabilities use English requirements. The four translated capabilities have explicit removal mappings for their old titles; the build-workflow correction preserves its existing English title. Exact earlier titles remain only as schema-required identifiers in removal records. Equivalent scenarios are translated, with obsolete implementation-status scenarios replaced by current behavior.

### Document observed contracts without changing runtime policy

**Choice:** Verify documentation against source and configuration, retaining separate Jetson, video, non-OSD camera, and continuous-preview lifecycles.

**Why:** These modes intentionally have different stop and recording behavior. One universal OFF/freeze description is incorrect.

**Alternative:** Describe the production camera behavior as universal or restore code to match old prose. Either would change supported behavior or mislead another caller.

**Consequence:** Continuous preview has one source-queue consumer and bounded downstream dispatch. The template feature remains a host Float32 value, while production search features require compatible native FP16 device storage. Host Float32 means CPU-readable 32-bit floating-point values; native storage describes the representation used by the device.

### Retain historical identities and acknowledge disposable evidence

**Choice:** Correct broken authored-report links and translate corrected narrative documents while preserving their exact outcomes, identifiers, limits, and task states. Leave frozen raw evidence and source snapshots unchanged.

**Why:** Navigation and language can be repaired without rerunning or reinterpreting old experiments. `.docker-artifacts`, its backups, and `/tmp` can be cleared at any time.

**Alternative:** Delete old failures, claim missing cache paths are permanently retained, or translate all historical evidence. Those approaches would erase provenance or exceed the requested scope.

**Consequence:** An evidence path identifies where data was recorded, not a retention guarantee. Required raw data and manifests must be exported to explicitly retained storage before cleanup. No fresh hardware verdict follows from these edits.

## Migration Plan

1. Read current code, configuration, main specs, and relevant archived deltas.
2. Write English reconciliation artifacts and validate their structure with the CLI.
3. Merge only the five returned delta paths into main specs, preserving their contracts and translated scenarios.
4. Update operational instructions and repair authored-document links. Remove test-case/result narratives from `AGENTS.local.md`.
5. Check English prose, Markdown links, parameter-value preservation, whitespace, and OpenSpec validation. Record pre-existing validation warnings separately.

For example, the old search-transfer text says both features pass through host Float32 storage. The reconciliation imports the archived direct-transfer rules, checks the current loader and session code, then publishes the corrected English contract. No binary is rebuilt or deployed.

Rollback consists of reverting the documentation patch and restoring the local instruction file from a retained copy if needed. It requires no service or device action.

## Risks / Trade-offs

- **Translation loses a constraint:** Compare original requirements and scenario coverage, and obtain an independent source review before syncing.
- **Requirement titles change:** Keep explicit old-to-new mappings in delta removal records so the language migration is reviewable.
- **An old path is mistaken for available evidence:** Mark cache locations disposable and keep historical identities distinct from current availability.
- **Unrelated strict-validation warnings obscure this change:** Validate the corrected specs strictly and run the full normal validator; report existing warnings without rewriting unrelated capabilities.
