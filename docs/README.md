# Documentation index

[Project overview](../README.md)

Use the root README for setup and a minimal run. This index links to detailed guides and references. Directory READMEs remain beside the code or tools they explain; standalone documents are grouped here by purpose.

## Build, configuration, and operation

| Document | Covers |
|---|---|
| [Image and build guide](../dockerfiles/README.amd64.md) | Compiler image setup, ARM64 output, test environments, caches, and packaging inputs |
| [Session topic recording](operations/RK3576_SESSION_RECORDING.md) | Fixed catalog, source timestamps, bounded admission, sampling and storage |
| [Parameter templates](../parameters/README.md) | Camera/video configuration and startup settings |
| [Platform guide](../src/platform/README.md) | Model integration, input lifecycles, standalone tools, and ROS observations |
| [HDMI display guide](operations/RK3576_HDMI_OSD.md) | Display setup, telemetry, renderers, timing, and rollback |
| [Recording guide](../src/platform/rk3576/recording/README.md) | Independent original-camera recording and inspection |
| [Deployment guide](../deployment/README.md) | Production packages, board preparation, services, and rollback |
| [Diagnostics guide](../src/diagnostics/README.md) | Tracking event collection, storage, inspection, and measurement limits |

## Architecture and algorithm

- [Jetson-to-RK3576 architecture and remaining validation](architecture/RK3576_REFACTORING_PLAN.md): responsibilities, platform boundaries, frame ownership, model contracts, and acceptance gaps.
- [Hybrid tracker algorithm review](architecture/ALGORITHM_REVIEW.md): detection scheduling, tracking, center fusion, recovery, and rotation-compensated output.
- [Shared tracker directory](../src/hybrid_tracker/README.md): source entry points and portable verification commands.

## Testing and measurement

- [Tracking regression tools](../tests/tracking_regression/README.md): configure, prepare, run, freeze references, and compare candidate results.
- [Regression metrics](testing/tracking-regression/metrics.md): formulas, sample windows, unavailable results, and comparison rules.
- [Regression diagnostic contract](testing/tracking-regression/diagnostic_contract.md): event formats, identity matching, observation fields, and CPU evidence.
- [Ten-pair diagnostic acceptance procedure](testing/diagnostics/ten_round_acceptance.md): matched off/basic measurements, deadlines, cleanup, and independent recomputation.
- [NanoTrackV3 benchmark tools](../tests/nanotracev3_performance_benchmarks/README.md): baseline, shared-device-memory handoff, and local-window preprocessing tools.

## Dated reports

These records describe their original software, boards, models, inputs, and checks. Moving a report does not rerun its experiment or change its conclusions. Original report text and evidence identities are preserved, including reports originally written in Chinese. Artifact paths may refer to disposable output that no longer exists.

| Report | Recorded scope |
|---|---|
| [Topic-recorder CPU placement, 2026-11-13](reports/performance/RECORDER_CORE_AFFINITY_20261113.md) | Eight recorder placements and a disabled control, per-core CPU, paired outputs, latency, overhead comparison, and CPU 2 recommendation |
| [Production-board per-core performance, 2026-11-13](reports/performance/PRODUCTION_PER_CORE_20261113.md) | Image-off target replays, all eight CPU cores, real Tracking coverage, recording comparison, and measurement overhead |
| [Tracking regression integration record](reports/tracking-regression/acceptance.md) | Earlier production-board regression integration and affinity experiments |
| [Board 2 NanoTrack comparison, 2026-09-21](reports/nanotrackv3/BOARD2_COMPARISON_20260921.md) | Baseline versus shared-device-memory search-feature handoff |
| [Detection/tracking evaluation, 2026-10-15](reports/detection-tracking/improve-detection-tracking-implementation-20261015.md) | Implementation evaluation and retained evidence |
| [Regression against 7f4853a, 2026-10-15](reports/detection-tracking/improve-detection-tracking-vs-7f4853a-20261015.md) | Recording-based behavior comparison |
| [Regression against cb977c2, 2026-10-15](reports/detection-tracking/improve-detection-tracking-vs-cb977c2-20261015.md) | Recording-based behavior comparison |

## Specifications and change records

[Main specifications](../openspec/specs/) define approved behavior. [Active changes and archives](../openspec/changes/) contain plans and their evidence. OpenSpec manages that directory separately from this documentation layout. Historical OpenSpec text retains the paths used when it was recorded.

Use this table to locate standalone documents cited at their previous paths:

| Previous repository path | Current document |
|---|---|
| `RK3576_REFACTORING_PLAN.md` | [Architecture](architecture/RK3576_REFACTORING_PLAN.md) |
| `src/hybrid_tracker/ALGORITHM_REVIEW.md` | [Algorithm review](architecture/ALGORITHM_REVIEW.md) |
| `tests/tracking_regression/metrics.md` | [Regression metrics](testing/tracking-regression/metrics.md) |
| `tests/tracking_regression/diagnostic_contract.md` | [Diagnostic contract](testing/tracking-regression/diagnostic_contract.md) |
| `src/diagnostics/tests/ten_round_acceptance.md` | [Diagnostic acceptance procedure](testing/diagnostics/ten_round_acceptance.md) |
| `tests/tracking_regression/acceptance.md` | [Regression integration record](reports/tracking-regression/acceptance.md) |
| `tests/nanotracev3_performance_benchmarks/BOARD2_COMPARISON_20260921.md` | [Board 2 comparison](reports/nanotrackv3/BOARD2_COMPARISON_20260921.md) |
| `docs/RK3576_HDMI_OSD.md` | [HDMI guide](operations/RK3576_HDMI_OSD.md) |
| `docs/improve-detection-tracking-implementation-20261015.md` | [Implementation evaluation](reports/detection-tracking/improve-detection-tracking-implementation-20261015.md) |
| `docs/improve-detection-tracking-vs-7f4853a-20261015.md` | [7f4853a comparison](reports/detection-tracking/improve-detection-tracking-vs-7f4853a-20261015.md) |
| `docs/improve-detection-tracking-vs-cb977c2-20261015.md` | [cb977c2 comparison](reports/detection-tracking/improve-detection-tracking-vs-cb977c2-20261015.md) |
