# Proposal

## Why

Saved flight video can omit the image that first finds the target, preventing reviewers from reconstructing how tracking began. The November 4, 2026 data confirms this gap because detection starts while the video recorder is still preparing.

## What Changes

- Align initial detection with actual video acceptance for RK3576 SC233HGS continuous preview when `recording.enabled=true`.
- Carry a recording receipt with each camera frame. A receipt is a small immutable record of whether the current video session accepted that exact frame.
- Apply a startup admission rule: only accepted frames from the current session may enter the algorithm until the first successful NanoTrack initialization. YOLO detects a target box; NanoTrack initializes its local appearance tracker from the corresponding historical image. A positive YOLO result alone does not end the rule.
- Keep recording accepted frames immediately, including frames with no target. Preparation means making the recording worker ready; startup extends from ON through successful tracker initialization. The admission rule, also called the startup filter, controls algorithm inputs and does not pause video recording.
- Permit startup delay and omission of recording-rejected algorithm candidates during this initial interval. Preserve existing steady-state scheduling after initialization.
- Preserve responsive capture, HDMI preview during preparation, telemetry, and OFF. Recording failure or unavailability releases the filter and permits tracking with an explicit coverage-failure result.
- Retain request and frame identities, separate startup omission counts, and saved-video coverage evidence. Successful completion must distinguish queue acceptance from frames actually written.
- Add deterministic startup/race tests and bounded production-board verification.

Example: in `12-06-28`, round 2 used camera frame 8884 at 12:09:04.936062 (UTC+08:00) for both the first YOLO request and tracker initialization. Video begins at 8892 at 12:09:05.069415, leaving eight frames and 133.353 ms outside the recording. After this change, detection in the equivalent controlled sequence starts with an accepted frame from 8892 onward. If its detector result is negative, the filter continues checking subsequent frames until initialization succeeds.

## Capabilities

### New Capabilities

None. Extend the existing frame, recording, and continuous-preview capabilities.

### Modified Capabilities

- `generic-frame`: optional immutable recording receipts that preserve frame ownership, timestamps, concrete backend compatibility, and operation with diagnostics disabled.
- `rk3576-camera-recording`: authoritative per-frame acceptance receipts, current-request status, bounded failure handling, and coverage evidence.
- `rk3576-continuous-hdmi-stream`: initial algorithm admission through first successful initialization, cancellation/fallback rules, and exact saved-frame acceptance checks.

## Impact

- Portable interfaces: `src/frame/frame.h` and `src/platform/camera_recording.h`. New receipt access defaults to unavailable for unaffected backends.
- RK3576 capture and frame storage: `host_frame.*`, `rk3576_camera_capture_base.*`, `ar0234_camera_frame_source.cpp`, and `sc233hgs_camera_frame_source.*`.
- Video recording: `recording/shared_slots.h`, `recording/session.*`, `recording/metadata.h`, `recording/storage.cpp`, and matching inspection/validation.
- Node orchestration: `src/drone_tracker_node.*`. Startup filtering remains outside the public detection/tracking algorithm.
- Bounded optional source-observation hooks in `src/hybrid_tracker/yolo_detector.*` and `hybrid_tracker.*` retain executed detector and historical initialization identities without changing algorithm decisions.
- Tests: existing host-frame, camera, recording-session/storage, and ROS continuous-camera fixtures. Guides must explain the delay, immediate recording of accepted frames, and failure fallback.
- No model, model preprocessing, inference-runtime, ROS message, public tracker-state, CPU-role, or parameter-default change. No additional image copies, unbounded buffers, startup pre-recording, or new configuration switch.
- Jetson, AR0234, file inputs, OSD-disabled capture, ROS bag timing/policies, and tracking after the first initialization retain their existing contracts.
- This change builds on `f08399533b9f548666b52ef9e16694e4c1ce0835`. The existing topic-recording change concerns ROS bags; this change concerns original-camera video and initial algorithm admission. It must preserve that recorder's behavior.
