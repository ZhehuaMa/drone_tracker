# Tasks

All tasks below are implementation or verification work still to be performed. Creating these planning artifacts does not complete any task. Follow [design.md](design.md) for terminology, ownership, and the startup filter's scope.

## 1. Portable receipts and concrete frame compatibility

- [x] 1.1 Expose an optional immutable recording receipt through `src/frame/frame.h` and a small portable metadata header; completion means the value includes availability/disposition, stream, round, request generation, capture id, driver sequence, and capture timestamp without SDK types or include cycles.
- [x] 1.2 Preserve RK3576 concrete storage compatibility in `src/platform/rk3576/host_frame.*`; completion means receipt attachment shares the existing reference-counted pixels and returns the same supported concrete host-frame type without another image copy.
- [x] 1.3 Cover receipt immutability, default-unavailable backends, sequence wrap, and diagnostics-off identity in the existing frame fixtures; completion means deterministic checks pass with unchanged pixels and timestamps.
- [x] 1.4 Cover preprocessing compatibility and retained read-view lifetimes in the existing RK3576 host-frame/model preprocessing tests; completion means decorated frames pass concrete storage validation and remain readable after capture owners release them.
- [x] 1.5 Document optional receipt and pixel-ownership semantics in the owning frame/platform interfaces and guide; completion means the documentation explains acceptance without implying completed writing.

## 2. Recording acceptance and request generations

- [x] 2.1 Report final per-candidate disposition in `rk3576_camera_capture_base.h`, `recording/shared_slots.h`, and `recording/session.h`; completion means accepted, rejected, and cancelled outcomes belong to the receiver pinned for the complete frame visit.
- [x] 2.2 Attach receipts after deferred commit in `ar0234_camera_frame_source.cpp` and populate `CameraNv12View::capture_id` in `rk3576_camera_capture_base.cpp`; completion means the enqueued frame has final immutable acceptance with no driver-lifetime escape.
- [x] 2.3 Preserve exact capture identity through `recording/shared_slots.h` and `recording/metadata.h`; completion means new frame records carry capture id and session/request correlation while existing identity fields remain readable.
- [x] 2.4 Expose current-request pending and terminal status in `camera_recording.h` and `sc233hgs_camera_frame_source.*`; completion means ON synchronously establishes a token that cannot inherit old readiness, failure, or completion.
- [x] 2.5 Cover stage/publish/OFF ordering, attachment during a visit, queue-full rejection, and receipt handoff in `recording/session_test.cpp` and camera fixtures; completion means controlled synchronization exercises each boundary without timing-dependent sleeps.
- [x] 2.6 Cover cancelled preparation, rapid request replacement, unreclaimed-worker refusal, timeout, and continuous rejection health checks in recording-session fixtures; completion means current-request failure remains bounded and raw buffers retain their existing limits.
- [x] 2.7 Explain preparation, ordinary accepted-frame recording, and request identity in `src/platform/rk3576/recording/README.md`; completion means the guide explicitly states that startup filtering never pauses accepted video.

## 3. Exact detector and initialization evidence

- [x] 3.1 Expose optional value-only observations at actual execution start in `yolo_detector.*` and successful historical initialization in `hybrid_tracker.*`; completion means first-source identities reflect executed work and the original initialization image without changing model values, arbitration, or request replacement.
- [x] 3.2 Collect bounded first-source evidence per round in `drone_tracker_node.*`; completion means hooks hold no raw node/session pointer, retain at most two source records, and obey the documented collector/control lock order.
- [x] 3.3 Cover replaced detector submissions, historical initialization, failed initialization, and observation neutrality in detector/tracker fixtures; completion means exact source observations match existing diagnostic events while ordinary outputs and scheduling remain equivalent.
- [x] 3.4 Cover diagnostics-off collection and OFF races in ROS/recording fixtures; completion means evidence survives independently of diagnostics and late callbacks cannot populate a cancelled or newer round.
- [x] 3.5 Document executed input versus submitted input and historical initialization source in the recording guide; completion means its concrete example identifies frame 100 rather than processing frame 108.

