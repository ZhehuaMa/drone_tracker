# Native x86_64 build image

`drone-tracker-rk3576:humble-amd64-rknn2.3.2` runs Linux x86_64 tools directly on an x86_64 host. It contains ROS 2 Humble, OpenCV, Eigen, FFmpeg, and the dependencies used by the sibling ROS packages. The existing ARM64 image remains the source of the production SDK.

The image delivered on 2026-10-15 uses a **local Ubuntu/ROS package snapshot** as its native base. Official registry downloads stalled. Its final frozen ID is `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`. The local fallback and its provenance are documented below. The default official-base command is a separate rebuild option.

The image separates two uses:

- Native builds produce x86_64 executables. They cover `quadrotor_msgs`, `apm_bridge`, `drone_interceptor`, the portable tracker targets, and the portable `rk_hdmi_osd` core.
- Cross-compilation runs x86_64 compilers that produce ARM64 executables. The target libraries live under `/opt/arm64-sysroot`. They include the real ROS, RKNN Runtime 2.3.2, RGA, and MPP files copied from the existing ARM64 image. The compiler never treats those libraries as native x86_64 implementations.

The complete tracker and `osd_bridge` require ARM64 target libraries. Their existing architecture checks remain enabled. The image does not add an x86_64 RKNN Runtime or provide an RK3576 device driver.

## Prepare the corrected ARM emulator

The amd64 test workflow uses a native static QEMU 8.2.2 executable. It runs ARM64 programs explicitly, without modifying the host's registered emulator. A small tracked patch restores Linux descriptor ownership when a Unix socket message contains more descriptors than the ARM receive buffer can hold. Unpatched QEMU leaks the descriptors that it cannot deliver. The test still requires exact cleanup and rejects a deliberately retained descriptor.

This preparation is a separate setup operation. Routine compilation and tests use no network. The preparation script never installs host packages. On a Linux x86_64 build host, explicitly prepare `build-essential`, `pkg-config`, `libglib2.0-dev`, its static PCRE dependency (`libpcre3-dev` on this Ubuntu 22.04 host), `zlib1g-dev`, `python3-pip`, `python3-venv`, `patch`, and `xz-utils`. Missing static dependencies produce an early link error.

Create Python build helpers outside the repository, then prepare the ignored emulator input:

```bash
python3 -m pip install --target /tmp/nickluo-qemu-python-tools \
  meson==1.2.3 ninja==1.11.1.1 tomli==2.0.1
bash dockerfiles/scripts/prepare-patched-qemu.sh \
  --python-tools /tmp/nickluo-qemu-python-tools
```

For offline preparation, add `--source-archive /absolute/qemu_8.2.2+ds.orig.tar.xz`. The script verifies official source SHA-256 `5e692dd20950a819ff6550cd3974e00319a93f47c0a15f808c58df1a9e4d2478`, applies the exact tracked patch without fuzzy matching, and builds in a fresh external directory. It writes the static binary, checksum, and compiler/library provenance to ignored `dockerfiles/vendor/qemu-user/`. The image build verifies those inputs before using them and copies their provenance to `/opt/qemu-arm64/`. Keep the generated directory with an offline build context; missing or changed inputs fail clearly.

The image selects `/opt/qemu-arm64/bin/qemu-aarch64` through `DRONE_QEMU_AARCH64`. CMake target launchers, diagnostic child processes, target Python, and target loader inspection all use that explicit executable. The original distribution emulator remains installed. Native compilers and fixture tools keep using the native loader.

The Python SDK CTest runs 52 portable cases natively and the actual ARM middleware case on ARM Python. The coordinator checks that every unique ID runs once and rejects skips or either group failing. ARM ctypes subprocesses explicitly reenter the selected emulator. Both groups share the unchanged 30-second timeout. QEMU 8.2 does not implement the child-subreaper syscall; the portable process-supervision tests therefore exercise native Linux rather than claiming guest support.

## Rebuild with the official ROS base

From the tracker repository:

```bash
bash dockerfiles/build-image-amd64.sh
```

The ARM64 SDK image must already exist locally. Rebuild it with `dockerfiles/build-image.sh` if necessary. To choose another SDK image or a registry cache, use explicit arguments:

