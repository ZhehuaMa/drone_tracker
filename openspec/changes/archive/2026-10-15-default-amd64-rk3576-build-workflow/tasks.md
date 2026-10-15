# Tasks

## 1. Shared backend selection and artifact safety

- [x] 1.1 Make `build.sh` and shared host/container helpers select amd64 by default, validate explicit image architecture, and retain ARM64 output; completion requires CLI help and fresh configure/build evidence.
- [x] 1.2 Protect per-backend caches and clean backups in the shared host helper; completion requires checks showing cache locking, selected-cache backup, and preserved legacy/other-backend files.
- [x] 1.3 Document backend selection, cache paths, and explicit deployment `--install-root` in build help and build documentation; completion requires commands consistent with the implemented CLI.

## 2. Architecture-correct SDK tests

- [x] 2.1 Make `prepare_test_tools.sh` validate image-provided amd64 tools or explicitly prepare the ARM64 private tools; completion requires successful amd64 tool preparation without downloads and actionable missing-tool output.
- [x] 2.2 Make `run_sdk_tests.sh` delegate to the shared build/test workflow while preserving `--regression-only`; completion requires the helper to configure with explicit fixture-tool paths and preserve failure status.
- [x] 2.3 Register and supply the native/target Python coordinator, explicit child launcher, and target loader inspection helper for ARM-extension regression cases; completion requires target Python message imports and the regression Python CTest to pass without relaxing assertions or deadlines.
- [x] 2.4 Document native fixture-tool selection and target Python execution in `tests/tracking_regression/README.md`; completion requires the documented ordinary and explicit-fallback test commands to match the helpers.
- [x] 2.5 Preserve strict failure coverage in the source and OSD C++ test harnesses; completion requires verified real allocation injection, passing failure/evidence/no-clock assertions, the unchanged public-state deadline, and a separate passing native OS memory-limit check.
- [x] 2.6 Preserve exact descriptor cleanup coverage in the diagnostic C++ test harness; completion requires a bounded idle baseline, the original equality and deadline, and a failing retained-descriptor negative control. Keep the OSD active input fresh without changing its stale limit.
- [x] 2.7 Provide a reproducible pinned emulator correctness repair and explicit diagnostic/target-Python child routing; completion requires source/package/patch/binary checksums, strict descriptor positive and retained-descriptor negative controls, and affected final-image tests without host emulator registration changes.

## 3. Integration verification

- [x] 3.1 Verify the real default `./build.sh --test` entry point in an external artifact root; record ARM64 output identities, exact CTest results, image identity, and any remaining failure without silent fallback.
- [x] 3.2 Verify the explicit ARM64 fallback selection and prerequisite path in a separate artifact root; record actual build/test coverage without presenting unavailable checks as passed.
- [x] 3.3 Validate OpenSpec artifacts with `openspec validate default-amd64-rk3576-build-workflow --strict` and record the change as implemented only for completed tasks; do not archive automatically.
