# Platform factory and RK3576 operation

[Project overview](../../README.md) · [Documentation index](../../docs/README.md)

`PlatformFactory` creates frame sources, host-image frame builders, and a complete set of model and preprocessing components. The shared tracker uses these interfaces without including hardware SDK types. A frame reference shares immutable image storage so asynchronous detection can safely outlive its caller.

The raw CMake default is `DRONE_TRACKER_PLATFORM=jetson`. The supported RK3576 entry point, `./build.sh`, explicitly selects `rk3576`. Unknown platform values fail before hardware dependency discovery. Jetson uses CUDA, VPI, TensorRT, and Jetson Multimedia API behind its platform boundary. RK3576 uses the RKNN backend, software MJPEG decoding, and V4L2 camera capture. HDMI rendering uses RGA; model image preprocessing remains separate from that display acceleration.

## Build and output architectures

Run from the repository root:

```bash
./build.sh --test
# Explicit ARM64-image fallback:
bash tests/tracking_regression/prepare_test_tools.sh --backend arm64
./build.sh --backend arm64 --test
```

The default x86_64 image cross-compiles ARM64 node, recording, diagnostic, and benchmark programs. The fallback runs an ARM64 compiler image. Neither image provides physical RK3576 devices. See [the image guide](../../dockerfiles/README.amd64.md) and `AGENTS.local.md` for SDK paths, isolated caches, test launchers, and packaging.

Portable OSD, preprocessing, and camera-only checks can run as native x86_64 programs. Full RKNN targets need genuine ARM64 SDK libraries. When adapting a standalone ARM64 SDK command to the x86_64 image, pass `-DCMAKE_TOOLCHAIN_FILE=/opt/toolchains/aarch64-humble.cmake` and select target dependencies under `/opt/arm64-sysroot`. Execute ARM programs on a matching board or through the selected QEMU launcher. Use separate native and ARM build directories outside read-only source mounts.

The compatibility baseline is RKNN Runtime 2.3.2 with RKNPU driver 0.9.8. Production programs use the board's system Runtime; do not replace it with a container library. Historical reports using an older SDK describe only their dated builds. `.docker-artifacts` and `/tmp` are disposable locations and may be cleared at any time. A path in a report records provenance, not assured evidence availability. Copy needed evidence to separately managed storage.

## Integrating a caller

Link the final program to `hybrid_tracker` and `platform_${DRONE_TRACKER_PLATFORM}`. Include `platform/platform_factory.h` and `hybrid_tracker.h`; platform-specific image or SDK headers are unnecessary.

1. Call `CreatePlatformFactory()`.
2. Create an input with `createFrameSource(config)`, or a host-image builder with `createFrameBuilder()`.
3. Supply explicit model roles to `createTrackerComponents(paths)` and check its result.
4. Move the complete component collection into `HybridTracker::CreateInstance(settings, ...)`, then call `init()`.
5. Start capture and processing only after initialization succeeds.
6. Stop consumers and capture before destroying the tracker. Its final cleanup waits for asynchronous detection before releasing accessed resources.

Jetson requires three model files: YOLO, a shared Backbone, and Head. RK3576 requires four files: YOLO, template Backbone, search Backbone, and Head. A Backbone extracts image features; Head compares template and search features. The RK3576 shared `backbone` field must remain empty. Mixed shared/split paths are rejected before loading, and roles are never inferred from filenames.

For an RK3576 caller with a prepared `TrackerSettings settings` and a Gray8 `cv::Mat gray`:

```cpp
auto factory{CreatePlatformFactory()};
auto builder{factory->createFrameBuilder()};
ModelPaths paths{};
paths.yolo = "/models/yolo.rknn";
paths.template_backbone = "/models/template_backbone.rknn";
paths.search_backbone = "/models/search_backbone.rknn";
paths.head = "/models/head.rknn";
auto components{factory->createTrackerComponents(paths)};
if (!builder || !components) {
    throw std::runtime_error((!builder ? builder.error : components.error).describe());
}
auto tracker{HybridTracker::CreateInstance(settings, std::move(components.value))};
if (!tracker->init()) { throw std::runtime_error("Tracker initialization failed"); }
auto copied{builder.value->copyFrom(gray, 1000000)};
if (!copied) { throw std::runtime_error(copied.error.describe()); }
FrameContext context{};
context.frame = std::move(copied.value);
context.now_us = 1000000;
auto output{tracker->update(context)};
```

