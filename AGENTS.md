# drone_tracker repository instructions

## Branches and compatibility

- Develop on `rk3576` unless the task explicitly selects another branch. Use `jetson` as the reference implementation; compare with `git show jetson:<path>` or a branch diff when investigating behavior differences.
- Preserve externally visible behavior and algorithm semantics unless an approved OpenSpec change authorizes a difference. Read [RK3576_REFACTORING_PLAN.md](docs/architecture/RK3576_REFACTORING_PLAN.md) before changing platform boundaries. Proposed designs and unverified hardware assumptions are not acceptance evidence.
- Keep hardware-specific dependencies inside their platform boundaries. CUDA, TensorRT, VPI, Jetson Multimedia API, Tegra libraries, and CUDA architecture 87 belong to the Jetson build. The RK3576 build selects RKNN and its own camera/display/recording dependencies.

## Architecture and terminology

- `src/drone_tracker_node.*` orchestrates ROS 2 parameters, inputs, tracking rounds, observations, and recording. `src/drone_tracker_main.cpp` owns process startup and the ROS executor.
- `src/hybrid_tracker/` contains asynchronous YOLO detection, NanoTrackV3 tracking, optional DCF center fusion, and a rotation-compensated Kalman filter. DCF is the grayscale correlation filter; disabling it retains the separate center Kalman filter. [ALGORITHM_REVIEW.md](docs/architecture/ALGORITHM_REVIEW.md) explains the algorithm; code and approved specifications remain authoritative.
- `src/frame/` and the public interfaces under `src/platform/` describe shared read-only frames and complete tracker components. Keep hardware SDK types out of these interfaces and the algorithm state machine.
- Jetson capture and decoding use `src/mv_camera/`; Jetson adapters live under `src/platform/jetson/`. RK3576 capture, software video decoding, model sessions, display, and recording live under `src/platform/rk3576/`.
- The three logical model roles are YOLO, NanoTrackV3 Backbone, and NanoTrackV3 Head. Jetson uses three TensorRT `.engine` files. RK3576 uses four `.rknn` files because template and search Backbone inputs require separate files.
- `launch/` and `parameters/` define runtime entry points. Absolute model paths are used directly; relative model paths resolve under the installed package's `share/drone_tracker/models/` directory. Do not commit large model binaries without an explicit request.

## RK3576 model and resource constraints

- Keep RKNN Runtime 2.3.2 and RKNPU driver 0.9.8 as the compatibility baseline. Use existing models; retraining or conversion is not a default prerequisite.
- Verify the complete model contract when replacing inference code: input dimensions, grayscale/color format, tensor layout, type, quantization, normalization, letterbox/crop rules, output ordering, YOLO decoding, and NMS. NMS removes overlapping duplicate detections. Successful model loading alone does not establish equivalent results.
- The production NanoTrack search feature passes through shared device memory. Check native FP16 layout, stride, capacity, and binding compatibility; incompatible models fail loading rather than silently falling back to a host transfer. Template features and final Head outputs retain their Float32 host path.
- Keep the platform-independent `HybridTracker` state machine and detection/tracking arbitration stable. Introduce platform work through frame, preprocessing, and model boundaries.
- For asynchronous detection, queues, read views, or device buffers, make ownership and thread boundaries explicit. Run the relevant concurrency, stress, or replay checks. Release buffers only after their device and thread users have stopped.

## OpenSpec and documentation

- Before a feature, architecture, interface, or observable behavior change on `rk3576`, inspect `openspec/changes/` and continue a matching change. If a new change is needed, complete its proposal, design, specifications, and tasks before implementation.
- Implement against the change's `tasks.md`. Verify code against all planning artifacts before synchronization or archival. Keep specifications consistent with the final implementation.
- Follow `openspec/config.yaml`: write new or revised artifacts in English. Make each artifact understandable without the chat, explain technical terms on first meaningful use, and preserve exact identifiers and normative keywords.
- Treat `.docker-artifacts/` and temporary directories as disposable output. They may be cleared regularly. Copy evidence needed for durable acceptance to an explicitly retained location before cleanup; a historical path does not guarantee that its files still exist.

## Build and verification

- This is a C++17 ROS 2 `ament_cmake` package. Preserve neighboring code style and avoid unrelated formatting. Read applicable language norms under `~/.codex/language_norms/` before editing that language.
- Before development or builds, read repository and parent `AGENTS.local.md` files when present. Put machine-specific paths and access details in the local file.
- Use `./build.sh` or `./build.sh --test` for RK3576. The default x86_64 compiler image produces ARM64 output. `--backend arm64` explicitly selects the ARM64-image fallback; failures must not trigger an automatic backend switch. See [dockerfiles/README.amd64.md](dockerfiles/README.amd64.md) for SDK paths, separate caches, and packaging boundaries.
- Build the Jetson reference on matching Jetson hardware and SDKs. Its entry point is `colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release`; it requires ROS 2, OpenCV, CUDA, TensorRT, VPI, FFmpeg, and Jetson Multimedia API.
- Manual configurations enable checks with `-DDRONE_TRACKER_BUILD_TESTS=ON`. Jetson-only race/stress/replay targets require the corresponding SDKs, and model/replay checks require actual models and recordings.
- Report the actual platform, model identities, and input data used for validation. State checks not run because hardware, SDKs, models, or recordings were unavailable. Host or emulated results do not replace target-board acceptance.

## Production boards

- Production acceptance targets `HZ-RK3576_WuKong Board`, Ubuntu 22.04.5 LTS ARM64, kernel `6.1.141-rt36`, and an RK3576 with four Cortex-A72 and four Cortex-A53 cores. Record board identity, available memory, storage, and device topology for each run; memory capacity is not universal across boards.
- Use the board's system `/usr/lib/librknnrt.so`; `/lib/librknnrt.so` refers to the same file on the documented system. Runtime 2.3.2 has SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`. Never overwrite it with a container library.
- Recheck NPU/render devices and camera topology on the actual board. Documented render nodes are `/dev/dri/renderD128` and `/dev/dri/renderD129`. Do not run `rknn_server` merely to query a version: it starts a foreground service.
- Source `/opt/ros/humble/setup.bash` in each non-interactive board command. The documented toolchain is GCC/G++ 11.4.0, CMake 3.22.1, and Python 3.10.12.
- Before capture, run `v4l2-ctl --list-devices` and `media-ctl -p`; verify the sensor, node, format, and access permissions. Development board 1/2 results do not substitute for production-board acceptance.

## ROS and review constraints

- Preserve existing `/fpv/gimbal`, `/fpv/odom`, `/fpv/tracker_trigger`, `/fpv/target_bbox`, `/fpv/tracker_image`, and `/fpv/tracker_state` contracts except for approved changes. RK3576 also publishes paired floating bbox/LOS observations through `/fpv/target_los` and records `/mavros/imu/data`; follow `target-observations` for timestamps, validity, QoS, and bag contents.
- Preserve `TrackerState`: `Detecting=0`, `TargetFound=1`, `Tracking=2`, `TargetLost=3`. Preview is a separate lifecycle and must not invent tracking states. Update all affected parameter templates and launch overrides when defaults or units change.
- For RK3576 camera input with `osd.enabled=true`, service startup begins preview and OFF leaves capture running. Models remain shared across tracking rounds; final service shutdown owns device release. Other input modes retain their specified lifecycle.
- Treat `readerwriterqueue.h`, `threadpool.h`, and other embedded dependencies as external code unless directly in scope. Review cross-platform dependency isolation, model compatibility, failure cleanup, and producer/consumer assumptions. The repository has no CI workflow configuration.
