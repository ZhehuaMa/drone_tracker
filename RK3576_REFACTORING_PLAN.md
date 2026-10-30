# Jetson-to-RK3576 architecture and remaining validation

## Current implementation

The repository uses one public tracking algorithm with a platform backend selected at build time. RK3576 camera input, ROS video input, HDMI display, original-camera recording, diagnostics, and deployment packaging are implemented. These capabilities have different configuration and acceptance boundaries; their presence does not establish full Jetson equivalence or flight readiness.

The raw CMake default is `DRONE_TRACKER_PLATFORM=jetson`. The supported RK3576 entry point, `./build.sh`, explicitly selects `rk3576` and produces ARM64 binaries with a native x86_64 cross compiler. `--backend arm64` selects the ARM64-image fallback. Current build SDKs provide RKNN Runtime 2.3.2; the target compatibility baseline is Runtime 2.3.2 and RKNPU driver 0.9.8. See the [build guide](dockerfiles/README.amd64.md) and local environment instructions.

This document describes the current architecture and remaining work. Main specifications under `openspec/specs/` define approved behavior. Dated change reports retain the scope and limitations of their original measurements.

## Responsibilities and dependency boundaries

| Boundary | Responsibility | Implementation |
|---|---|---|
| ROS orchestration | Parameters, input selection, tracking rounds, observations, recording sessions | `src/drone_tracker_node.*`, `src/drone_tracker_main.cpp` |
| Public algorithm | Detection scheduling, tracking, history replay, arbitration, box decoding, center fusion and rotation compensation | `src/hybrid_tracker/` |
| Public frame and platform interfaces | Shared read-only images, frame-source operations, component creation and compatibility checks | `src/frame/`, public headers under `src/platform/` |
| Jetson backend | TensorRT inference, VPI/CUDA preprocessing and device resources | `src/platform/jetson/`, Jetson model/preprocessing adapters |
| Jetson input | Camera/video capture, decoding and original input queues | `src/mv_camera/` |
| RK3576 backend | V4L2 capture, software MJPEG decoding, RKNN sessions and CPU model preprocessing | `src/platform/rk3576/` |
| Display and recording | Common display policy plus RK3576 DRM/RGA output; independent MPP recording worker | `src/osd/`, `src/platform/rk3576/osd/`, `src/platform/rk3576/recording/` |

V4L2 is the Linux camera interface. MJPEG stores each video frame as an independent JPEG image. DRM/KMS controls display planes, RGA performs display image operations, and MPP provides hardware video encoding. The current software video source does not use hardware decoding. RGA display support does not imply RGA preprocessing for inference.

Keep SDK types, native handles, memory synchronization, and device ownership inside platform implementations. Public interfaces describe the requested operation and its result. Public algorithms may depend on portable configuration and diagnostic support, but must not acquire a reverse dependency on the hardware backend.

## Frames, creation and shutdown

`FrameRef` is a shared pointer to a read-only frame. It carries dimensions, format, row stride through its read view, and a microsecond timestamp. `FrameReadView` retains the lifetime of a host mapping or device lock; a borrowed `cv::Mat` must not outlive that view. A source or factory may be destroyed while independently owned returned frames remain valid.

`CreatePlatformFactory()` returns the selected platform implementation. It provides:

- `createFrameSource(config)` for camera or video input.
- `createFrameBuilder()` for copying host images into compatible owned frames.
- `createTrackerComponents(paths)` for complete model, preprocessing, and frame-compatibility dependencies.

`TrackerComponents` transfers exclusive component ownership to `HybridTracker`. Failed assembly returns a diagnostic rather than a partially usable pipeline. Compatibility checks reject an incorrect frame implementation before changing tracking state. Model and preprocessing operations still validate their own inputs.

For example, an asynchronous detection can start on frame 100 and finish while tracking processes frame 104. Shared references keep frame 100 alive while the detector and history replay need it. Ending that round cancels unstarted work, waits for in-flight accesses, and clears old requests/results before the next round uses the same models.

Keep each capture queue's single producer and single consumer. Continuous preview uses one persistent dispatcher as that consumer and retains at most one pending tracking task; new tasks may replace unstarted tasks. Display and tracking share read-only references rather than independently consuming the camera queue. Final destruction joins threads before releasing the resources they access. Unconfirmed device access must follow the specified fatal-error policy instead of freeing possibly active storage.

## Model contracts and current execution

The three logical model roles are YOLO detection, NanoTrackV3 feature extraction, and NanoTrackV3 matching. Jetson uses three TensorRT engine files. RK3576 uses four RKNN files: YOLO, template Backbone, search Backbone, and Head. The Backbone extracts image features; the Head compares template and search features.

Keep raw-output decoding and detection/tracking arbitration in the public algorithm. YOLO's configured target is one exact zero-based class ID. NMS, or non-maximum suppression, removes overlapping duplicate candidates. Parameter and model changes must retain the approved preprocessing, threshold, coordinate, and output semantics.

RK3576 NanoTrack prepares Float32 image inputs without an extra divide by 255. Template features use the host Float32 path. Search features pass from the search model to the Head through shared device memory. The native search output and Head input must agree on FP16 type, grouped channel layout, stride, and safe capacity. Bindings are refreshed for each execution; incompatible attributes or missing shared-memory support fail explicitly. There is no automatic fallback to the old CPU transfer. Final classification and location outputs return as Float32 values to the public tracker.