`TrackerComponents` is movable, with exclusive ownership transferred into the tracker. Components and returned frames do not borrow the factory's lifetime. `FrameCompatibility` checks the actual frame implementation, allowing compatible frames from another factory instance. Incompatible input is rejected before algorithm time, frame count, or detection submission changes; `TrackerOutput::consumed` is false and `info` explains the cause.

`waitForLatestFrame(source, frame, timeout)` waits for one frame and drains older queued frames through the same consumer. There must be only one consumer of the original queue. Continuous ROS preview uses a dispatcher as that consumer, with at most one pending tracking task. It shares immutable frames with display and tracking; neither opens a second camera or independently drains capture.

`finishRound()` must run after updates stop. It cancels pending detection, waits for in-flight frame access, and clears unread results. `reset()` starts fresh algorithm state while preserving loaded models. Old requests/results cannot enter the next round. Jetson retains its frame-source recording boundary; RK3576 uses `need_save` for ROS bags and `recording.*` for independent SC233HGS video recording.

## Standalone YOLO

The standalone build needs C++17, OpenCV core/imgproc/dnn/video/imgcodecs/videoio, threads, and the ARM64 RKNN SDK. It does not require ROS or Jetson SDKs. A native ARM64 SDK example is:

```bash
cmake -S src/platform/rk3576 -B build/rk3576-yolo \
  -DRKNN_INCLUDE_DIR=/usr/include \
  -DRKNN_LIBRARY=/usr/lib/librknnrt.so -DCMAKE_BUILD_TYPE=Release
cmake --build build/rk3576-yolo -j4
ctest --test-dir build/rk3576-yolo --output-on-failure
```

The accepted YOLO contract is 640×640 grayscale replicated into three channels, with six outputs. Scales are 80, 40, and 20; each has a 64-channel box-distribution output followed by C raw class-score channels. Native input is INT8 or FP16 NHWC. NHWC orders batch, height, width, and channels. INT8 uses affine quantization with zero point -128 and scale 1/255. Floating input attributes may carry NONE or AFFINE metadata without integer rescaling rules.

The application always submits UINT8 NHWC `[1,640,640,3]`, 1,228,800 bytes, with `pass_through=0`. The model conversion must encode mean=0/std=255. Pixel 150 is submitted as byte 150; Runtime performs the configured conversion. The application does not divide by 255, subtract 128, or store FP16 in advance. Output is requested as Float32 and is not dequantized twice. Model metadata and fixed-input comparison must establish numerical meaning; shape queries alone cannot prove normalization or whether class scores already contain Sigmoid probabilities.

Examples, using an existing compatible model and user-supplied input:

```bash
build/rk3576-yolo/rk3576_yolo_probe --model /models/yolo.rknn --mode query
build/rk3576-yolo/rk3576_yolo_probe --model /models/yolo.rknn \
  --mode detect --target 2 --image /inputs/sample.jpg
build/rk3576-yolo/rk3576_yolo_probe --model /models/yolo.rknn \
  --mode detect --target 2 --video /inputs/clip.mov
mkdir -p results
build/rk3576-yolo/rk3576_yolo_probe --model /models/yolo.rknn \
  --mode region --image /inputs/sample.jpg --dump results/sample
```

`--target` is one exact zero-based class ID, also exposed as `tracker.yolo_target_class_id` in ROS. A target of 0 means class 0. It does not accept every class. Class bounds are checked before detection starts. `--roi x y width height` supplies a preferred image region. Detect mode consumes one-call regional results until a Searching seed or complete coverage. Each stage retains its candidate set and provenance. The tracker uses separate regional confirmation and plan-level verdicts. Other classes do not participate in NMS, the removal of highly overlapping duplicate boxes. Crop mode (`--mode crop`) returns only the first requested region for exact-pixel comparison. `--confidence`, `--nms`, and `--relax` identify the probe profile explicitly; they do not change ROS configuration defaults.

Region mode exports `.input.u8`, six `.output0.f32` through `.output5.f32` files, and `.prediction.f32`. `--constant 0`, `114`, or `255` can replace an image for numerical checks. Nine-output variants, precomputed-probability variants, and arbitrary normalization contracts are not accepted. See the dated [YOLO validation](../../openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/board-validation.md) and [FP16 input change](../../openspec/changes/archive/2026-09-20-support-rk3576-yolo-fp16-input/validation.md) for their actual evidence and limitations.

