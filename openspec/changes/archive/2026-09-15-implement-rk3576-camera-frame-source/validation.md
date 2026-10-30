# RK3576 independent camera-source validation

## Scope and conclusion

Change: `implement-rk3576-camera-frame-source`. Implementation date: 2026-09-15. This archived report describes that stage; later ROS camera integration does not change the historical scope below.

Final progress was **29/29**: the first implementation completed 25 tasks, then the user authorized development-board-2 acceptance and tasks 6.3–6.6 completed. See [board-2 acceptance](board2-validation.md).

The change implemented independent AR0234 continuous grayscale capture, an explicit factory capability option, device-failure injection, timestamp mapping, and an independent reporting tool. The production ROS factory still rejected cameras at this stage. Output frames owned their host memory, so retaining them did not retain driver buffers.

The user initially deferred board-2 execution; no board connection or program was run in that phase. After explicit authorization, board-2 camera topology was rechecked and independent capture ran. Final device results are in [board follow-up](board2-validation.md). Container sections preserve the first implementation's evidence.

The environment was Linux ARM64 in existing local image `roc-rk3576-ros2-build:humble-arm64`. Every container used `--rm`; the image was neither modified nor rebuilt. Container checks used no real models, recordings, or camera input: inputs were fixed synthetic grayscale pixels, generated MJPEG, and mock model interfaces. MJPEG independently JPEG-compresses each frame. Source flow was compared with local `ar0234_osd_hdmi/hw/camera.cpp`; the reference repository was unchanged.

## Implementation and specification mapping

| Requirement | Implementation and verification |
|---|---|
| Explicit configuration and unsupported-option rejection | Three fields appended to public configuration; `validateCameraConfig()` checks before device access and preserves aggregate-initialization order |
| Read back sensor settings | Argument-array `media-ctl` execution, then subdevice pad-0 format/frame-interval reads. Different dimensions, rate, or format are rejected |
| No capture during creation | Factory creation configures/checks/closes devices. Tests verify no buffer requests or streaming |
| Buffer lifecycle | MMAP maps driver buffers into process memory. Queue all buffers before streaming, return them after copying, and unwind partial failures in reverse order |
| Failed stream stop | Preserve the error, close capture device before unmapping, and verify call order |
| Valid pixels and independent lifetime | Verify every pixel for 1920×1200 input with stride=2048 and nonzero offset. Stride is the distance between row starts. Retained views preserve pixels after source-buffer reuse and release |
| Metadata bounds | Cover index, plane count, offset, bytesused, mapping length, error flags, and truncated frames. Reject before pixel access |
| Time strategy | Bracket monotonic samples for a fixed offset. Cover driver-monotonic time, unknown/COPY fallback, calendar adjustment, repeats, rollback, strategy changes, invalid times, and overflow |
| Queue and stop | Preserve capacity argument 30 and non-expanding enqueue. Drop new frames when full. Cover concurrent stop with one consumer, retained old frames, queue clearing on restart, and a 250 ms stop limit |
| Restart after faults | Restart after stream-start, dequeue, return, stream-stop, and thread-start failures. Reject an empty returned thread |
| Recording rejection | Enabling recording throws visibly; disabling it is safely accepted |
| Factory boundary | Default factory rejects camera. Explicit capability with replacement device returns Ready. Complete-assembly tests directly verify `CreatePlatformFactory()` still rejects camera; video/model checks continue passing |
| Independent tool | Create/consume through public factory. Two instances with two rounds each generate configuration, frame/resource/round reports, and pixel-correct lossless PGM snapshots. Existing destinations are not overwritten |
| Output errors | Check report writes and close state. A callback using `/dev/full` injects disk-write failure and retains its reason |

`CameraSystem` replaces system calls. The substitute exercises the real `CameraDevice` resource flow while replacing open, ioctl, mapping, poll, clock, and configuration-tool behavior. It does not bypass the device layer with a mock frame source. Initialization checks separately inject tool execution, sensor query, capabilities, format, event handle, buffer request, third mapping, enqueue, and stream-start failures; each checks acquired-resource release.

Real Linux `eventfd` and `poll` additionally verify wakeup within 250 ms without camera access. The configuration-tool runner uses ordinary commands to check argument boundaries, stderr capture, missing tools, nonzero exit, and bounded timeout.

## Build and test evidence

Historical build/log outputs were under ignored `.docker-artifacts/`. It is disposable and may be cleared; these paths do not guarantee permanent evidence retention.

| Layer | Result from this run | Log |
|---|---|---|
| Standalone camera build | Linux ARM64 Release, successful | `camera-configure.log`, `camera-build.log` |
| Camera/public no-device checks | 14/14 passed | `camera-tests.log` |
| Complete standalone RK3576 build | Successful, including existing video/model components | `camera-regression-configure.log`, `camera-regression-build.log` |
| Complete standalone regression | 33/33 passed, covering camera, video, public algorithm, model contracts, and replay | `camera-regression-tests.log` |
| Root ROS ARM64 build | `colcon build` succeeded, 1 package completed | `camera-ros-build.log` |
| Root ROS checks | All final 12 passed. Initial 10/12 had two package-discovery failures; rerunning after loading the installed workspace passed. Fixture setup also ran automatically, giving 3/3 for that rerun | `camera-ros-tests.log`, `camera-ros-retest.log` |
| Strict OpenSpec validation | `openspec validate implement-rk3576-camera-frame-source --strict` passed | CLI output |

