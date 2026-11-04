# drone_tracker repository instructions

## Branches and compatibility

- Develop on `rk3576` unless the task explicitly selects another branch. Use `jetson` as the reference implementation; compare with `git show jetson:<path>` or a branch diff when investigating behavior differences.
- Preserve externally visible behavior and algorithm semantics unless an approved OpenSpec change authorizes a difference. Read [RK3576_REFACTORING_PLAN.md](docs/architecture/RK3576_REFACTORING_PLAN.md) before changing platform boundaries. Proposed designs and unverified hardware assumptions are not acceptance evidence.
- Keep hardware-specific dependencies inside their platform boundaries. CUDA, TensorRT, VPI, Jetson Multimedia API, Tegra libraries, and CUDA architecture settings belong to the Jetson build. RK3576 selects RKNN and its own camera/display/recording dependencies.

## Architecture and terminology

- `src/drone_tracker_node.*` orchestrates ROS 2 parameters, inputs, tracking rounds, observations, and recording. `src/drone_tracker_main.cpp` owns process startup and the ROS executor.
- `src/hybrid_tracker/` contains asynchronous YOLO detection, NanoTrackV3 tracking, optional DCF center fusion, and a rotation-compensated Kalman filter. DCF is the grayscale correlation filter; disabling it retains the separate center Kalman filter. [ALGORITHM_REVIEW.md](docs/architecture/ALGORITHM_REVIEW.md) explains the algorithm; code and approved specifications remain authoritative.
- `src/frame/` and the public interfaces under `src/platform/` describe shared read-only frames and complete tracker components. Keep hardware SDK types out of these interfaces and the algorithm state machine.
- Jetson capture and decoding use `src/mv_camera/`; Jetson adapters live under `src/platform/jetson/`. RK3576 capture, software video decoding, model sessions, display, and recording live under `src/platform/rk3576/`.
- The three logical model roles are YOLO detection, NanoTrackV3 Backbone feature extraction, and NanoTrackV3 Head matching. Jetson uses three TensorRT `.engine` files. RK3576 uses four `.rknn` files because template and search Backbone inputs require separate files.
- Use `launch/` and [parameters/README.md](parameters/README.md) for runtime entry points and configuration. Absolute model paths are used directly; relative paths resolve under the installed package's `share/drone_tracker/models/` directory. Do not commit large model binaries without an explicit request.

## RK3576 model and resource constraints

- Keep RKNN Runtime 2.3.2, the inference library, and RKNPU driver 0.9.8 as the compatibility baseline. Use existing models; retraining or conversion is not a default prerequisite.
- Verify the complete model contract when replacing inference code. Check dimensions, grayscale/color format, tensor memory layout, numeric type, and quantization (numeric scale and zero-point). Check normalization (pixel scaling), letterbox (resizing with padding) and crop rules, output ordering, YOLO decoding, and NMS. NMS removes overlapping duplicate detections. Successful model loading alone does not establish equivalent results.
- Preserve production NanoTrack search-feature transfer through device memory shared by the search Backbone and Head. Check native FP16 (16-bit floating point) layout, stride (storage spacing), capacity, and memory bindings. Incompatible models must fail loading without silently falling back to a host transfer. Template features and final Head outputs retain their Float32 host path. Follow the [NanoTrack contract](openspec/specs/rk3576-nanotrack-backend/spec.md).
- Keep the platform-independent `HybridTracker` state machine and detection/tracking arbitration stable. Introduce platform work through frame, preprocessing, and model boundaries.
- For asynchronous detection, queues, read views, or device buffers, make ownership and thread boundaries explicit. Run the relevant concurrency, stress, or replay checks. Release buffers only after their device and thread users have stopped.

## OpenSpec and documentation