## Standalone NanoTrack

The same standalone build produces `rk3576_nanotrack_probe`:

```bash
build/rk3576-yolo/rk3576_nanotrack_probe \
  --template-model /models/template_backbone.rknn \
  --search-model /models/search_backbone.rknn \
  --head-model /models/head.rknn --mode query
build/rk3576-yolo/rk3576_nanotrack_probe \
  --template-model /models/template_backbone.rknn \
  --search-model /models/search_backbone.rknn \
  --head-model /models/head.rknn --mode replay \
  --video /inputs/mjpeg.mov --bbox 100,80,40,30
```

The probe's replay reader requires local MJPEG video and decodes luminance as Gray8, one byte per pixel. `--bbox` is the first-frame left/top/width/height in pixels. Query mode loads and validates the same production backend, including its shared-memory contract.

| Model role | Logical input | Logical output |
|---|---|---|
| Template Backbone | `[1,3,127,127]` | `[1,96,8,8]` |
| Search Backbone | `[1,3,255,255]` | `[1,96,16,16]` |
| Head | Template and search features above | Classification `[1,2,15,15]`; position `[1,4,15,15]` |

Logical shapes are interpreted as NCHW: batch, channels, height, width. Roles match unique shapes rather than export names or enumeration order. Classification values are raw scores; position values are left/top/right/bottom distances. Crop preprocessing converts to Float32 before resizing, replicates grayscale across three channels, and preserves the 0–255 range without an extra division by 255.

Production `loadNanoTrack` uses shared device memory for the search Backbone-to-Head transfer. The native tensor, meaning the device's actual storage arrangement, must be compatible FP16 NC1HWC2 on both sides, including channel groups, spatial dimensions, stride, and capacity. NC1HWC2 splits channels into storage groups; the validated search shape includes `[1,12,16,16,8]`. Search features are not read, copied, or converted by the CPU. Template features and final Head outputs still use Float32 host transfers.

Each search input submission is followed by rebinding the search output. After that model completes, template input is submitted to Head and its shared search input is rebound before execution. The next search cannot overwrite storage while Head reads it. Incompatible native storage or missing shared-memory support rejects backend creation; there is no automatic host-transfer fallback. A private `loadNanoTrackCpuReference` remains available for explicit numerical-reference work.

Logical FP16/Float32 and supported affine INT8 attributes are still validated. A model's precision label alone does not establish that its search edge satisfies the native FP16 requirement. Historical four-precision results describe the former host-transfer implementation; see [original backend validation](../../openspec/changes/archive/2026-09-11-implement-rk3576-nanotrack-backend/validation.md) and [production adoption](../../openspec/changes/archive/2026-09-22-adopt-nanotrack-search-direct-handoff/validation.md). They do not establish current compatibility for an arbitrary replacement model.

Failures before device execution return diagnostics. A recoverable search failure follows the shared tracker's existing failure path. If device access cannot be confirmed stopped, or shared-memory release fails, the established fatal policy terminates the process rather than releasing storage that might still be in use. Reset invalidates the template without reallocating shared search storage; destruction releases the consumer import before the producer allocation.

## Complete sequential tracker replay

`rk3576_tracker_replay CONFIG.yaml` creates four-model components through the factory and runs the real `HybridTracker`, starting with automatic detection. It requires no manual first-frame box. The standalone build additionally uses OpenSSL Crypto for file fingerprints. This tool has its own synchronous decoder and does not provide ROS, camera capture, or display acceleration.

Copy [the FP16 configuration](../../parameters/replay_fp16.yaml) or [the INT8 configuration](../../parameters/replay_int8.yaml), then supply actual files, compatible model contracts, fixed video FPS, intrinsics, and a new output directory. Historical precision examples remain subject to the current native search-edge validation.

```bash
build/rk3576-yolo/rk3576_tracker_replay /absolute/path/run.yaml
```

Exactly one configuration is accepted. It supplies models, video, camera, algorithm, and output; there are no additional runtime options, ROS YAML imports, or environment overrides. Relative paths resolve from that configuration's directory. Keep the OpenCV `%YAML:1.0` header. Fields are required, booleans use integer 0/1, and unknown/duplicate keys, multiple documents, invalid numbers, and existing output directories are rejected before loading models.