The complete standalone entry used the existing RKNN build SDK but ran only substitutes, without NPU inference. The NPU is the board's neural-network accelerator. These builds do not establish hardware behavior on Runtime 2.3.2/driver 0.9.8.

### Dependencies and static analysis

Standalone camera configuration explicitly used `RKNN_INCLUDE_DIR=/nonexistent` and `RKNN_LIBRARY=/nonexistent`, yet configured and built. It did not include `dependencies.cmake` or discover RKNN, ROS, RGA, DRM, or Jetson SDKs.

`ldd` showed OpenCV, JPEG, FFmpeg, and system libraries, without RKNN, CUDA, TensorRT, VPI, or Tegra. System FFmpeg indirectly loaded `libva-drm` and `libdrm`; this is not a camera-code DRM development dependency. The tool neither invokes display operations nor opens DRM devices. Do not describe the dynamic dependencies as completely DRM-free. See `camera-tools-and-linkage.log` for the full list.

The image lacked `clang-tidy` and `clangd`, and the repository had no configured commands for them, so neither ran. Standalone camera code compiled with `-Wall -Wextra`; new translation units had no warnings. Three logged warnings came from existing `src/hybrid_tracker/tests/test_main.cpp`, involving a multiline comment and two unused variables. That file was unchanged. Compiler warnings do not replace those static analyzers.

### Historical reproduction commands

The following temporary-container mounts followed `AGENTS.local.md` at the time. Outputs survived container removal but remain disposable host artifacts:

```bash
mkdir -p .docker-artifacts/{build,install,log}
docker run --rm \
  --mount type=bind,src="${PWD}",dst=/work/ros2_ws/src/drone_tracker \
  --mount type=bind,src="${PWD}/.docker-artifacts/build",dst=/work/ros2_ws/build \
  --mount type=bind,src="${PWD}/.docker-artifacts/install",dst=/work/ros2_ws/install \
  --mount type=bind,src="${PWD}/.docker-artifacts/log",dst=/work/ros2_ws/log \
  --workdir /work/ros2_ws roc-rk3576-ros2-build:humble-arm64 bash -lc '
    cmake -S src/drone_tracker/src/platform/rk3576 -B build/camera-only \
      -DDRONE_TRACKER_CAMERA_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
      -DRKNN_INCLUDE_DIR=/nonexistent -DRKNN_LIBRARY=/nonexistent \
      -DCMAKE_CXX_FLAGS="-Wall -Wextra" &&
    cmake --build build/camera-only -j4 &&
    ctest --test-dir build/camera-only --output-on-failure
  '
```

Complete standalone regression used the same mounts:

```bash
cmake -S src/drone_tracker/src/platform/rk3576 -B build/camera-regression \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-regression -j4
ctest --test-dir build/camera-regression --output-on-failure
```

The root ROS build installed its agreed dependencies only in a new temporary container; those additions disappeared on container exit:

```bash
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  ros-humble-vision-msgs ros-humble-mavros-msgs ros-humble-cv-bridge
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-clean-cache --cmake-args \
  -DDRONE_TRACKER_PLATFORM=rk3576 -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
LD_LIBRARY_PATH=/opt/rknn-runtime/lib:$LD_LIBRARY_PATH \
  ctest --test-dir build/drone_tracker --output-on-failure
```

The first run omitted `source install/setup.bash`, causing package-discovery failure. Only failed tests were rerun after loading that environment with `ctest --test-dir build/drone_tracker --rerun-failed --output-on-failure`. No source or board environment changed.

A later small tool change added per-second resource sampling and failure response during slow-consumer waits. The tool rebuilt and passed three tool checks, recorded in `camera-driver-final-build.log` and `camera-driver-final-tests.log`. This did not expand prior device, video, or ROS coverage.

### Historical artifact locations

- `.docker-artifacts/build/camera-only/camera_source_driver`: Linux ARM64 tool without RKNN SDK dependency.
- `.docker-artifacts/build/camera-only/camera-driver-fixtures/normal/`: four generated report/snapshot rounds, not real camera images.
- `.docker-artifacts/build/camera-regression/`: complete standalone build and video fixtures.
- `.docker-artifacts/install/drone_tracker/`: root ROS output.
- `.docker-artifacts/log/camera-*.log`: build, test, and dependency logs above.

## Hardware gaps at first implementation

These were the original gaps. [Board follow-up](board2-validation.md) records their subsequent completion; containers and older board records did not replace them:

1. **6.3:** recheck board-2 topology, format, driver, and device occupation after connecting the camera.
2. **6.4:** compare valid same-frame pixels in short real capture; record snapshots, time source, and offset-sampling span. Exposure synchronization remained unverified.
3. **6.5:** at least 20 restarts and 100 create/destroy cycles, each stopping within 1 second without accumulating threads or descriptors.
4. **6.6:** at least 10 minutes of capture and slow-consumer load, explaining resource changes and separating driver-sequence gaps from full-queue drops.

Device recovery and operating commands are in the [platform guide](../../../../src/platform/README.md). Camera configuration changes actual sensor format; stopping does not claim restoration of another application's prior settings.

This change did not modify earlier complete-replay quality tasks 7.2/7.3 or claim ROS-camera integration, Jetson hardware equivalence, or deployment performance. Follow-up completed the initial hardware gaps. At the report's close, archival conditions were satisfied but archival had not yet run.
