# Hybrid tracker algorithm review

[Project overview](../../README.md) · [Documentation index](../README.md) · [Tracker source guide](../../src/hybrid_tracker/README.md)

## 1. Scope and components

This document describes code behavior on the current `rk3576` branch. The algorithm entry point is `HybridTracker::update()`. The platform factory assembles dependencies; the ROS node applies parameter overrides and publishes messages. Defaults below come from C++ structures and may differ from the effective YAML or launch configuration.

The pipeline has four components:

1. `YoloV8Detector` asynchronously detects the configured class for initial discovery and later tracking confirmation.
2. `NanoTracker` uses the NanoTrackV3 neural network to estimate the target box on each frame.
3. `NanoDcfTracker` combines NanoTrack dimensions with a smoothed center. Its optional DCF, a discriminative correlation filter, locates the center in grayscale images. Disabling DCF selects an independent center Kalman filter.
4. `RotationKalmanFilter` estimates center, size, and center velocity. It compensates for camera rotation and supports corrections at historical measurement times.

The shared algorithm receives model backends, preprocessors, and frame-compatibility checks through `TrackerComponents`. It does not load hardware SDKs directly. Jetson uses TensorRT/VPI; RK3576 uses RKNN Runtime. The logical models are YOLO, NanoTrackV3 Backbone, and Head. The Backbone extracts image features; the Head compares them. RK3576 uses separate template and search Backbone files.

## 2. Asynchronous YOLO detection

### 2.1. Requests, results, and rounds

The detector has one worker thread and at most one pending request. A new request may replace that pending request without changing an execution already in progress. Requests are rejected when a result is unread, round cleanup is underway, or the detector has stopped. The result slot retains only the latest result.

On normal entry to the detecting or tracking branch, the main thread reads one result snapshot. This releases the submission restriction imposed by an unread result. It then submits or skips the current frame's request before processing the snapshot. The same update does not read a detection that finishes later.

Requests carry source-frame and round identities, plus comparison data such as that source frame's tracking and prediction boxes. Comparison data does not own an image. Executing work, pending requests, and the recovery cache retain their own required shared frame references.

Call `finishRound()` after stopping calls to `update()`. It cancels pending work, waits until executing detection no longer accesses frames, and clears unread results. `reset()` clears algorithm state and increments the round identity. Results from older rounds cannot initialize or arbitrate the current round.

### 2.2. Detection cascade and postprocessing

Each request follows this sequence:

1. If it contains a valid ROI, detect that region first. ROI means region of interest, the part of the image inspected preferentially. This stage uses threshold `yolo_conf_threshold * yolo_recall_relax_ratio`.
2. If the ROI does not contain the target class, detect the whole image.
3. If whole-image detection also fails, use adaptive overlapping tiles. The shorter side is split in two; the longer side's count depends on the aspect ratio. Overlap is `0.2`. The first tile containing a target ends the search.

The cascade selects the first successful stage and returns that stage's highest-confidence single box. Candidates from all stages and tiles are not ranked together. There is no separate tiling switch.

Postprocessing reads only the class score selected by `target_class_id`. It restores letterbox coordinates, filters dimensions, and applies NMS. Letterboxing resizes with preserved aspect ratio and padding. NMS, or non-maximum suppression, removes highly overlapping candidate boxes. `max_box_area_ratio` separately limits candidate width and height relative to the current detection region; it does not limit area directly.

## 3. NanoTrack and center fusion

`NanoTracker` uses a `127×127` template and a `255×255` search input. Classification output has shape `[1,2,15,15]`; location output has shape `[1,4,15,15]`. Postprocessing combines scale and aspect-ratio penalties, a Hann window that favors positions near the search center, and size interpolation.

There is no `tracking_conf_threshold` parameter or fixed `0.8` score threshold controlling updates to the main pipeline. Failed search preprocessing, inference, or output-contract validation returns a negative score. The fusion tracker treats that frame as failed. Diagnostics retain the raw score.

`NanoDcfTracker` keeps NanoTrack width and height and selects the center as follows:

- `enable_dcf=true`: use the grayscale DCF center. PSR, the peak strength relative to background response, indicates confidence in this center. If PSR is below its threshold or the DCF center differs too much from NanoTrack, use the NanoTrack center and retrain the DCF template. An unreadable frame also falls back to the NanoTrack center.
- `enable_dcf=false`: smooth the NanoTrack center with `CenterKalmanFilter`. If its prediction differs too much from NanoTrack, use the NanoTrack center and reset the center filter.

Both paths use center-deviation threshold `dcf_max_div_factor * (width + height) / 2`. The center filter is independent of the main `RotationKalmanFilter`. Disabling DCF therefore still applies center smoothing.

## 4. Per-frame processing and states

### 4.1. Input checks and cache

1. Check the frame, components, and platform compatibility. Frame timestamps must increase strictly. Repeated or backward timestamps return `consumed=false`; the caller skips publication for that frame.
2. Call `predictOnly()` at capture time to obtain the main Kalman prediction.
3. Add the frame to the recovery cache, retaining at most `frame_buffer_size` frames. The cache supplies source images when detection arrives late.
4. Enter the detecting or tracking branch according to tracker initialization state.

Preserve this coordinate detail when interpreting the current code: `predictOnly()` returns center format `(cx, cy, w, h)`, but `kf_pred_box`, used for ROI and jump comparisons, writes the first two values directly into the top-left fields of `cv::Rect2f`. This path has not converted center coordinates to top-left coordinates. Final output boxes perform their own correct conversion.

### 4.2. Uninitialized: discovery and catch-up

Before initialization, each valid update attempts a YOLO request. With a Kalman prediction, the ROI expansion factor is `roi_scale_factor * 1.5`. Without a prediction, detection starts with the whole image.

A positive detection from the current round requires its source image to remain in the cache. Initialize `NanoDcfTracker` on that source image using the detection box, then track subsequent cached images in timestamp order. This catch-up replay advances the delayed detection to the current frame.

For example, detection of frame 100 may be read during the frame-104 update. If frame 100 remains cached, initialize there and process frames 101–104. If frame 100 has been evicted, frame 104 cannot substitute for it during initialization.

Successful initialization enters `TargetFound` and resets the main Kalman filter. A failed individual catch-up step does not make the entire `catchupTracker()` call fail; the display box retains the latest successful result. Ten consecutive failed detection initializations clear Kalman state so later requests stop relying on the old predicted ROI.

### 4.3. Initialized: tracking and jump checks

Each frame first calls `NanoDcfTracker::update()`. A tracking box must differ from both the Kalman prediction and the previous display box to count as a jump. A difference qualifies when center distance exceeds `max_position_jump` pixels or either side-length ratio falls outside `[1/max_size_jump_ratio, max_size_jump_ratio]`.

An isolated jump retains the old display box and does not correct the main filter with that measurement. When consecutive jumps reach `jump_tolerance`, clear tracker initialization and boxes, then return early with `consumed=false`. This path does not additionally set `TargetLost`.

A normal measurement updates the display and previous boxes and retains the floating-point tracking result for Kalman correction. A failed tracking update supplies no measurement. State may remain `Tracking`, and output may continue using the existing prediction.

Every `yolo_interval` valid updates, attempt another YOLO request. Prefer the Kalman prediction for ROI; otherwise use the display box. Expand ROI width and height by `roi_scale_factor`, make each at least half the corresponding YOLO input dimension, then constrain it to the image.

### 4.4. Delayed-detection arbitration

Arbitration decides whether a delayed YOLO result confirms tracking or should restart it. Compare the YOLO box with the tracking and prediction boxes saved for the same source frame. Clip boxes to the source image first so comparison uses their visible portions. IoU, intersection over union, is intersection area divided by union area and measures box overlap.

Apply these rules in code order:

| Condition | Behavior |
|---|---|
| Result belongs to an older round | Skip target arbitration. |
| Positive detection lacks a valid source-frame tracking box | Neither confirm nor reanchor; an old display box cannot substitute for an observation. |
| Box differs from both source-frame prediction and tracking boxes, and IoU is below `iou_calibrate_threshold` | Count a YOLO jump. At `jump_tolerance`, attempt catch-up reanchoring. Before that threshold, count an unconfirmed result. |
| Current tracking update failed, or IoU is below `iou_reset_threshold` | Attempt catch-up reanchoring and count an unconfirmed result. |
| IoU exceeds `iou_calibrate_threshold` | Confirm tracking and clear unconfirmed/YOLO-jump counters. Keep the current box without an extra YOLO-based Kalman correction. |
| Remaining intermediate overlap band | Clear unconfirmed/YOLO-jump counters and keep the current tracking result. |
| Detection completed without a target | Clear the YOLO-jump counter and count an unconfirmed result. |