Model replacement requires validation of dimensions, color/grayscale semantics, tensor order and layout, type, quantization, normalization, crop/letterbox behavior, and output meaning. Loading succeeds only for supported contracts; it is not proof of numerical or tracking equivalence. Do not retrain or convert existing models as a default migration step.

## Input, preview and output lifecycles

RK3576 ROS input supports local MJPEG files and explicit AR0234 or SC233HGS camera configuration. AR0234 supports continuous capture and periodic software triggering. SC233HGS uses continuous capture and validates startup timestamps before delivering qualified frames. Device mappings and camera calibration must be supplied for the actual board.

For a camera with `osd.enabled=true`, service startup begins capture and HDMI preview after initialization. ON creates a tracking round and explicitly enabled recording sessions. OFF rejects further old-round results, clears target decoration, and resumes fresh preview while model/recording cleanup completes. It does not reopen or stop the camera. Camera failure ends the service with a nonzero result; safe service shutdown releases the devices. Finite video, cameras without OSD, independent input tools, and Jetson retain their specified ON/OFF and EOF lifecycles.

`need_save` controls RK3576 ROS bag recording. Bags contain the configured non-image observation and attitude topics; `/fpv/tracker_image` is never recorded. `visualized_result` independently enables live debug images for consumed tracking frames. `recording.enabled` controls SC233HGS original-camera H.264/TS video in an independent process; its failure does not stop otherwise healthy tracking or display.

RK3576 bbox and LOS observations share one reference timestamp. LOS means the camera-to-target direction and its rotation-compensated angular rates. Default `publish_prediction=false` uses capture time; prediction mode selects an eligible pose time or falls back to capture time. Preserve the separate image capture timestamp, floating observation precision, validity rules, and QoS. Tracking states remain `Detecting=0`, `TargetFound=1`, `Tracking=2`, and `TargetLost=3`; preview does not create another algorithm state.

The `ON MISSION` display requires a received mission-start event and fresh connected, armed, guided flight status. Tracking-state publications do not determine its lifetime. Follow the current mission policy and [HDMI guide](docs/RK3576_HDMI_OSD.md) for cancellation and reauthorization.

## Verification already recorded

The following are historical evidence sources, not claims that the current checkout was rerun on hardware:

| Area | Recorded scope |
|---|---|
| Public factory | [Factory verification](openspec/changes/archive/2026-09-10-introduce-platform-factory/verification.md): portable contracts and source review; no Jetson execution claim |
| RKNN model backends | [YOLO board validation](openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/board-validation.md), [NanoTrack validation](openspec/changes/archive/2026-09-11-implement-rk3576-nanotrack-backend/validation.md), and [shared-feature adoption](openspec/changes/archive/2026-09-22-adopt-nanotrack-search-direct-handoff/validation.md) |
| Software video and ROS video | [Video-source validation](openspec/changes/archive/2026-09-14-implement-rk3576-video-frame-source/validation.md) and [ROS-video validation](openspec/changes/archive/2026-09-15-integrate-rk3576-ros-video/validation.md) |
| Camera and ROS integration | [Camera-source validation](openspec/changes/archive/2026-09-15-implement-rk3576-camera-frame-source/validation.md) and [ROS-camera final board validation](openspec/changes/archive/2026-09-15-integrate-rk3576-ros-camera/final-board-validation.md); both changes are archived |
| HDMI, recording and preview | [HDMI final validation](openspec/changes/archive/2026-09-23-integrate-rk3576-hdmi-osd/final-validation.md), [recording final validation](openspec/changes/archive/2026-09-24-add-rk3576-camera-recording/final-validation.md), and [continuous-preview validation](openspec/changes/archive/2026-10-13-decouple-rk3576-hdmi-stream-from-tracker-trigger/validation.md) |

Read each report's board, program, model, input, timing, and failure boundaries before reusing a conclusion. An old SDK version or completed earlier run does not define the current build or prove acceptance of a changed binary. Raw outputs under `.docker-artifacts/` and `/tmp` may have been deleted. Preserve required acceptance evidence in an explicitly retained location outside disposable build caches.

## Remaining validation and change process

- The active [complete-replay task list](openspec/changes/assemble-rk3576-tracker-replay/tasks.md) retains incomplete FP16/INT8 automatic-capture and required-scene acceptance. Existing resource and integration evidence does not turn an unsuccessful detection-quality run into a pass.
- The active [diagnostic task list](openspec/changes/add-rk3576-tracking-diagnostic-process/tasks.md) retains its independent basic-mode overhead acceptance. Recorded diagnostic or regression runs do not substitute for that prescribed paired measurement.
- Jetson hardware build/runtime equivalence, NPU device-memory accounting where not measured, physical camera timestamp/exposure synchronization, calibrated angular accuracy, and flight behavior require their own evidence. Synthetic intrinsics and unannotated footage cannot establish these properties.
- Scope each future platform optimization through OpenSpec. Freeze program, model, input, configuration, board, and workload identities; compare observable results before claiming a performance benefit. Follow each change's actual deadlines and acceptance conditions instead of combining unrelated historical tests.

For future changes: inspect the existing change, complete or revise its planning artifacts, implement its tasks, verify code and specifications together, then synchronize or archive. Keep the system Runtime unchanged during application deployment and use the [deployment guide](deployment/README.md) for selected installation paths, service ownership, and rollback.