```bash
bash dockerfiles/build-image-amd64.sh \
  --sdk-image drone-tracker-rk3576:humble-arm64-rknn2.3.2 \
  --base-image mirror.gcr.io/library/ros:humble-ros-base-jammy
```

The attempted Google registry-cache route returned the same official ROS index digest as Docker Hub: `sha256:860e53793ccf575e5369b5e9a19939de2bfcfb46a299954d06f54f2af54fed19`. Its amd64 manifest was `sha256:6b4abb5340e430c632bcf4710fb34996df4f80e502993e5755f8fc59a39c7b2e`. Large layer downloads stalled, so that base was not used for the delivered image. Image tags and apt package versions can change; record the resulting image ID and installed package list for each validation run.

### Initial local fallback

On 2026-10-15 Docker Hub repeatedly timed out. Larger layers from the Google cache also stalled. The initial build therefore used `nickluo-local/ros-humble:jammy-amd64-host-packages-20261015`, a local snapshot of installed Ubuntu/ROS package files from the Ubuntu 22.04 x86_64 development host. This base is explicitly named as a local snapshot, rather than being tagged as the official ROS image.

The reconstruction script is `/tmp/nickluo-amd64-image-20261015/host-package-rootfs.py`. Its package versions are recorded in `host-package-manifest.json` in the same directory. The script selects the installed dependency closure and all Essential packages, which Ubuntu expects to exist even when dependencies do not name them. It copies only package-managed executable, library, header, ROS, and data paths. It creates fresh account and apt configuration files. It copies public repository keys and retains signature verification. It excludes host home directories, user configuration, credentials, caches, source code, and runtime state.

Missing packages were downloaded with `apt-get download` against the host's existing authenticated apt metadata. The download plan is `download-packages.txt`. The offline base recipe and generated alternative-link repairs are preserved in the same evidence directory. The exact build invocations are:

```bash
docker build --platform linux/amd64 --network none \
  -f /tmp/nickluo-amd64-image-20261015/Dockerfile.offline-base \
  -t nickluo-local/ros-humble:jammy-amd64-build-deps-20261015 \
  /tmp/nickluo-amd64-image-20261015
bash dockerfiles/build-image-amd64.sh \
  --base-image nickluo-local/ros-humble:jammy-amd64-build-deps-20261015 \
  --offline-native-deps --network none
```

`deb-checksums.json` records each archive's SHA-256 and source. There are 69 archives that match the authenticated apt index. `libflite1=2.2-3` was repacked from the host's installed package files after its downloaded archive was interrupted. Every repacked payload file matched the installed package's MD5 manifest. That archive is identified as a local repack, rather than as the original downloaded package.

The snapshot is a local recovery route. Prefer the official ROS base for a fresh build when registry access is reliable. Keep the local reconstruction script and package manifest with the validation evidence when preserving the initial image.

### Final image verification

The final image, independently checked after the emulator repair on 2026-10-15, is:

```text
drone-tracker-rk3576:humble-amd64-rknn2.3.2
sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421
linux/amd64
```

