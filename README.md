# drone_tracker

`drone_tracker` is a C++17 ROS 2 package for detecting and tracking a target in camera or video frames. It is intended for robotics developers integrating visual tracking with gimbal and vehicle pose data. YOLO finds the configured target class. NanoTrackV3 follows it between detections. Optional center smoothing and a Kalman filter stabilize and predict the output while accounting for camera rotation.

The `rk3576` branch supports RK3576 camera capture, local MJPEG video, HDMI display, recording, and diagnostics. MJPEG stores each frame as an independent JPEG image. The `jetson` branch is the reference implementation for NVIDIA Jetson. Hardware dependencies stay inside their respective platform implementations.

Start with the [documentation index](docs/README.md) for architecture, operating guides, testing, and dated reports.

## Setup and build

For the supported RK3576 build workflow, prepare:

- A Linux x86_64 host with Docker and access to this checkout.
- The image `drone-tracker-rk3576:humble-amd64-rknn2.3.2`. It supplies ROS 2 Humble, the C++ toolchain, OpenCV, Eigen, FFmpeg, and ARM64 target libraries, including RKNN Runtime 2.3.2.
- The image `drone-tracker-rk3576:humble-arm64-rknn2.3.2` as the target SDK source when building the x86_64 image, selecting the ARM64 compiler workflow, or packaging production dependencies.

Use the [image and build guide](dockerfiles/README.amd64.md) to provision or rebuild those images. Routine builds run offline and require the selected image to exist locally.

Run from the repository root:

```bash
./build.sh
./build.sh --test
```

The default x86_64 compiler produces **ARM64 deployment binaries**. Build, installation, and log directories are under `.docker-artifacts/backends/amd64/`. The package installation is `.docker-artifacts/backends/amd64/install/drone_tracker/`. These programs require an ARM64 target; they do not run directly on the x86_64 build host.

To deliberately select the ARM64 compiler image, use `./build.sh --backend arm64`. Its tests may first require `bash tests/tracking_regression/prepare_test_tools.sh --backend arm64`. See the build guide for separate caches, image identity changes, and external artifact directories.

The Jetson reference requires matching Jetson hardware and ROS 2, OpenCV, CUDA, TensorRT, VPI, FFmpeg, and Jetson Multimedia API dependencies. Its build entry point is:

```bash
colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release
```

## Configure and run on RK3576

The target needs ROS 2 Humble, RKNN Runtime 2.3.2, RKNPU driver 0.9.8, and the required camera/display drivers for the chosen input. Production acceptance uses an identified `HZ-RK3576_WuKong Board`. Follow the [deployment guide](deployment/README.md) for packaging, board preparation, installation, and service operation. Keep the board's system RKNN Runtime intact.

For a minimal local-video example:

1. Copy [rk3576_video_params.yaml](parameters/rk3576_video_params.yaml) to your own configuration file.
2. Fill `camera.fx`, `camera.fy`, `camera.cx`, and `camera.cy` with calibration values in input-image pixels. The supplied `.nan` values intentionally reject an unfinished configuration.
3. Supply the four compatible `.rknn` model paths: `tracker.yolo_engine`, `tracker.nano_template_backbone`, `tracker.nano_search_backbone`, and `tracker.nano_head`. Leave `tracker.nano_backbone` empty. The Backbone extracts image features; the Head compares template and search features. Set `tracker.yolo_target_class_id` to the model's exact zero-based target class.
4. On the RK3576 board, source ROS and the package installation, then launch with your configuration and MJPEG file:

   ```bash
   source /opt/ros/humble/setup.bash
   source /absolute/install/drone_tracker/share/drone_tracker/local_setup.bash
   ros2 launch drone_tracker rk3576_video.launch.py \
     params_file:=/absolute/video.yaml video_source:=/absolute/input.mov
   ```

Absolute model paths are used directly. Relative model paths resolve under the installed package's `share/drone_tracker/models/` directory. Compatible models must be supplied separately; model loading alone does not establish tracking quality.

In another terminal with the same ROS and package environment, start and stop a tracking round:

```bash
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header '{frame_id: ON}'
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header '{frame_id: OFF}'
```

Video input waits for ON. Each new round starts at the beginning of the file; end of file finishes the round without looping. For camera input, choose the appropriate [parameter template](parameters/README.md) and follow the [camera guide](src/platform/README.md#camera-sources-and-configuration). With camera input and `osd.enabled=true`, startup begins preview and OFF leaves capture running.

The ROS interfaces include `/fpv/gimbal`, `/fpv/odom`, `/fpv/tracker_trigger`, `/fpv/target_bbox`, `/fpv/tracker_image`, and `/fpv/tracker_state`. RK3576 also publishes `/fpv/target_los`, the camera-to-target direction and angular rates. Read the [observation contract](src/platform/README.md#rk3576-paired-target-observations) for timestamps, validity, message fields, and delivery rules.

## Repository navigation

| Location | Purpose |
|---|---|
| [docs/](docs/README.md) | Architecture, operating guides, test protocols, and dated reports |
| [src/hybrid_tracker/](src/hybrid_tracker/README.md) | Shared detection and tracking algorithm |
| [src/platform/](src/platform/README.md) | Platform interfaces and RK3576 input/model integration |
| [src/diagnostics/](src/diagnostics/README.md) | Tracking event collection and inspection |
| [launch/](launch/) and [parameters/](parameters/README.md) | ROS entry points and configuration templates |
| [models/](models/) | Model assets copied into the package installation |
| [dockerfiles/](dockerfiles/README.amd64.md) | Compiler images, target SDK, and build setup |
| [deployment/](deployment/README.md) | Production packaging, installation, and rollback |
| [tests/tracking_regression/](tests/tracking_regression/README.md) | Configurable recording-based regression comparisons |
| [tests/nanotracev3_performance_benchmarks/](tests/nanotracev3_performance_benchmarks/README.md) | Standalone NanoTrackV3 performance tools |
| [openspec/](openspec/) | Approved specifications, active changes, and archived evidence |

Read [AGENTS.md](AGENTS.md) before development. Machine-specific setup belongs in `AGENTS.local.md`. Feature and behavior changes follow the repository's OpenSpec workflow, which records proposals, design decisions, specifications, and implementation tasks.

Host and emulated checks do not establish NPU, camera, HDMI, timing, or flight acceptance. Dated reports apply to their recorded board, binary, models, and input. Build caches and `/tmp` are disposable; retain required release and acceptance evidence separately.
