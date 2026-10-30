# Proposal

## Why

Maintainers can follow the current guides and still choose an unsupported model configuration, expect stopped camera preview, or run a retired comparison command. The documents mix earlier migration stages with the implemented system and describe disposable build artifacts as retained evidence.

## What Changes

- Replace obsolete descriptions with the implemented platform, recording, preview, mission, observation, diagnostic, and regression behavior.
- Restore the eight shared-memory requirements from the archived `adopt-nanotrack-search-direct-handoff` change to the main specifications.
- Reconcile the platform factory's single-consumer rule with the implemented continuous-preview dispatcher.
- Describe both current NanoTrack benchmark loaders accurately. Historical CPU-transfer comparisons retain their original build identities; current executable names alone do not establish that comparison.
- Write corrected documents in English. English requirement titles replace their earlier titles through explicit delta operations; the earlier titles remain only as exact migration identifiers.
- Remove test-case descriptions and historical test/deployment narratives from `AGENTS.local.md`.
- Treat `.docker-artifacts` and `/tmp` as disposable locations. Required evidence must be exported to separately managed storage before cleanup.

For example, a maintainer previously following the camera guide could send OFF and expect frozen video. The corrected sequence is: start an OSD-enabled camera service, observe preview, send ON to track, then send OFF to clear targets while preview continues. Stopping the service releases the devices.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `rk3576-model-session`: restore the already implemented native-attribute, partial-submission, binding, completion, and shared-memory lifecycle contracts.
- `rk3576-nanotrack-backend`: replace the obsolete all-host-transfer description with the implemented search-feature contract and its compatibility/failure boundaries.
- `platform-factory`: qualify the original queue rule for continuous preview while retaining one consumer of the camera queue.
- `nanotrackv3-search-direct-benchmark`: distinguish historical CPU-transfer comparisons from the two current shared-memory loaders.
- `rk3576-build-workflow`: replace a stale fixed Python-case count with the existing discovery-based architecture partition.

These are corrections to the documented contracts. They require no change to runtime behavior.

## Impact

Affected files are the five named main specifications, their reconciliation deltas, repository instructions, and the operational guides identified in the documentation audit. Camera-template comments are corrected without changing parameter values. Historical reports and raw evidence retain their original results. No C++, Python, shell implementation, model, ROS interface, CPU assignment, service, or deployed release changes are included.
