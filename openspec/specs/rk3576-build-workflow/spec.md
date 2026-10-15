# rk3576-build-workflow Specification

## Purpose

Provide one reproducible RK3576 build/test workflow that uses native x86_64 compilation by default while preserving ARM64 deployment output and an explicit ARM64-image fallback.

## Requirements

### Requirement: Default native compiler backend retains deployment architecture
The build and SDK-test entry points SHALL default to the amd64 compiler image. The installed tracker executables SHALL remain ARM64 binaries suitable for the existing deployment architecture.

#### Scenario: Ordinary build
- **WHEN** a user runs `./build.sh` with the prepared amd64 image
- **THEN** the command uses a linux/amd64 container and installs ARM64 tracker executables

### Requirement: Backend selection is explicit and failures propagate
The entry points SHALL accept `--backend amd64|arm64`. They SHALL reject an image whose architecture differs from the selected backend. Build and test failures SHALL return a nonzero status without silently selecting another backend.

#### Scenario: Explicit ARM64 image
- **WHEN** the user runs `./build.sh --backend arm64`
- **THEN** the command uses the existing linux/arm64 image and its own cache

#### Scenario: Failing test
- **WHEN** a selected test fails under amd64
- **THEN** the command returns that failure without rerunning it under arm64

### Requirement: Backend artifacts and cleanup preserve other work
Build, install, and log caches SHALL be separate for each backend. `--clean` SHALL preserve the selected backend's previous directories in a new backup. Existing legacy caches and the other backend's caches SHALL remain untouched. Concurrent commands SHALL NOT mutate the same selected cache. A persisted image ID, deployment target, and toolchain checksum SHALL match before cache reuse. A mismatch or unverified populated cache SHALL require `--clean` before configuration.

#### Scenario: Clean amd64 build
- **WHEN** amd64 and arm64 caches exist and the user selects `--backend amd64 --clean`
- **THEN** the previous amd64 directories are backed up and the arm64 and legacy directories remain intact

#### Scenario: Compiler image changes
- **WHEN** the selected backend cache records a different image ID or toolchain checksum
- **THEN** the command fails before running a container and explains how `--clean` preserves the old output before a fresh configure

### Requirement: Routine build and test operations are isolated
Routine build/test containers SHALL have no network access. Source and Git metadata SHALL be mounted read-only. Generated files SHALL be written to the selected artifact directory or temporary container storage.

#### Scenario: Running tests from a managed worktree
- **WHEN** the selected source uses Git metadata outside its directory
- **THEN** provenance checks can read that metadata and cannot modify it

### Requirement: Test prerequisites are checked before compilation
`./build.sh --test` and the SDK-test helper SHALL validate executable FFmpeg fixture tools before compilation. Image-provided tools SHALL work without a separate download. Missing private tools SHALL produce the exact preparation command for the selected backend and a nonzero exit.

#### Scenario: Prepared amd64 image
- **WHEN** the image provides native `ffmpeg` and `ffprobe`
- **THEN** the ordinary test command passes their locations to CMake without network access

#### Scenario: Missing ARM64 private tools
- **WHEN** the selected ARM64 image lacks fixture tools and no private tools are available
- **THEN** the command fails before compilation and prints the explicit ARM64 preparation command

### Requirement: Test processes use the matching architecture
The amd64 workflow SHALL use native fixture-generation tools. Python cases that import target extensions SHALL run with genuine ARM64 Python and the target dynamic loader. Existing test assertions and deadlines SHALL remain unchanged.

#### Scenario: Target Python message import
- **WHEN** a regression Python case imports the generated ARM64 message type support
- **THEN** ARM64 Python loads that extension and the target dependency inspection uses the ARM64 loader

### Requirement: Selected outputs are discoverable
Build completion and help SHALL identify the selected backend and its install/log paths. The documented production-package command SHALL pass the selected install directory explicitly so a stale legacy install is not packaged accidentally.

#### Scenario: Packaging a new default build
- **WHEN** the user follows the documented amd64 build/package sequence
- **THEN** the package command selects the new backend's ARM64 install path explicitly

### Requirement: Emulated failure checks preserve their acceptance conditions
The target cache-allocation test SHALL trigger and verify a real allocation failure. It SHALL retain the failure-state, evidence, and no-playback-clock assertions. A native check SHALL retain the real OS address-space limit. Asynchronous OSD checks SHALL wait on the public displayed state within their original deadline.

#### Scenario: Allocation failure under emulation
- **WHEN** QEMU does not enforce a guest address-space limit
- **THEN** the target check still proves a real allocation failure with complete rejection evidence and native OS-limit coverage remains available

### Requirement: Asynchronous test observations preserve exact limits
Descriptor cleanup checks SHALL retain exact baseline equality and their original deadline. Their idle baseline SHALL exclude transient storage-scan descriptors using bounded observation. A retained-descriptor negative control SHALL remain detectable. OSD test input SHALL remain fresh during its intended active period without changing the production stale limit.

#### Scenario: Idle worker scans directories
- **WHEN** a transient storage scan opens directory descriptors during baseline sampling
- **THEN** the bounded baseline observation excludes those temporary descriptors and a deliberately retained descriptor still fails exact cleanup

### Requirement: Selected emulator preserves Linux resource ownership
The default test environment SHALL close host resource handles that a truncated Unix socket message does not deliver to the ARM guest. Its emulator source, correctness patch, and resulting binary SHALL have recorded content checksums. Strict cleanup assertions SHALL remain in force.

#### Scenario: Truncated descriptor message
- **WHEN** a socket message supplies more descriptors than the guest receive buffer can hold
- **THEN** descriptors not delivered to the guest are closed and a separately retained guest descriptor is still detected by the cleanup test

### Requirement: Required ARM subprocesses use the selected emulator explicitly
The default workflow SHALL route diagnostic main, worker, inspector, and target Python processes through its selected emulator explicitly. It SHALL NOT modify host emulator registration. It SHALL retain native execution for native compiler and fixture-tool subprocesses.

#### Scenario: Host still has an older registered emulator
- **WHEN** the host's registered ARM emulator is older than the image's selected emulator
- **THEN** the explicitly routed diagnostic and target Python processes use the selected image emulator without changing host configuration

### Requirement: Python tests execute on the architecture they exercise
A single SDK Python CTest SHALL run portable controller and native-loader checks using native Python. It SHALL run the ARM middleware identity case using genuine ARM Python. Discovery SHALL preserve every unique test ID exactly once across the two groups. Either group failing or skipping a case SHALL fail the CTest. The original aggregate 30-second deadline SHALL remain unchanged. ARM Python children SHALL reenter the selected emulator explicitly.

#### Scenario: Native supervisor and actual ARM middleware
- **WHEN** the Python SDK CTest discovers the current 53 cases
- **THEN** it runs 52 portable cases natively and the actual ARM middleware case on ARM Python, including its ctypes child, without relying on host emulator registration

#### Scenario: Classification drops or duplicates a test
- **WHEN** test discovery or partitioning loses an ID or produces a duplicate
- **THEN** the coordinator fails before reporting the SDK Python group as passed