Intrinsics `fx/fy/cx/cy` are pixels in the decoded image. No image or intrinsics scaling is added. Fixed-rate frame i has time `1000000 + round(i * 1000000 / fps)` microseconds: at 30 FPS, the first three are 1000000, 1033333, and 1066667. Prediction time equals frame time, with identity camera rotation. Color input is converted once to grayscale. Unsupported formats, size changes, and a valid reported FPS conflicting with the configuration fail.

Replay waits one frame period after the previous update completes. A 60 ms update at 30 FPS is followed by approximately 33.333 ms waiting. It does not skip frames or burst to catch up. This differs from a real-time queue and cannot measure total model throughput.

| Output | Meaning |
|---|---|
| `frames.csv` | Every update, including unconsumed input, with times, state, boxes, score, and duration |
| `run.json` | Resolved configuration, file fingerprints, build identity, queried model/Runtime attributes, input statistics, state transitions, durations, and end reason |
| `diagnostics.log` | Model, algorithm, and process diagnostics, separate from CSV |

CSV `display_*`/`realtime_*` boxes use center-x/center-y/width/height; `nano_*` uses left/top/width/height. Invalid coordinates and non-finite scores are blank. Negative NanoTrack score indicates no valid inference. JSON counts and frame IDs use decimal strings to preserve integer precision. Durations distinguish decoding, conversion/copy, update, and waiting; parent and nested durations must not be added twice. P95 selects sorted item `ceil(0.95*N)`; empty samples are null.

| End reason | Exit code |
|---|---:|
| `eof`, after reaching the declared frame count | 0 |
| `frame_limit` | 0 |
| Configuration or argument error | 2 |
| `decode_error`, `end_unverified`, `initialization_error`, `runtime_error` | 1 |
| `interrupted`, after safe SIGINT cleanup | 130 |

The initial report state is `running`; ordinary completion replaces it with the final report. Write/flush/close failures are failures. Cleanup stops input and waits for detection without adding a fictitious final frame. SIGKILL, power loss, or fatal device failure cannot guarantee a final report. A normal exit does not establish that a target was found or that tracking was accurate. Historical integration, quality gaps, and resource checks remain in the [assembly records](../../openspec/changes/assemble-rk3576-tracker-replay/validation.md).

The three startup deadlines are `confirmation_timeout_ms=100`, `candidate_timeout_ms=1000`, and `expiry_check_interval_ms=10` under the replay `tracker` map. They are positive integer engineering defaults, not validated flight limits. The check interval cannot exceed confirmation lifetime. `lost_max_count` abandons provisional attempts; legacy jump settings are accepted but inactive.

Standalone replay uses synthetic `standalone-admission-v1` evidence time. Source frame timestamps remain unchanged. Decode, copy, and finish the scheduler wait before sampling each image's immutable monotonic admission anchor. Initialization, inference, catch-up, and later stalls consume that image's lifetime. New images and result arrival cannot renew it. At 50 FPS, F1 admitted at 5000 ms and F2 admitted at 5140 ms have ages 170/30 ms at 5170 ms; F2 expires at 5240 ms for a 100 ms lifetime. Production and independently timed regression use `capture-monotonic-v1`, retaining pre-admission age. Slowed standalone results establish neither production-camera timing nor physical rotation acceptance. Historical report version 2 appends exact integer image anchors, decisions, attempts, confirmation deadlines, and consumed-image availability. Report version 3 additionally identifies the one-call scheduler and inactive no-result frame cleanup. It leaves job/plan counts and seed-disposition evidence unavailable when no paired format-4 diagnostics were recorded. ROS admission and subscriber delivery remain unavailable in this runner.

## Continuous MJPEG frame source

Factory `FrameSourceType::Video` accepts a local ordinary file whose first video stream is MJPEG. Audio and other streams are skipped. Eight-bit grayscale and YCbCr 4:2:0, 4:2:2, or 4:4:4 decode directly to original-size Gray8, without color round trips or luminance stretching. Unsupported depth, sampling, or color encoding fails.

Creation opens the file to verify the first frame, then closes it. Each `start()` reopens the configured path from the beginning, including the first start; a missing file or changed dimensions fails startup. Video ignores camera dimensions, nominal FPS, flipping, and mirroring. Its recording configuration does not create camera output.