## 4. Initial algorithm admission and responsive fallback

- [x] 4.1 Apply per-frame startup admission in `dispatchFrames()`, `takeTrackingFrame()`, and round setup in `drone_tracker_node.*`; completion means only accepted current-token frames are consumed until the first successful current-round initialization.
- [x] 4.2 Preserve preview, cancellation, and failure isolation in `drone_tracker_node.*`; completion means preparation keeps the starting preview active, OFF invalidates pending eligibility immediately, and terminal current-request failure permits live tracking without restarting the filter.
- [x] 4.3 Reproduce the November 4 ordering in `src/tests/ros_video_test.cpp`; completion means frames 8884 through 8891 are withheld during preparation and the first executed request uses 8892 or a later accepted frame.
- [x] 4.4 Cover negative detection, positive detection with failed initialization, pending-task replacement, rejected candidates, and post-initialization target loss in continuous-camera fixtures; completion means filtering ends only after successful initialization and does not reactivate for later recovery.
- [x] 4.5 Cover preparation timeout, old-worker refusal, stale receipts/status, rapid ON/OFF/ON, and OFF during preparation or inference in continuous-camera fixtures; completion means preview/telemetry progress and the existing 500 ms OFF display response remain observable.
- [x] 4.6 Cover recording disabled, AR0234, file input, OSD-disabled capture, ROS bag behavior, and default no-op observation hooks in existing regression fixtures; completion means unchanged inputs require no receipt and retain their existing lifecycle.
- [x] 4.7 Explain startup latency, the per-frame filter, immediate video recording, steady-state limits, and failure fallback in the camera/recording owning guides; completion means the terminology and examples are understandable without this conversation.

## 5. Coverage metadata and saved-frame verification

- [x] 5.1 Transfer frozen startup evidence through recording control into bounded session-end metadata in `camera_recording.h`, `sc233hgs_camera_frame_source.*`, and `recording/session.*`; completion means capture/control/observation callbacks perform no file I/O and metadata failure remains explicit.
- [x] 5.2 Write additive startup keys and distinct omission counters in `recording/metadata.h` and `recording/storage.cpp`; completion means preparation omissions, rejected inputs, replacements, recorder drops, absent initialization, cancellation, and fallback cannot be mistaken for complete coverage.
- [x] 5.3 Check exact source membership with bounded streaming reads in the matching recording inspector; completion means its separate report distinguishes accepted, metadata-matched, verified-written, unknown, and failed coverage without modifying originals or adding a replay decoder.
- [x] 5.4 Cover accepted-but-unwritten failure, missing/truncated startup fields, old recordings, cross-session identities, counter observation points, and zero-initialization rounds in storage/inspection fixtures; completion means no aggregate count or completion marker alone produces verified-written coverage.
- [x] 5.5 Document the metadata fields, matching-inspector use, and video-readback verification in the recording guide; completion means historical recordings remain explicitly unknown and examples preserve original capture times.

## 6. Integrated build and production acceptance

- [x] 6.1 Establish software integration with `./build.sh --test` on the selected RK3576 backend; completion means the receipt, camera, recording, tracker, ROS continuous-camera, and regression checks pass with recorded commands and exit statuses and no automatic backend switch.
- [x] 6.2 Produce a matching release using the existing deployment workflow; completion means node, recording worker, inspector, model references, and CPU policy identities are retained together and no system RKNN Runtime is overwritten.
- [ ] 6.3 Verify startup coverage on an identified production HZ-RK3576_WuKong Board; completion means at least 20 ON/OFF rounds and at least 60 seconds of combined tracking/recording/HDMI workload satisfy exact first-YOLO and first-initialization membership with video readback, with every individual load run at most 180 seconds.
- [x] 6.4 Retain a dated validation report with raw evidence under the repository's existing report/evidence conventions; completion means it records source/executable/model/configuration identities, actual OS/kernel/toolchain/devices, Runtime 2.3.2 and driver 0.9.8 checks, commands/exit statuses, startup latency, display/processing rates, OFF response, memory/thread/descriptor changes, and all unexecuted checks.