Reanchoring initializes on the detection's source image and catches up to the current frame. Comparison data can still confirm tracking after that image is evicted, but reanchoring requires the image itself. When a jump reaches its acceptance threshold and the source image remains available, the current code clears the unconfirmed counter even if initialization fails. Without that image, it increments the counter. The low-IoU/current-tracking-failure branch increments the counter regardless of recovery success.

### 4.5. Loss, timeout, and state values

At `lost_max_count` unconfirmed results, clear initialization and boxes and set `TargetLost`. Unconfirmed results include no target, low-IoU reinitialization, and jumps not yet accepted.

While initialized, more than `lost_max_count * yolo_interval * 3` consecutive updates without reading a detection result also clear initialization and boxes. The default product is `60`, so the 61st such update triggers timeout. Any read result resets this counter, including no-target results, missing-comparison results, and older-round results. This timeout has no result-age threshold and does not additionally set `TargetLost`.

| State | Value | Typical trigger |
|---|---|---|
| `Detecting` | 0 | Uninitialized detection branch. |
| `TargetFound` | 1 | Successful source-image initialization and catch-up. |
| `Tracking` | 2 | Initialized branch, including prediction and brief tracking failures. |
| `TargetLost` | 3 | YOLO unconfirmed counter reaches its limit. |

State values alone do not establish box validity. Repeated jumps and no-result timeout may clear internal initialization before the next update enters `Detecting`.

## 5. Kalman output and line-of-sight observations

`RotationKalmanFilter` state is `[cx, cy, w, h, vx, vy]`; measurements are `[cx, cy, w, h]`. It compensates camera rotation and revisits historical states. `predictOnly()` queries a prediction at the requested time without changing internal state or history.

Successful initialization or reanchoring resets the filter at the current frame time using the caught-up display box and sets `reset_los=true`. Ordinary tracking measurements correct it with the original floating-point box. YOLO confirmation alone performs no extra correction. Final output prediction uses `ctx.now_us` and `ctx.R_cam_now`.

`display_box` is an integer display box; `realtime_box` is an integer prediction box. `observation_box` retains double-precision prediction and clips it to the actual input image. Internal filter state is not clipped to image boundaries. The ROS node selects observation time according to its time mode. With `publish_prediction` defaulting to `false`, observations are not all extrapolated to current wall-clock time.

LOS means line of sight, the camera-to-target-center direction. `TargetObservationBuilder` constructs paired box and LOS messages in the node. For pixel center `(u,v)`, the ray in camera FLU coordinates is `(1, -(u-cx)/fx, -(v-cy)/fy)`. FLU axes point forward, left, and up.

Pitch is positive downward; yaw is positive leftward. Angles use radians, `rad`; angular rates use radians per second, `rad/s`. The estimator rotates the previous ray and filtered velocity into the current camera frame before computing direction changes. This compensates the camera's own rotation. Smoothing uses `1-exp(-dt/tau)` with `los.rate_filter_tau` defaulting to `0.12` seconds. Setting it to `0` disables smoothing.

The first sample or a gap exceeding `0.3` seconds starts with zero angular rate. An invalid box, missing/invalid rotation, or successful reanchoring clears rate history. Simulated camera calibration can validate interface behavior but cannot establish physical angular accuracy.

## 6. C++ parameter defaults

These values come from `TrackerSettings`. The ROS node, YAML, and launch settings may override them. The platform assembly layer interprets model paths.