Its native tools report GCC 11.4.0, CMake 3.22.1, Python 3.10.12, and OpenCV 4.5.4. Native `rclpy`, NumPy, YAML, serial, MAVROS message, and vision message imports passed. A real C++ program using `rclcpp` and OpenCV compiled, linked, and ran successfully as an x86_64 executable. The target RKNN library retained SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`.

The final ARM64 smoke compiled and linked the same real C++ ROS/OpenCV program with the cross compiler. Its output was verified as an ARM aarch64 executable and ran successfully under QEMU. The toolchain selected ARM64 Python 3.10 headers and libraries. Its `PYTHON_SOABI` value was `cpython-310-aarch64-linux-gnu`; this tag gives generated Python extensions the filename expected by ARM64 Python.

The image contains three target-path repairs proven during project validation. A compatibility link exposes the genuine target `libpython3.10.so` at the architecture-specific path exported by ROS. The target BLAS and LAPACK links point to the exact implementations selected by the original SDK's alternatives. All three refer to real ARM64 libraries; native x86_64 loader directories are unchanged. The toolchain also corrects the target Python extension tag while retaining a native Python executable for code generation.

The image before the emulator repair is preserved as `drone-tracker-rk3576:humble-amd64-rknn2.3.2-before-qemu-repair-20261015`, ID `sha256:156c071fcff2385947d0adaea176b29be1b3fb110d2969ebddfda39f2d9d99fd`. It passed native/cross smoke checks but its QEMU 6.2 could not run strict process-descriptor tests. The final image adds the pinned, corrected emulator and retains the genuine target library repairs.

The earlier image remains available as `drone-tracker-rk3576:humble-amd64-rknn2.3.2-initial-20261015`, ID `sha256:7b63829372e2cf10af24f619d619b01b858e7458538c352905a0a4f7d5c1c520`. Its native smoke passed, but its cross link initially failed on the ROS Python library path. The final image incorporates the subsequent fixes; it does not rely on temporary sysroot repair commands.

Build logs and package provenance are under `/tmp/nickluo-amd64-image-20261015/`. The earlier sysroot-repair build and smoke logs are `final-repair/build.log` and `final-repair/smoke.log`. The current emulator-repair build, source preparation, native/cross smoke logs, and image metadata are under `/tmp/nickluo-build-workflow-20261015/qemu-image/`. Its generated emulator binary SHA-256 is `da420e56e1d0199413c081067d647936af23123b9eefdb859b1facb92cccaddc`. Independent tests on this exact image passed seven Unix descriptor-buffer capacities and three strict ARM diagnostic runs. Deliberately leaking a transferred descriptor or retaining a worker descriptor failed as required. These controls are recorded under `/tmp/nickluo-native-default-investigation-20261015/root/final-image-logs/`. The historical `final-smoke.log` records the initial cross failure and remains preserved as such. Small scripts, manifests, logs, and before/after recipe hashes are also preserved in the ignored tracker artifact directory `.docker-artifacts/amd64-image-20261015/provenance/`.

The larger root filesystem and downloaded package archives remain only in `/tmp/nickluo-amd64-image-20261015/`; they were not duplicated into the source tree. `/tmp` can be cleaned by the operating system. Preserve those archives separately before clearing `/tmp` if an offline reconstruction must remain possible. The installed Docker image remains available independently of that temporary directory.

## Routine tracker build and tests

The final ordinary `./build.sh --test` run passed 57/57 CTests in 156.28 seconds, plus the mandatory native OS memory-limit check in 5.67 seconds. The Python CTest passed all 53 unique IDs without skips. The ARM target source test passed in 71.26 seconds within its original 90-second deadline. The final log is `/tmp/nickluo-build-workflow-20261015/final-image/default-build-test.log`; the scoped OpenSpec change retains a detailed verification report.

From the tracker repository, `./build.sh` defaults to this x86_64 image and generates ARM64 deployment binaries. It checks the installed executable's ELF machine type before reporting success. Use `./build.sh --test` for the full existing CTest suite. The established helper `bash tests/tracking_regression/run_sdk_tests.sh` delegates to the same build policy; `--regression-only` explicitly selects its regression subset.

```bash
./build.sh --test --artifacts-dir /tmp/tracker-build
./build.sh --backend arm64 --test --artifacts-dir /tmp/tracker-build
```

The second command explicitly selects the preserved ARM64 compiler image. There is no automatic backend switch after failure. `--image IMAGE` accepts a custom image only when its architecture matches the selected backend. `--jobs N` controls compilation parallelism.

Artifacts are separated by backend: `/tmp/tracker-build/backends/amd64/{build,install,log}` and the corresponding `arm64` directories. Without `--artifacts-dir`, the root is the repository's `.docker-artifacts` directory. Existing legacy top-level caches remain untouched. `--clean` preserves only the selected backend's previous directories under `ROOT/backups/BACKEND/`. A cache lock blocks concurrent mutation and prints an actionable lock path if a previous process left a stale lock.

Routine containers are offline. Source and external Git metadata are mounted read-only at their original paths. Image-provided native FFmpeg tools are validated before test compilation. The explicit ARM64 fallback needs private fixture tools when its image lacks them:

```bash
bash tests/tracking_regression/prepare_test_tools.sh --backend arm64
```

Production packaging still runs its dependency collector in the preserved ARM64 image; it has not been migrated to a native target-loader implementation. This remains a real image dependency, separate from cross-compilation. It also keeps its historical default input. Select the new install explicitly so it cannot package a stale legacy build accidentally:

```bash
bash deployment/package.sh .docker-artifacts/packages \
  --install-root /tmp/tracker-build/backends/amd64/install/drone_tracker \
  --model-root /absolute/model-directory
