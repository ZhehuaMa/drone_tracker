# Workflow verification

The ordinary tracker entry points now use an x86_64 compiler container and produce ARM64 deployment executables. The explicit `--backend arm64` option retains the existing compiler image. Routine containers have no network, read-only source and Git metadata, and separate backend caches. A cache records its image ID and toolchain checksum; changed or unverified caches require a preserving `--clean` backup.

## Frozen environment

- Current image: `drone-tracker-rk3576:humble-amd64-rknn2.3.2`, ID `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`, Linux amd64.
- Previous image: ID `sha256:156c071fcff2385947d0adaea176b29be1b3fb110d2969ebddfda39f2d9d99fd`, preserved as `drone-tracker-rk3576:humble-amd64-rknn2.3.2-before-qemu-repair-20261015`.
- Existing ARM64 compiler/SDK image: ID `sha256:620524fe768a8c7664a24c40504c86dfea35538d53b2da63b6dbf4cc4849293d`, unchanged.

The current image uses the explicitly named local Ubuntu/ROS package snapshot described in `dockerfiles/README.amd64.md`. It does not claim official registry-base provenance. Native ROS/OpenCV compilation and execution passed. The same program cross-compiled, linked against genuine ARM libraries, and ran under the selected emulator. Its Python extension ABI is `cpython-310-aarch64-linux-gnu`. The original RKNN Runtime 2.3.2 library retains SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.

## Main entry-point checks

```bash
./build.sh --test \
  --artifacts-dir /tmp/nickluo-build-workflow-20261015/final-image/artifacts \
  --jobs 6
```

The ordinary entry point passed all 57 CTests in 156.28 seconds. The separate mandatory native OS memory-limit CTest passed in 5.67 seconds. The target source suite passed in 71.26 seconds within its unchanged 90-second deadline. The final complete result is recorded in `/tmp/nickluo-build-workflow-20261015/final-image/default-build-test.log`. Output and per-test logs are under its `artifacts/backends/amd64/` directory. The helper verifies the installed node's ELF machine type as AArch64. An ELF file is an executable or shared-library file; this field establishes that the deployment output targets ARM64 even though the compiler runs on x86_64.

The mandatory native `tracking_regression_native_resource_limit` check exercises the real host address-space limit. The target source suite separately throws a real cache-allocation exception and preserves the original failure-state, evidence, and no-playback-clock assertions. No production allocator was changed.

The Python CTest discovers 53 unique IDs. It runs 52 portable cases natively, including real Linux orphan-process supervision and freshly compiled native loader fixtures. The actual ARM ROS middleware case runs on genuine ARM Python. Its loader and ctypes subprocesses explicitly enter the selected emulator. The single aggregate timeout remains 30 seconds. Independent checks confirmed exactly-once execution with no skips. Deliberate native-case and ARM-case failures each returned status 1. Positive execution took 8.82 seconds independently and 8.74 seconds through the ordinary entry point.

Six host CLI contract checks pass. They cover architecture rejection, explicit fallback selection, offline/read-only container arguments, preserved clean backups, lock protection, image-change rejection, and propagation of container failure without retry. These use a fake Docker command to inspect CLI behavior; they are distinct from the real image tests above.

## Corrected emulator and strict controls

The tracked setup script builds a static QEMU 8.2.2 from official source SHA-256 `5e692dd20950a819ff6550cd3974e00319a93f47c0a15f808c58df1a9e4d2478`. The exact descriptor-ownership patch has SHA-256 `a2838c7f61ec52a29a59e5622a2d69a488a7f5a58d761d0276188702a42ad4e9`. The installed binary has SHA-256 `da420e56e1d0199413c081067d647936af23123b9eefdb859b1facb92cccaddc`. Its compiler, library, helper versions, comparison-package checksum, and build commands are preserved in the generated manifest and setup logs.

Independent verification used the installed binary from the frozen final image, without a mounted emulator override:

- Seven Unix descriptor-message buffer capacities passed. A descriptor is an operating-system handle to an open resource.
- Three strict ARM diagnostic runs passed with exact descriptor cleanup.
- A deliberate transferred-descriptor leak failed with status 1.
- A worker deliberately retaining a descriptor failed with status 1 and `rejected descriptor leak`.
- The OSD correction passed 20 repeated ARM runs with the original 200 ms mission-following deadline. Its other original deadlines and 1000 ms stale threshold remain unchanged.

The emulator controls and Python architecture controls are under `/tmp/nickluo-native-default-investigation-20261015/root/final-image-logs/`. The corrected OSD repeats are specifically `root/logs/osd-deadlines-status.log` and `osd-deadlines-*.log`; the earlier `osd-repeat-fixed-*` logs predate the restored 200 ms deadline.

## Explicit ARM64 fallback

The real `./build.sh --backend arm64 --artifacts-dir /tmp/nickluo-build-workflow-20261015/arm64-fallback --jobs 6` entry built and installed the production ARM executable. Its successful repeat is `arm64-build-rerun.log`. Tool preparation verified the retained private ARM FFmpeg tools in `prepare-arm64.log`. The amd64 tool-preparation path verified the final image's native tools without downloads in `final-image/prepare-tools.log`.

This records a successful fallback build and prerequisite check, not a fully passing fallback test suite. The fallback retains the real `tracking_regression_os_resource_limit` assertion. On an actual ARM kernel that checks native OS behavior. On this x86_64 host the ARM image is emulated, and QEMU can ignore guest `RLIMIT_AS`; that assertion remains enabled and can fail. Its older emulator also lacks process-descriptor support needed by strict diagnostics. No automatic switch to the amd64 backend conceals those limits.

## Retained failures and boundaries

The first helper run on image 156c failed seven CTests. Four were corrected launcher environment paths; the remaining failures exposed strict diagnostic, OSD, and source-harness issues. The first run on final image 025d passed 56 of 57 CTests, with the Python group failing because QEMU 8.2 does not implement the child-subreaper syscall. That unsupported guest call was not reported as passing. The native/target coordinator tests process supervision on the real native kernel and ARM middleware on ARM Python.

The older failures remain in `default-build-test.log` at the workflow evidence root and `final-image/default-build-test-all-arm-python-failed.log`. The final rerun uses `final-image/default-build-test.log`. Source manifests and before/after differences retain the changing dirty-worktree identity, including new recipe files. Existing user changes remain uncommitted. No commits, pushes, board access, or host emulator-registration changes occurred.

These results cover compilation and device-free tests. They do not establish NPU inference, camera/HDMI operation, recorded-video/model acceptance, flight behavior, or sustained board load. At the time of the implementation verification above, this change had not yet been archived.


## Authorized synchronization and archive — 2026-10-15

The user subsequently requested synchronization, archive, and a commit of all repository changes on a new branch. The `spec-driven` artifact graph is complete, and the CLI reports 13 of 13 tasks complete. No task completion markers were changed during archiving.

All 12 added requirements were synchronized into `openspec/specs/rk3576-build-workflow/spec.md`. The authored purpose and all requirement bodies were preserved. The synchronized main specification and the change both passed strict validation before the archive move. `openspec validate --specs` passed all 24 specifications; existing requirement-length warnings in unrelated specifications were left unchanged.

The six build-backend contract tests passed again. Syntax checks passed for all 11 changed or new shell scripts and all changed or new Python files. The recorded full-image tests were not rerun for this archive operation. Existing failures and hardware-validation boundaries above remain in force.

Archive destination: `openspec/changes/archive/2026-10-15-default-amd64-rk3576-build-workflow/`. The earlier statement that work was uncommitted describes the implementation-verification stage, before this explicitly authorized commit. No push or board deployment is part of this operation.
