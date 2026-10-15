# Proposal

## Why

The existing build and test commands always run an emulated ARM64 container, even though a native x86_64 compiler image is now available. `./build.sh --test` also lacks the fixture tools required by its own test configuration, so the ordinary test command fails before building.

## What Changes

- Default the existing build and SDK-test commands to the x86_64 image while retaining ARM64 deployment output through cross-compilation: an x86_64 compiler produces ARM64 executables.
- Provide an explicit `--backend arm64` selection for the existing ARM64-image workflow. A failed build or test never silently switches backends.
- Separate backend build/install/log caches, preserve old caches, and back up selected caches before `--clean` builds.
- Mount source and Git metadata read-only. Keep routine build/test containers offline.
- Make test prerequisites automatic when already supplied by the image. For a missing private ARM64 FFmpeg tool, fail before compilation with an explicit preparation command.
- Use native FFmpeg tools for synthetic fixture generation in the x86_64 workflow. Run Python tests that import ARM64 extensions with genuine target Python and target libraries.
- Keep production tracking behavior, acceptance assertions, and existing test deadlines unchanged.
- Correct test-only assumptions exposed by emulation: inject a real cache-allocation failure while retaining a separate native OS memory-limit check; sample the idle descriptor baseline within its existing bound; keep the OSD freshness test's input alive and wait for its public completion counter within the existing deadline.
- Pin a compatible user-mode emulator and a minimal correctness patch for truncated Unix descriptor delivery. Route diagnostic main/worker/inspector and target Python explicitly through that emulator without changing host emulator registration.

For example, `./build.sh --test` will select the x86_64 image, generate ARM64 programs in its own cache, and execute the existing suite with the appropriate native or ARM tools. `./build.sh --backend arm64 --test` will explicitly select the previous image and a separate cache.

## Capabilities

### New Capabilities

- `rk3576-build-workflow`: Observable build/test command selection, deployment-output architecture, cache safety, prerequisite checks, and architecture-correct test execution.

### Modified Capabilities

None. Existing production packaging keeps its explicit `--install-root` override and historical default. Regression acceptance criteria remain unchanged.

## Impact

Affected files are `build.sh`, `tests/tracking_regression/{prepare_test_tools.sh,run_sdk_tests.sh,CMakeLists.txt}`, new shared shell helpers and target-Python launchers, the regression source/OSD test harnesses, and their build/test documentation. The existing amd64 and arm64 Docker images remain distinct dependencies. The workflow requires Docker and registered ARM64 emulation for ARM subprocesses on this x86_64 host. It does not establish camera, NPU, HDMI, or flight acceptance.