| Parameter | Default | Current meaning |
|---|---|---|
| `yolo_engine_path` | `drone_int8.engine` | Retained YOLO model-path field. RK3576 model-path configuration selects a `.rknn` file. |
| `nanotrack_backbone_path` | `nanotrackv3_backbone_int8.engine` | Retained Backbone model-path field. |
| `nanotrack_head_path` | `nanotrack_head_int8.engine` | Retained Head model-path field. |
| `target_class_id` | `0` | Detect only this exact class. |
| `yolo_conf_threshold` | `0.4` | Confidence threshold for whole-image and tile detection. |
| `yolo_nms_threshold` | `0.4` | NMS overlap threshold. |
| `yolo_recall_relax_ratio` | `1.0` | ROI threshold multiplier; no relaxation by default. |
| `yolo_interval` | `4` | Tracking-branch request interval in valid updates. |
| `iou_reset_threshold` | `0.4` | Attempt reanchoring below this overlap. |
| `iou_calibrate_threshold` | `0.8` | Confirm tracking above this overlap; confirmation keeps the tracking box. |
| `max_position_jump` | `150.0` | Center jump-distance threshold in pixels. |
| `max_size_jump_ratio` | `2.0` | Jump-ratio threshold applied separately to width and height. |
| `max_box_area_ratio` | `0.8` | Maximum width and height ratios relative to the detection region. |
| `lost_max_count` | `5` | Consecutive unconfirmed-result limit. |
| `roi_scale_factor` | `2.0` | Tracking-stage ROI expansion factor. |
| `frame_buffer_size` | `30` | Recovery image-cache capacity in frames. |
| `jump_tolerance` | `3` | Count threshold for tracking-jump reset and YOLO-jump acceptance. |
| `enable_dcf` | `true` | Enable the grayscale DCF center; disabling it selects the center filter. |
| `dcf_max_div_factor` | `0.5` | Center-deviation threshold as a multiple of mean target side length. |
| `kf_fx`, `kf_fy`, `kf_cx0`, `kf_cy0` | All `0.0` | Focal lengths and principal point in input-image pixels; valid calibration is required at runtime. |
| `kf_r_pos`, `kf_r_size` | Both `4.0` | Position and size measurement-noise variances. |
| `kf_q_a` | `5e4` | Acceleration process-noise parameter for center motion. |
| `kf_q_size` | `1e3` | Size process-noise parameter. |
| `kf_history_len` | `60` | Main Kalman history length. |

The `dcf` substructure comes from `GrayscaleDCF::Params`: `model_sz=96`, `pad=1.0`, `sigma_factor=0.1`, `learning_rate=0.01`, `psr_thresh=5.0`, `lambda_reg=1e-4`, `fixed_template=true`, and `eps=1e-5`. By default, the template learns at initialization or reanchoring. Tracking-time updates at the configured learning rate occur only when fixed-template mode is disabled.

NanoTrack constants come from `nano_tracker.h`: `score_size=15`, `min_wh=5`, `template_size=127`, `instance_size=255`, `window_influence=0.455`, `track_lr=0.348`, `track_penalty_k=0.138`, and `stride=16`. They are not adjustable `TrackerSettings` parameters.

## 7. Source and verification entry points

- [hybrid_tracker.cpp](../../src/hybrid_tracker/hybrid_tracker.cpp): branches, catch-up, jumps, arbitration, and output.
- [yolo_detector.cpp](../../src/hybrid_tracker/yolo_detector.cpp): request replacement, result snapshots, detection cascade, and postprocessing.
- [nano_tracker.cpp](../../src/hybrid_tracker/nano_tracker.cpp) and [dcf_tracker.cpp](../../src/hybrid_tracker/dcf_tracker.cpp): neural-network tracking and center fusion.
- [rotation_kf.cpp](../../src/hybrid_tracker/rotation_kf.cpp) and [target_los.h](../../src/hybrid_tracker/target_los.h): rotation-compensated filtering and LOS estimation.
- [target_observation.h](../../src/target_observation.h) and [drone_tracker_node.cpp](../../src/drone_tracker_node.cpp): paired messages, time modes, and publication.

Algorithm tests live in [src/hybrid_tracker/tests/](../../src/hybrid_tracker/tests/). They cover model/preprocessing contracts, recovery, diagnostic arbitration, and LOS behavior. See [AGENTS.md](../../AGENTS.md) for build and hardware-verification boundaries. This document describes code behavior; it does not establish real-model, camera, HDMI, sustained-load, or flight acceptance.
