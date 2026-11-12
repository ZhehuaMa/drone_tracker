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

### 2.2. One-call detection scheduling and regional evidence

A submitted request identifies an admitted source image. A stage job executes exactly one ROI, whole-image, or tile inference. A discovery plan retains one fixed source image and its remaining region positions. Regions are searched with the existing confidence filtering and NMS. ROI means region of interest. Its confidence threshold remains `yolo_conf_threshold * yolo_recall_relax_ratio`; whole-image and tile stages use the full configured threshold.

One dedicated worker interleaves targeted checks (`T`) with discovery stages (`D`). When both stay eligible, it serves at most two T calls, then one D stage. The shorter image side is split in two, with adaptive overlapping tiles along the longer side and overlap 0.2. New images replace only the shared unstarted mailbox. They do not restart the fixed-image plan. For example, D10 continues on F10 while F12 replaces pending F11. This retains later-tile coverage without a per-image cascade queue.

The bounds are one active device call, one pending image, one discovery plan, one unread result, and one locally taken result. Taking a result releases capacity before arbitration. A full unread slot prevents another inference. The active call finishes before its input session is released or another call reuses SDK storage. OFF cancels waiting work, wakes waits, joins active device access, and clears old-round controls/results.

Searching pauses its plan at a positive seed candidate. `SeedAccepted` closes that plan before expensive initialization and permits a submitted later-image check during preparation. For example, D10 seeds B before catch-up; D20 may finish concurrently, but must later compare with B's independent T20. No camera-current image or renewed clock anchor substitutes for an unsubmitted image. Partial empty stages continue without waiting for full tracker arbitration. A targeted miss can occupy the single plan slot as a same-image fallback; later-image misses cannot add more plans.

Regional results retain all surviving configured-class candidates and exact region/job/source provenance. A unique same-image match can confirm immediately. An unrelated positive or partial miss is neutral and search continues. Multiple qualifying matches close as ambiguity. An adverse no-target/disagreement verdict requires completed coverage on one source image and usable comparisons. A newer same-attempt agreement supersedes older discovery; old adverse completion cannot override it. Cancelling a plan resets coverage and rotates its first tile on restart.

Postprocessing reads only the class score selected by `target_class_id`. It restores letterbox coordinates, filters dimensions, and applies NMS. Letterboxing resizes with preserved aspect ratio and padding. NMS, or non-maximum suppression, removes highly overlapping candidate boxes. `max_box_area_ratio` separately limits candidate width and height relative to the current detection region; it does not limit area directly.

## 3. NanoTrack and center fusion

`NanoTracker` uses a `127×127` template and a `255×255` search input. Classification output has shape `[1,2,15,15]`; location output has shape `[1,4,15,15]`. Postprocessing combines scale and aspect-ratio penalties, a Hann window that favors positions near the search center, and size interpolation.

There is no `tracking_conf_threshold` parameter or fixed `0.8` score threshold controlling updates to the main pipeline. Failed search preprocessing, inference, or output-contract validation returns a negative score. The fusion tracker treats that frame as failed. Diagnostics retain the raw score.

`NanoDcfTracker` keeps NanoTrack width and height and selects the center as follows:

- `enable_dcf=true`: use the grayscale DCF center. PSR, the peak strength relative to background response, indicates confidence in this center. If PSR is below its threshold or the DCF center differs too much from NanoTrack, use the NanoTrack center and retrain the DCF template. An unreadable frame also falls back to the NanoTrack center.
- `enable_dcf=false`: smooth the NanoTrack center with `CenterKalmanFilter`. If its prediction differs too much from NanoTrack, use the NanoTrack center and reset the center filter.

Both paths use center-deviation threshold `dcf_max_div_factor * (width + height) / 2`. The center filter is independent of the main `RotationKalmanFilter`. Disabling DCF therefore still applies center smoothing.

## 4. Acquisition, verification, and recovery

An initialized model is separate from permission to publish valid observations. `OutputValidity` stores values under a short mutex. The tracking worker owns models, images, and local filter state. The ROS expiry callback only changes validity values; it never resets a model.

The internal phases are `Searching`, `PreparingProvisional` (initialization and catch-up), `VerifyingProvisional`, and `VerifiedTracking`. A track instance is one target template and its state across images. An acquisition attempt is one seed, preparation, and later-image verification. A recovery episode begins when a previously verified instance loses trust and can contain several attempts.

### 4.1. Input and coordinates

Frames and components must be compatible. Source timestamps must increase strictly. Invalid or repeated input returns `consumed=false` before detector interaction; independent expiry still runs. The bounded image cache retains at most `frame_buffer_size` images.

`predictOnly()` returns center format `(cx,cy,w,h)`. `filterRectangle()` converts it once to `(cx-w/2,cy-h/2,w,h)` for ROI generation and retained source metadata. For example, `(100,50,40,20)` becomes `(80,40,40,20)`. Its unconstrained doubled ROI is `(60,30,80,40)`, centered at `(100,50)`. Fractional values survive until display conversion. Minimum ROI size and image clipping are applied afterward.

### 4.2. First seed and independent second evidence