Real-time mode takes a new system-time origin per start. History mode strictly reads a positive decimal int64 microsecond origin from the container's case-insensitive `Artist` field; missing, duplicate, or invalid values fail. Output time is origin plus PTS, the video's presentation timestamp, rounded to the nearest microsecond with halves upward. Negative, missing, repeated, backward, and overflowing times fail. A first-frame offset is preserved. Playback waits on a monotonic clock, which is unaffected by calendar correction, and processes immediately when behind.

One consumer may run while `stop()` wakes producer waits. Stop preserves queued tail frames and a determined EOF/failure reason. `isRunning()==false` does not imply an empty queue. Finish consumer calls before restarting or destroying the source. A restart clears the old queue; history timestamps can repeat across starts, so callers must also reset algorithm rounds. Frames and read views outlive the decoder, source, and factory.

The `video_source_driver FILE HISTORY SLOW_MS ROUNDS` tool exercises the real factory and latest-frame consumer. `video_board_validation FIXTURE_DIRECTORY 600` performs its dated restart/lifetime/slow-consumer protocol. Prepare fixtures with `video_fixture_generator`. These tools do not load models. See [video-source validation](../../openspec/changes/archive/2026-09-14-implement-rk3576-video-frame-source/validation.md) for the actual board and measured scope.

## Camera sources and configuration

Production RK3576 factories enable camera support. Independent factories must set `rk3576::FactoryOptions::enable_camera=true`; the default independent factory rejects camera requests. Sources use V4L2, the Linux capture interface. AR0234 provides original-size grayscale continuous capture or periodic software triggering. SC233HGS accepts continuous capture through an already prepared ISP/3A chain and produces Gray8 for tracking from NV12 input. Device paths depend on the actual wiring and driver topology.

Copy the appropriate parameter file and fill actual model paths and camera values:

| Configuration | Runtime defaults |
|---|---|
| [AR0234](../../parameters/rk3576_ar0234_camera_params.yaml) | Software trigger 20 Hz; device 60 FPS; OSD, timing, recording, and bag disabled; model paths empty |
| [SC233HGS production](../../parameters/rk3576_sc233hgs_production_params.yaml) | Continuous 60 FPS; OSD, timing, independent recording, and bag enabled; explicit target-board model paths |
| [MJPEG video](../../parameters/rk3576_video_params.yaml) | OSD, timing, independent recording, and bag disabled; explicit video/models required |

These are template settings, separate from C++ declaration defaults. Intrinsics in both camera templates are simulated; they do not establish measured lens calibration or rotation-compensation accuracy. Absolute model paths are used directly; relative ROS model paths resolve below the installed package's `share/drone_tracker/models/`. RK3576 needs `tracker.yolo_engine`, `nano_template_backbone`, `nano_search_backbone`, and `nano_head`, with `nano_backbone` empty.

Before capture, inspect `v4l2-ctl --list-devices`, `media-ctl -p`, and existing device users. Then start with an explicit input:

```bash
ros2 launch drone_tracker rk3576_camera.launch.py \
  params_file:=/absolute/path/camera.yaml video_source:=/dev/video22
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header '{frame_id: ON}'
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header '{frame_id: OFF}'
```

The example node is not universal. The launch accepts `/dev/video10` and shorthand `video10`, and overrides only the input path. Its default parameter template is AR0234. Models, mode, and algorithm values come from the selected file.

| Field | Constraint |
|---|---|
| `mv_camera.sensor_model` | `ar0234` or `sc233hgs`; unspecified defaults to AR0234 |
| `capture_mode` | `continuous` or `software_trigger`; SC233HGS requires continuous |
| `fps` | Positive integer device rate |
| `trigger_rate_hz` | Finite 1..fps in software mode; 0 in continuous mode |
| `media_device`, `sensor_entity` | Explicit current topology; SC233HGS uses these as diagnostic identity |
| `buffer_count` | 2..32; default 4 |
| `width_px`, `height_px` | Actual captured pixel dimensions |
| `output_width_px`, `output_height_px` | Same as input or 0 to retain size |
| `use_sim_time`, `use_history_timestamp` | false for production RK3576 camera/video nodes |
| `need_save` | Independent ROS bag switch; true is accepted for camera and video |

Capture configuration, models, intrinsics, algorithm, clocks, recording, and OSD options are fixed at startup; changing them requires restart. SC233HGS does not silently switch to AR0234 or software triggering on failure.