```

A cache manifest records the immutable image ID, ARM64 output target, and toolchain checksum. Changing the image or toolchain requires `--clean`. That option backs up the selected backend's old build, install, logs, and manifest before fresh configuration. An unverified populated cache also requires this backup step.

## Native portable checks

Mount the source read-only and put generated files in a separate directory. For example, this builds and tests the tracker OSD drawing logic without accessing HDMI hardware:

```bash
mkdir -p /tmp/nickluo-amd64-check
docker run --rm --platform linux/amd64 --network none \
  -v "$PWD:/src:ro" -v /tmp/nickluo-amd64-check:/out \
  drone-tracker-rk3576:humble-amd64-rknn2.3.2 bash -lc '
    cmake -S /src/src/osd -B /out/tracker-osd
    cmake --build /out/tracker-osd -j 8
    ctest --test-dir /out/tracker-osd --output-on-failure
  '
```

The other standalone tracker entry points are `src/hybrid_tracker` with `DRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON` and `src/platform/rk3576` with `DRONE_TRACKER_CAMERA_ONLY=ON`.

## ARM64 target builds

Use `/opt/toolchains/aarch64-humble.cmake` as `CMAKE_TOOLCHAIN_FILE`. A toolchain is a CMake configuration that selects the ARM64 compiler, target headers, and target libraries. Do not set `CMAKE_SYSTEM_PROCESSOR` alone: that would bypass an architecture check without changing the compiler.

Use separate build and install directories for native and ARM64 output. Build `quadrotor_msgs` for the same target before its consumers. Clean ARM64 cross-builds of both `apm_bridge` executables and the enabled `drone_interceptor` node were additionally verified on 2026-10-15, including real target-loader dependency resolution. Their exact native/cross recipes are in `../apm_bridge/docs/build-and-test.md` and `../drone_interceptor/docs/DOCKER_BUILD.md` relative to the tracker repository root. Load the native ROS environment so Python code generators run as native processes, then pass the target toolchain to CMake. A cross-build configuration can require additional package-specific paths; retain the configure and linker output as evidence of the selected libraries.

The toolchain sets `QEMU_LD_PREFIX` during CMake configuration so ARM subprocesses can find the target loader. When running target programs from a separate shell, set that variable explicitly. Pass target-only library paths through `QEMU_SET_ENV` so native build tools keep their normal library environment. For example:

```bash
export QEMU_LD_PREFIX=/opt/arm64-sysroot
export QEMU_SET_ENV=LD_LIBRARY_PATH=/opt/arm64-sysroot/opt/ros/humble/lib:/opt/arm64-sysroot/opt/rknn-runtime/lib:/opt/arm64-sysroot/usr/lib/aarch64-linux-gnu
/opt/qemu-arm64/bin/qemu-aarch64 -L "$QEMU_LD_PREFIX" /out/arm64/program
```

Include the target `quadrotor_msgs` install library directory in `QEMU_SET_ENV` when the program uses the newly generated shared messages. Do not load its ARM Python extensions into the native x86_64 interpreter.

ARM64 executables require either an ARM64 machine or QEMU, which emulates ARM CPU instructions. Select the corrected `/opt/qemu-arm64/bin/qemu-aarch64` explicitly for target tests. The image also contains the historical distribution emulator; transparent subprocess execution uses the host ARM64 `binfmt` registration and is not equivalent to explicitly selecting the repaired emulator. Emulation and portable tests do not verify NPU inference, physical cameras, HDMI output, timing, or flight behavior. Preserve the existing ARM64 production build and real-device acceptance workflow.