1. On F1, request detection D1. During a later update, save one completed-result snapshot, then submit the current image F2 before processing D1.
2. While searching with no active replacement, an eligible positive D1 with original F1 pixels starts initialization immediately. Its original expired-attempt tag does not reject seed use. Send the short `SeedAccepted` control before initialization or replay. D1 may be stale for confirmation; this exception permits only provisional work. It cannot replace an active instance or change its counter.
3. Initialize on F1 and replay subsequent buffered images in source order. Every preparation, inference, and geometry step must succeed. The first failed intermediate step rejects the attempt.
4. Retain each replay-produced measurement, such as T2 on F2, with round and attempt identity. These records contain values and immutable clock anchors, not image ownership. Preserve evidence required by outstanding detector requests even after source pixels leave the cache.
5. Consume D2 in a later normal update. F2 must be strictly later than seed F1. Join D2 to this attempt's T2 before any detector correction. D2 may have been submitted before initialization and may finish during it. An old instance's submitted box or the newest image cannot substitute for T2.
6. A unique fresh overlap match verifies the attempt. Initialize the trusted motion filter at the current accepted provisional measurement, clearing inherited velocity and LOS history. Only then may output be valid, subject to publication admission and expiry.

Initialization, catch-up, verification, and confirmation have different meanings. Confirmation renews a verified instance using fresh same-image evidence; it does not reinitialize or reset LOS rates. Original request metadata remains unchanged when replay evidence is used. Consumed results cannot be reused against a new attempt.

### 4.3. Association and structural failures

Clip candidate boxes and the source tracking measurement to that source image. IoU (intersection over union) is intersecting area divided by combined area. Exactly one candidate at or above `iou_reset_threshold` is agreement. Zero matches is neutral while coverage is partial and becomes disagreement only after complete applicable coverage; multiple matches close as ambiguity. `iou_calibrate_threshold` describes strong agreement diagnostically and adds no mandatory gate.

For example, candidate A scores 0.88 and uniquely overlaps T2, while unrelated B scores 0.94 elsewhere. A can verify T2; B's higher score cannot replace A. An unrelated first regional positive does not end confirmation search. Unsearched regions do not establish target absence.

Missing same-image context, stale evidence, duplicate results, and irrelevant old-attempt results are neutral. They do not confirm or increment the unconfirmed count. Predictions and held display boxes are not genuine tracking measurements. Metadata-only confirmation can succeed after image eviction; initialization still requires original pixels.

Appearance-score and motion validators return allow with `not_evaluated`. Weak positive NanoTrack scores and large displacements do not independently reject candidates. Both legacy jump counters stay zero. `max_position_jump`, `max_size_jump_ratio`, and `jump_tolerance` remain accepted but inactive. Failed execution, non-finite geometry, nonpositive dimensions, and invalid identity or clock relations remain structural failures.

### 4.4. Revocation, attempt budgets, and cleanup

The first eligible adverse detector result, tracking failure, invalid geometry, or expired confirmation removes valid-output permission. Revocation atomically registers a pending loss event. It does not wait for five results. Recovery starts immediately: an eligible adverse positive may seed a new provisional attempt from its original image; otherwise searching continues. Provisional and rejected measurements never update trusted motion state.

Each attempt starts with zero YOLO unconfirmed count. Distinct usable disagreements and completed no-target searches count against the active instance. At `lost_max_count`, abandon a provisional attempt and search again. Candidate timeout independently bounds elapsed seed-acceptance time. Neither count reset nor initialization progress renews confirmation permission.

Example: verified A disagrees on its first usable detector result. A loses permission immediately. That result can initialize B with count zero. If B receives five usable disagreements, B is abandoned; result five does not mark the beginning of A's recovery episode.

The no-result frame watchdog is inactive. Updates 15/16 at interval 1 and 60/61 at interval 4 cannot discard provisional work before its elapsed candidate deadline. Delayed replies alone do not clear boxes or comparison evidence. At the candidate deadline, the worker abandons the attempt at its first servicing opportunity. Confirmation expiry, distinct usable adverse-result exhaustion, genuine execution/geometry failure, and service shutdown retain separate responsibilities. There is no new detector-health timeout or model-restart policy. Later cleanup does not emit a duplicate loss.

For example, A expires around F35 and undisposed D32 arrives while processing F42. With original F32 pixels, a valid clock/source identity, and no active replacement, D32 may seed B. B starts at zero and replays F33 through F42. Fresh D41 can verify only against B's independent T41, even if submitted before B initialized. A's request-attempt identity and original anchor remain unchanged. D32 alone cannot revive A or verify B.

| Public state | Value | Meaning |
|---|---|---|
| Detecting | 0 | Searching or provisional acquisition |
| TargetFound | 1 | Fresh later-image verification succeeds |
| Tracking | 2 | Next normal verified update |
| TargetLost | 3 | Ordered publication attempt for revoked permission |

Local revocation can precede the TargetLost publication attempt when publication is blocked. That attempt must finish before later recovery states and observation pairs.

### 4.5. Separate evidence and observation clocks