- Before a feature, architecture, interface, or observable behavior change on `rk3576`, inspect `openspec/changes/` and continue a matching change. If a new change is needed, complete its proposal, design, specifications, and tasks before implementation.
- Implement against the change's `tasks.md`. Verify code against all planning artifacts before synchronization or archival. Keep specifications consistent with the final implementation.
- Follow [openspec/config.yaml](openspec/config.yaml): write new or revised artifacts in English. Make each artifact understandable without the chat, explain technical terms on first meaningful use, and preserve exact identifiers and normative keywords.
- Keep these instruction files focused on lasting constraints. Maintain operational details in their owning guides and scripts. Store environment snapshots and validation results in dated reports; they establish only the revision and conditions recorded.

## Build and verification

- This is a C++17 ROS 2 `ament_cmake` package. Preserve neighboring code style and avoid unrelated formatting. Read applicable norms under `~/.codex/development_norms/`, including `cpp-norms.md` before C++ edits and `documentation-norms.md` within its stated scope.
- Before development or builds, read repository and parent `AGENTS.local.md` files when present. Put machine-specific paths and access details in the local file.
- Use `./build.sh` or `./build.sh --test` for RK3576. The default x86_64 compiler image produces ARM64 output. `--backend arm64` explicitly selects the ARM64-image fallback; failures must not trigger an automatic backend switch. See [dockerfiles/README.amd64.md](dockerfiles/README.amd64.md) for SDK paths, separate caches, and packaging boundaries.
- Build the Jetson reference on matching Jetson hardware and SDKs. Follow the [README build instructions](README.md#setup-and-build) for its entry point and required dependencies.
- Manual configurations enable checks with `-DDRONE_TRACKER_BUILD_TESTS=ON`. Jetson-only race/stress/replay targets require the corresponding SDKs, and model/replay checks require actual models and recordings.
- Report the source revision, actual platform, model identities, input data, commands, and exit statuses used for validation. State checks not run because hardware, SDKs, models, or recordings were unavailable. Host or emulated results do not replace target-board acceptance.
- Treat `.docker-artifacts/`, cache backups, and temporary directories as disposable. Copy required acceptance evidence to an explicitly retained location before cleanup. A documented path or backup does not guarantee permanent retention.

## Production boards

- Use an identified production `HZ-RK3576_WuKong Board` and follow [deployment/README.md](deployment/README.md). Record the actual OS, kernel, toolchain, memory, storage, and device topology for each run. Recheck current devices and deployments; historical reports do not establish their current state.
- Use the board's system `librknnrt.so`. Resolve its actual path and verify its version and checksum against the approved Runtime baseline pinned in the [SDK image recipe](dockerfiles/Dockerfile.rk3576-humble-amd64). Record the results; never overwrite the system library with a container copy. Do not run `rknn_server` merely to query a version: it starts a foreground service.
- Source `/opt/ros/humble/setup.bash` in each non-interactive board command. Recheck NPU and display devices and their access permissions on the selected board.
- Before capture, run `v4l2-ctl --list-devices` and `media-ctl -p`; verify the sensor, node, format, and access permissions. Development board 1/2 results do not substitute for production-board acceptance.

## ROS and review constraints

- Preserve existing `/fpv/gimbal`, `/fpv/odom`, `/fpv/tracker_trigger`, `/fpv/target_bbox`, `/fpv/tracker_image`, and `/fpv/tracker_state` contracts except for approved changes. RK3576 pairs floating bounding boxes with line-of-sight (LOS) observations on `/fpv/target_los` and records `/mavros/imu/data`. Follow [target-observations](openspec/specs/target-observations/spec.md) for timestamps, validity, queue and reliability settings (QoS), and ROS bag contents.
- Preserve `TrackerState`: `Detecting=0`, `TargetFound=1`, `Tracking=2`, `TargetLost=3`. Preview is a separate lifecycle and must not invent tracking states. Update all affected parameter templates and launch overrides when defaults or units change.
- For RK3576 camera input with `osd.enabled=true`, service startup begins preview and OFF leaves capture running. Models remain shared across tracking rounds; final service shutdown owns device release. Other input modes retain their specified lifecycle.
- Treat `readerwriterqueue.h`, `threadpool.h`, and other embedded dependencies as external code unless directly in scope. Review cross-platform dependency isolation, model compatibility, failure cleanup, and producer/consumer assumptions.