A camera copies only valid pixels from each driver row. For a 1920-byte gray row with 2048-byte stride, its 128 padding bytes are excluded. Driver buffers are returned before frames are delivered. Consumer frames and views retain their own storage after source cleanup. Bad indices, lengths, planes, offsets, or error flags fail the stream. Queues discard newly arriving frames when full; the producer does not remove older frames to make space.

AR0234 saves sensor controls before configuration and restores changed controls on cleanup. Each control write is followed by up to 100 ms of readback, at 5 ms intervals, without repeated writes. Software triggers call V4L2 directly from a separate command thread, which never produces image-queue entries. Missed periods are skipped rather than burst-replayed. Counts distinguish commands, received frames, driver gaps, queue drops, and consumed frames. A successful command does not prove exposure or a one-command/one-frame relationship.

Per stream, a fixed offset maps monotonic driver times into system microseconds. `driver_monotonic` means the driver declared that clock domain. `dequeue_fallback` uses host dequeue time when the domain is absent or COPY is reported; it describes retrieval, not exposure. The offset is never recomputed midstream. Invalid, repeated/backward post-warmup time, clock-policy changes, and overflow fail instead of manufacturing timestamps. Capture timeout is two seconds without a successful dequeue. Stop wakes the wait, joins command/capture threads, stops the device, restores applicable controls, and then releases resources. Blocking driver calls still require actual board validation.

### SC233HGS timestamp warmup

Each new SC233HGS stream observes timestamps before delivering pixels. The fixed bounds are two seconds after successful stream start and at most 120 successfully dequeued buffers. Eight strictly increasing timestamps make the eighth frame eligible. Equal times reset the streak to one; earlier observations are returned to the driver without tracking/recording pixels. A deadline takes precedence over readiness. Buffer 120 may succeed within the deadline; no buffer 121 is examined.

Backward/invalid/overflowing timestamps, changed clock policy, and duplicate/backward driver sequence remain fatal during warmup. Forward sequence gaps are counted separately. A real capture stop cancels warmup. In continuous preview, ON/OFF does not restart or cancel that stream warmup. AR0234 has no such observation phase. Warmup establishes timestamp progression, not exposure synchronization, focus, or image quality.

Capture diagnostics distinguish `dequeued_total`, `warmup_discarded`, `terminal_discarded`, qualified `read`, queue `enqueued`/`dropped`, and driver gaps. After cleanup, `dequeued_total = warmup_discarded + read + terminal_discarded`, and `read = enqueued + dropped`. `resources_reclaimed=false` means release has not been confirmed even if counts balance.

### Independent camera tools

Camera-only builds require Linux, C++17, OpenCV, threads, JPEG, and FFmpeg development libraries; they do not discover RKNN, ROS, RGA, DRM, or Jetson SDKs:

```bash
cmake -S src/platform/rk3576 -B build/camera-only \
  -DDRONE_TRACKER_CAMERA_ONLY=ON -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-only -j4
ctest --test-dir build/camera-only --output-on-failure
```

The driver keeps its name `ar0234_camera_source_driver` for both sensors. For a verified SC233HGS topology, an example is:

```bash
./ar0234_camera_source_driver --sensor-model sc233hgs \
  --video-device /dev/video22 --media-device /dev/media2 \
  --sensor-entity 'm00_b_sc233hgs 0-0030' \
  --width 1920 --height 1200 --fps 60 \
  --frames 60 --rounds 20 --output /external/warmup-rounds-new
```

AR0234 finite software triggering additionally accepts `--mode software_trigger --trigger-hz 20 --commands 60 --tail-ms 500`. Zero commands test no-trigger behavior; one tests a single request. Tail waiting is 0..1500 ms. `--frames`, `--seconds`, `--rounds`, `--instances`, `--slow-ms`, `--snapshots`, and `--compare-pixels` select bounded observations. Output must not exist, and its parent must exist. Check tool configuration before a device run; long historical protocols are not default permission to occupy a production board.

Reports retain frame times/stride/pixel checks, trigger attempts, round results, process resources, and optional PGM snapshots. SC233HGS also exports bounded `warmup.csv` and `capture.csv`. Host memory excludes unmeasured device-internal memory. Trace callbacks and snapshots can affect throughput. Dated [camera-source validation](../../openspec/changes/archive/2026-09-15-implement-rk3576-camera-frame-source/validation.md) and [ROS camera validation](../../openspec/changes/archive/2026-09-15-integrate-rk3576-ros-camera/final-board-validation.md) identify their actual board, inputs, trigger-rate limitations, and missing quality checks.