`confirmation_timeout_ms=100`, `candidate_timeout_ms=1000`, and `expiry_check_interval_ms=10` are positive integer startup-only settings. The expiry interval cannot exceed the confirmation lifetime. These are engineering defaults, not validated flight limits.

`capture-monotonic-v1` maps production and independently timed regression capture age into a monotonic execution clock. An image already 80 ms old at admission plus 30 ms inference is 110 ms old. It cannot confirm with a 100 ms lifetime. Future anchors, invalid clock relations, and checked-arithmetic overflow withhold permission.

`standalone-admission-v1` is synthetic. Preserve each source timestamp for frame order, filtering, and observation reference. Decode, copy, and complete the replay wait first. Immediately before tracker admission, sample one immutable monotonic anchor for that image. Initialization, inference, catch-up, and later stalls consume its lifetime. Result arrival and newer images cannot renew old anchors.

At 50 FPS, F1 can have source time 1000 ms and admission 5000 ms, with its update finishing at 5120 ms. After the next 20 ms wait, F2 has source time 1020 ms and admission 5140 ms. At 5170 ms, F1 is 170 ms old and F2 is 30 ms old. F2 expires exactly at 5240 ms under the 100 ms lifetime, even if its result arrives then. Standalone decoding and waiting before admission are outside synthetic evidence age. Slowed standalone replay establishes neither production-camera timing nor camera-rotation acceptance.

Searching and provisional updates offer a YOLO request on every normally processed image. Verified tracking uses configurable `yolo_interval`. The RK3576 camera/video and replay templates start at 1; other positive intervals remain supported. Interval 1 offers submissions, not one completed detection per captured image. Replay FPS is unchanged.


## 5. Kalman output and line-of-sight observations

`RotationKalmanFilter` state is `[cx, cy, w, h, vx, vy]`; measurements are `[cx, cy, w, h]`. It compensates camera rotation and revisits historical states. `predictOnly()` queries a prediction at the requested time without changing internal state or history.

Successful later-image verification resets the filter at the current frame time using the caught-up display box and sets `reset_los=true`. Ordinary tracking measurements correct it with the original floating-point box. YOLO confirmation alone performs no extra correction. Final output prediction uses `ctx.now_us` and `ctx.R_cam_now`.

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
| `yolo_interval` | `4` | Verified-track request interval; RK3576 templates start at 1. |
| `confirmation_timeout_ms` | `100` | Source-evidence lifetime; startup-only. |
| `candidate_timeout_ms` | `1000` | Provisional seed-acceptance timeout; startup-only. |
| `expiry_check_interval_ms` | `10` | Independent RK3576 expiry servicing interval; startup-only. |
| `iou_reset_threshold` | `0.4` | Minimum unique same-image overlap for verification and confirmation. |
| `iou_calibrate_threshold` | `0.8` | Diagnostic strong-agreement threshold. |
| `max_position_jump` | `150.0` | Accepted legacy setting; inactive. |
| `max_size_jump_ratio` | `2.0` | Accepted legacy setting; inactive. |
| `max_box_area_ratio` | `0.8` | Maximum width and height ratios relative to the detection region. |
| `lost_max_count` | `5` | Distinct usable adverse-plan budget for a provisional attempt; no frame-count cleanup authority. |
| `roi_scale_factor` | `2.0` | Tracking-stage ROI expansion factor. |
| `frame_buffer_size` | `30` | Recovery image-cache capacity in frames. |
| `jump_tolerance` | `3` | Accepted legacy setting; both jump counters inactive. |
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
- [yolo_detector.cpp](../../src/hybrid_tracker/yolo_detector.cpp): request replacement, one-call jobs, fixed-image plans, result capacity, and postprocessing.
- [nano_tracker.cpp](../../src/hybrid_tracker/nano_tracker.cpp) and [dcf_tracker.cpp](../../src/hybrid_tracker/dcf_tracker.cpp): neural-network tracking and center fusion.
- [rotation_kf.cpp](../../src/hybrid_tracker/rotation_kf.cpp) and [target_los.h](../../src/hybrid_tracker/target_los.h): rotation-compensated filtering and LOS estimation.
- [target_observation.h](../../src/target_observation.h) and [drone_tracker_node.cpp](../../src/drone_tracker_node.cpp): paired messages, time modes, and publication.

Algorithm tests live in [src/hybrid_tracker/tests/](../../src/hybrid_tracker/tests/). They cover model/preprocessing contracts, recovery, diagnostic arbitration, and LOS behavior. See [AGENTS.md](../../AGENTS.md) for build and hardware-verification boundaries. This document describes code behavior; it does not establish real-model, camera, HDMI, sustained-load, or flight acceptance.

The evaluated targeted hint retains the successful region's size/aspect and recenters it at the current genuine measurement. It retains producing job/source and attempt provenance, supplies no permission, and resets on abandonment. The November 12 formal-profile exact-pixel comparison returned positives in 1/12 current-320 regions, 1/12 exact prior regions, and 2/12 recentered regions. Four initial samples were all negative. This limited evidence does not establish accuracy or a performance improvement; scheduler-only and combined board measurements retain separate identities.
