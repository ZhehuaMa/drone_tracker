# NanoTrackV3 performance benchmarks

[Project overview](../../README.md) · [Documentation index](../../docs/README.md)

These standalone tools measure NanoTrackV3 preprocessing, model execution, and feature handoff. They do not run the complete ROS tracker with YOLO detection. Use each tool's README for its configuration, commands, model requirements, and measurement limits.

| Tool | Purpose |
|---|---|
| [baseline/](baseline/README.md) | Reference path with search features transferred through Float32 host memory |
| [search_direct/](search_direct/README.md) | Search features passed through shared device memory in the required native FP16 layout |
| [window_preprocess/](window_preprocess/README.md) | Local-window preprocessing checks and performance comparison |

FP16 and Float32 are 16-bit and 32-bit floating-point formats. A model's advertised precision does not establish that its memory layout matches the shared-memory path. Follow the complete model contracts in each guide.

The dated [Board 2 comparison](../../docs/reports/nanotrackv3/BOARD2_COMPARISON_20260921.md) records the 2026-09-21 baseline/search-direct experiment. It applies only to that experiment's board, binary, models, and input. Use the [image and build guide](../../dockerfiles/README.amd64.md) for setup; actual model and performance acceptance requires the identified target board.