## ROS video and continuous HDMI camera service

For finite video, use `rk3576_video.launch.py params_file:=/absolute/video.yaml video_source:=/absolute/mjpeg.mov`. The production video reader requires local MJPEG. Initialization waits for ON; every ON starts from the first frame, duplicate ON is idempotent, OFF stops input, and EOF finishes the round without automatic looping. Models are reused. After safe cleanup another ON can retry a restored input. See [the dated ROS video validation](../../openspec/changes/archive/2026-09-15-integrate-rk3576-ros-video/validation.md).

For an RK3576 camera with `osd.enabled=true`, service startup opens capture and HDMI after parameters, models, ROS interfaces, and display initialize. Before the first qualified frame the screen is black with waiting status and available telemetry. Healthy preview continues before ON and after OFF. ON starts tracking plus explicitly enabled recording/bag sessions. OFF closes result reception and clears target decorations immediately, while model and recording cleanup finish independently. Stop the service to release camera and HDMI devices.

One persistent dispatcher drains capture. During Starting, Stopping, and idle phases it submits fresh unboxed preview. During Running, HDMI receives only images actually consumed by the tracker with their own `display_box`. A 60 FPS camera with 25 consumed frames per second can produce 25 new paired HDMI images per second. Repeated refresh of one image is separate. A stalled Running inference can enter stale-frame waiting; OFF resumes preview without waiting for that inference.

The lifecycle applies to both `cached-overlay` and `legacy`. Cameras with OSD disabled, Jetson, finite video, and independent tools retain their ON/start and OFF/stop behavior. Preview does not publish bbox, LOS, state, or `/fpv/tracker_image`. Duplicate ON keeps the active round. While old model access finishes, only the latest desired ON/OFF state is retained. New rounds begin beyond a new dispatch boundary and never accept old tasks or results.

Camera startup/runtime failure ends continuous service with a nonzero result for the existing service restart policy. ON cannot reopen that failed stream. A tracking exception ends only its round; healthy preview continues and another ON may retry after cleanup. Recording, bag, and runtime display failures remain isolated. A failed display does not automatically reconnect and does not stop healthy tracking.

`TRIG ON/OFF` shows the last control event for two seconds. Mission display requires an actually received `/fpv/mission_state=true` plus fresh connected, armed, guided `/mavros/state`. Tracking-state changes do not determine its lifetime. A MISSION trigger alone is insufficient. Mission end, flight-authorization loss/expiry, OFF, or DISMISS clears it; an invalidated attempt requires a later false then true mission event. DISMISS preserves tracking. See [the HDMI guide](../../docs/operations/RK3576_HDMI_OSD.md) for timing, visible fields, and restart limitations.

Diagnostics use a stream ID and source sequence across ON/OFF, plus separate tracking-round IDs. Counters distinguish capture, dispatch, task replacement, actual consumption, and preview/paired submissions. Control acceptance, display completion, model/bag cleanup, and recording close have distinct timestamps. Display completion does not measure light reaching the screen. Messages already handed to ROS middleware may still arrive after OFF; clients retain their existing drain/discard behavior.

## RK3576 paired target observations

RK3576 publishes floating `vision_msgs/msg/Detection2D` on `/fpv/target_bbox` and `geometry_msgs/msg/TwistStamped` on `/fpv/target_los`. A constructed pair shares its nanosecond reference timestamp. Valid frame IDs are `camera_frame` and `camera_flu`; an empty ID means invalid. FLU means forward/left/up camera coordinates. A valid bbox may coexist with invalid LOS when camera orientation is unavailable. `/mavros/imu/data` is recorded as received and never substitutes for `/fpv/gimbal` camera orientation.

LOS is the line of sight from the camera to the target. `linear.y/z` store downward-positive pitch and left-positive yaw in radians. `angular.y/z` store rotation-compensated rates in radians per second. Other components are zero. These fields are angles, not linear velocity. Two independent ROS topics do not guarantee atomic network receipt; consumers pair messages by timestamp.

`publish_prediction=false` is the default and describes capture time. When true, a latest pose qualifies only if `frame <= pose <= node_now` and its age is at most 300000 microseconds. Otherwise the reference falls back to capture time and the existing nearest capture pose. For frame 10.000 s, pose 10.030 s, and now 10.050 s, default messages use 10.000 s; prediction mode uses 10.030 s. Images always retain capture time. This does not add a freshness guarantee to nearest-pose lookup.

`camera.fx/fy/cx/cy` are explicit input-pixel intrinsics; HDMI sizing does not rescale them. `los.rate_filter_tau` defaults to 0.12 seconds, must be finite/nonnegative, and is fixed at startup. Zero disables rate smoothing. First samples and gaps over 0.3 seconds start at zero angular rate. Invalid target/orientation or successful reanchoring resets rate history. Duplicate/backward observation timestamps suppress the pair independently of state/debug-image publication. A pose-clock rollback resets pose/rate history while retaining the round's publication guard. After system-clock correction, OFF then ON after cleanup establishes a new time base without synthetic times.

OFF attempts one invalid pair for an established round before sealing its bag; repeated OFF is deduplicated. Old frame results cannot publish valid observations afterward. An OFF notification is a control event, not an input frame or a TargetLost transition.

BBox/LOS use reliable depth 10. Gimbal uses best-effort depth 10, MAVROS IMU best-effort depth 100, and debug images best-effort depth 1. For live image inspection:

```bash
ros2 topic echo /fpv/tracker_image --qos-reliability best_effort
```

Debug images require `visualized_result=true` and an active round that actually consumes input. `need_save` controls an independent asynchronous ROS bag containing gimbal, odometry, bbox, LOS, and MAVROS IMU. Bags never register or write `/fpv/tracker_image`, for either visualization setting. Independent original-camera video uses `recording.enabled`.

Bag admission is bounded to 256 messages and 64 MiB. A bbox/LOS pair consumes two slots and is admitted together or rejected together; rejection marks the bag incomplete. A disk error can still leave half a pair written and fails the bag. Open/write/close/collision failures leave healthy tracking, HDMI, and independent camera recording running. Per-round bag directories never overwrite earlier sessions.

## Independent SC233HGS recording

See [the recording guide](rk3576/recording/README.md) for hardware encoding, runtime settings, session evidence, and inspection. The production build enables recording support, while `recording.enabled` controls whether ON actually starts a session. This is independent of ROS bags, debug images, and HDMI. Original NV12 frames are copied only after capture's final accepted boundary; video has no tracking/OSD overlay.

## HDMI display stage timing

See [the HDMI guide](../../docs/operations/RK3576_HDMI_OSD.md#renderers-and-display-stage-timing) for renderer selection, stage boundaries, counter interpretation, and rollback. `osd.timing_enabled` is startup-only and defaults to false in C++; the production camera template explicitly enables it. Set false and restart to disable measurement.

## Verification boundaries

Portable algorithm checks can be built with `cmake -S src/hybrid_tracker -B build/platform-contracts -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON`, followed by build and CTest. They use the real shared tracker with injected dependencies and do not load physical devices or real models. [Factory validation](../../openspec/changes/archive/2026-09-10-introduce-platform-factory/verification.md) records that original scope.

The algorithm states remain Detecting=0, TargetFound=1, Tracking=2, TargetLost=3. Interface/integration results do not prove target accuracy, physical angle accuracy, exposure synchronization, Jetson equivalence, or flight suitability. A new binary or workload requires its own relevant acceptance evidence. Preserve concrete board, model, input, software, and configuration identities with each measured result.

## Evaluated targeted-region hints

The one-call scheduler retains the last successful region as a bounded value-only hint with producing job/source and attempt identity. After seeding, its successful width/height can be recentered at the current source measurement. Bounds clipping still applies, and hints reset when the attempt is abandoned. Regions do not supply tracker measurements or publication permission. During preparation, an already submitted image may use the original seed region before replay produces its measurement.

The November 12 exact-pixel comparison used the deployed YOLO and FP16 NanoTrack models with formal 0.50 confidence, 0.45 NMS, and relaxation 1.0. Twelve later images compared the current 320-pixel region, exact prior successful 739×666 region, and that scale/aspect recentered at independent NanoTrack measurements. Positive-region counts were 1/12, 1/12, and 2/12 respectively. Four initial samples were negative for every region. These small counts support evaluating the recentered hint; they do not establish tracking accuracy, repeatability, or throughput improvement. Raw input bytes, resize/padding transforms, candidates, and identities are retained in the change's dated validation report. Formal lifetime/confidence defaults remain unchanged.
