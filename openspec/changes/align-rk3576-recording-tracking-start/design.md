# Design

## Context

The saved video can begin after the image that finds and initializes a target. The intended outcome is that initial algorithm inputs belong to the same video session, while camera preview remains responsive during recorder preparation. See [proposal.md](proposal.md) for the observed November 4 example.

The inspected base is `f08399533b9f548666b52ef9e16694e4c1ce0835`. In `continuousWorker()`, the node requests recording and immediately enables tracking dispatch. `Sc233hgsCameraFrameSource::recordingLoop()` attaches its receiver only after `Session::start()` finishes. The existing guide explicitly allows this preparation gap.

The platform already stages original NV12 pixels before returning the driver buffer. NV12 contains brightness and color planes. Final capture checks and the deferred recording commit precede enqueueing the grayscale tracking frame. This is the correct place to associate exact acceptance with the frame.

`isHostGrayFrame()` checks the concrete private `HostFrame` type. A generic forwarding wrapper would fail that check. `TrackerOutput` currently provides successful tracking state but not the historical source of the first initialization. Existing diagnostic events record detector execution and historical initialization identities; diagnostics can be disabled.

Terms used here:

| Term | Meaning |
|---|---|
| Preparation | Creating the recording worker and its resources, ending when its receiver can accept inputs |
| Startup | ON through the first successful tracker initialization, or cancellation/fallback |
| Startup filter, previously called gate | A rule selecting recorded algorithm inputs; it does not hold or delay video |
| Receipt | Immutable metadata reporting the recording disposition of one exact captured frame |
| Accepted | Final publication placed a candidate in the recorder's bounded input queue |
| Verified-written | Exact source identity was matched to saved frame metadata and readable video |
| Request generation | A monotonically increasing number distinguishing recording requests within a stream |

A ready receiver can still reject an individual frame. Acceptance also does not establish that encoding, writing, or synchronization has completed.

## Goals / Non-Goals

**Goals:**

- Check each initial algorithm candidate against its own current-session receipt.
- Continue checking until successful initialization, including later requests after negative detections.
- Preserve existing single-consumer capture, bounded queues, immutable frames, and failure isolation.
- Retain exact source evidence without making production correctness depend on diagnostics.

**Non-Goals:**

- Recording pre-ON preview or reconstructing missing historical pixels.
- Guaranteeing every steady-state tracking or later recovery frame is recorded.
- Changing model calculations, detection arbitration, public tracker states, ROS contracts, CPU roles, or default parameters.
- Adding threads, unbounded histories, a configuration switch, or extra pixel copies.

## Decisions

### 1. Apply per-frame admission through first initialization

**Choice.** Enable the filter only for SC233HGS continuous preview with original-camera recording enabled. Check receipts at dispatch and again before consumption. End filtering after the node accepts the first successful current-round tracking result. A positive detector result alone does not release it.

**Reason.** The first detector result may be negative. A later recording-rejected image could otherwise become the initialization source. Filtering all initial consumed frames also ensures the historical inputs used for initialization and catch-up came from accepted images.

**Rejected alternatives.** A fixed sleep depends on timing. A global ready flag cannot distinguish queued older frames or recording queue rejection. Ending filtering after the first request protects only that request. Recording preparation before service preview would violate the existing service lifecycle.

**Consequence.** Preparation adds startup latency. Recording congestion can also reduce initial algorithm input rate. After first initialization, the existing latest-pending-task and video-drop policies resume; later loss does not reactivate this filter.

Illustrative sequence:

1. ON registers a current request; capture and preview continue.
2. Frames 8884 through 8891 have no accepted receipt and do not enter the initial algorithm.
3. Recording accepts 8892; the dispatcher can supply it or a later accepted candidate.
4. YOLO, the asynchronous detector, may find nothing; eligible requests continue.
5. A result from accepted frame 8900 initializes NanoTrack, the local appearance tracker, while processing accepted frame 8908.
6. The node ends filtering after that successful current-round result.
7. Both 8892, if actually executed, and the historical initialization source 8900 can be verified in ordinary video.

This example is a controlled reproduction pattern, not a claim that the missing original images can be replayed.

### 2. Preserve the concrete host frame when attaching receipts

**Choice.** Define the portable receipt in a small frame-metadata header and expose a default-unavailable accessor in `Frame`. Public recording interfaces reuse this value type without introducing a frame/recording include cycle. Extend the existing RK3576 `HostFrame` with immutable receipt storage.

After final recording commit, a backend-private helper constructs a new `HostFrame` metadata object sharing the existing reference-counted image storage. It preserves timestamp and capture identity. The receipt-bearing frame is enqueued only after this construction; existing frame objects are never mutated after publication.

**Reason.** This preserves the concrete type accepted by `isHostGrayFrame()`, and reference-counted image sharing avoids another pixel copy.

**Rejected alternatives.** An arbitrary `Frame` wrapper fails the current backend check. Broadening that check to any readable frame weakens platform validation. A mutable receipt updated after enqueueing creates races and changes the meaning of already queued frames.

**Consequence.** One bounded metadata allocation can be added per applicable delivered frame; image storage remains shared. Add a lifetime and storage-compatibility test. Jetson, video, and unrelated frame builders use the default unavailable receipt.

Receipt fields are: explicit availability/disposition, stream id, tracking round, request generation, 64-bit stream-local capture id, driver sequence, and original capture timestamp. The capture id distinguishes driver-sequence wrap; timestamps remain evidence rather than authorization. These identities must work with diagnostics off.

### 3. Capture the actual outcome of final recording publication

**Choice.** Extend the platform-private recording sink contract with a value-only outcome for its current frame visit. `RecordingSlots::publish()` records the actual accepted/rejected/cancelled disposition. `SessionDispatcher` associates it with the receiver pinned for that visit. Attachment midway through a visit cannot authorize that frame.

The capture-side sequence is:

1. Copy tracking brightness pixels into owned storage and stage recording pixels.
2. Return the driver buffer and complete warmup/stop checks.
3. Run `DeferredSink::commit()`.
4. Read the final outcome before `finishFrame()` releases the pinned receiver.
5. Attach immutable receipt metadata to the concrete host frame.
6. Enqueue the frame and publish normal capture statistics.

Populate the already declared `CameraNv12View::capture_id`; the inspected aggregate construction currently omits it. Carry capture id through recording slot identity and additive JSON frame metadata. Keep the worker and parent built and packaged from the same revision if the shared-memory layout changes.

**Reason.** Only final publication proves acceptance. The existing final checks and ownership fence must remain authoritative.

**Rejected alternatives.** Successful staging can later be cancelled. Counter differences are aggregate evidence rather than a typed per-frame result. Waiting for disk synchronization in capture would violate fault isolation.

**Consequence.** Receipt creation uses short memory operations without encoder/storage waits. The four raw slots, 64 MiB raw-pixel limit, encoded buffer limit, and image contents remain intact.

### 4. Give pending and failed requests their own identity

**Choice.** Register an immutable stream/round/generation token synchronously in `requestRecording()`. Immediately expose pending status for that token, separate from the previous `Session` snapshot. Preparation completion and terminal outcomes publish against that token. Return the token to the caller or provide an equally synchronous value-only request snapshot through `CameraRecordingControl`.

The node keeps per-round filter state under its existing control mutex. Preparation/readiness alone cannot open input admission. Accepted receipts must match all current request identifiers.

**Reason.** The old session can still report finished or ready while the asynchronous controller has not begun a new request.

**Rejected alternative.** Reusing phase strings without request identity can release or authorize the wrong round.

**Consequence.** No configuration parameter is needed. A new request refused because of unreclaimed resources reports explicit current-request unavailability, allowing the existing tracking fallback.

Retain the existing three-second preparation and close budgets. Do not append another node-level three-second readiness wait. Keep stream warmup and qualified-input health timing separate. If qualified recording inputs are continuously rejected, the existing five-second no-persistence-progress rule still produces a terminal failure.

### 5. Keep control cancellation and display behavior independent

**Choice.** Before the first eligible algorithm task, remain in the existing starting presentation and submit fresh unannotated preview. After algorithm processing begins, preserve the current same-frame pairing rule. A rejected startup candidate does not produce ROS observations or debug images and is not displayed with a previous frame's box.

ON resets the round filter. Under the control mutex, OFF invalidates the round/token, closes evidence collection, discards pending work, and requests recording stop. Late readiness and inference outputs retain their existing rejection rules. A terminal current-request recording failure changes the filter to fallback for the rest of that round; do not restart it opportunistically.

**Reason.** Delaying the algorithm need not stop preview. Recording failure must continue to leave healthy tracking usable.

**Rejected alternatives.** Waiting inside ON/OFF callbacks would delay telemetry and cancellation. Rewinding queued preparation frames after fallback would reintroduce stale inputs. Showing old boxes on fresh preview would break display pairing.

**Consequence.** Published states remain `Detecting=0`, `TargetFound=1`, `Tracking=2`, `TargetLost=3`. The existing 500 ms OFF display boundary remains. Filtering does not create a fifth tracker state. After pairing begins, display cadence reflects admitted/consumed inputs; this change does not promise full-rate preview during every later algorithm stall.

### 6. Retain bounded observational source evidence

**Choice.** Add optional value-only observation hooks at the existing detector-execution-start and successful historical-initialization points in `yolo_detector.*` and `hybrid_tracker.*`. Hooks report source capture identity, timestamp, request/update identity, and algorithm generation. They do not return decisions or change scheduling.

The node installs a per-round collector with space for the first executed detector input and first successful initialization source. Collection is independent of diagnostic mode. An initialization event uses the historical frame, not the later frame that consumes its result. Retain the existing diagnostic events for cross-checking.

The collector owns only fixed-size values and a short mutex. Callbacks capture the collector, never a raw node or recording-session pointer. OFF freezes it under a defined lock order: node control mutex, then collector mutex; observation hooks take only the collector mutex. Hooks arriving after freezing are ignored for cancelled coverage. Cleanup waits for inference users before releasing their hook state.

Pass the frozen value snapshot to recording control before closing acceptance. The recording executor serializes it into the existing bounded end-of-session summary path, and the worker writes additive startup evidence in the session's metadata. No callback writes files. Failed metadata persistence is reported; a directory failure retains the summary in node logs. Cancellation with no detector execution records an absent source rather than inventing one.

**Reason.** Existing `TrackerOutput` cannot reveal the historical initialization image. The first consumed node frame is also not necessarily the first request actually executed, because pending requests can be replaced.

**Rejected alternatives.** Inferring execution from the first submitted or consumed task can identify the wrong frame. Requiring basic diagnostics would silently change production settings. Retaining every startup frame or request would permit unbounded growth when no target appears.

**Consequence.** Shared algorithm files gain optional observation only; admission policy stays in node orchestration. Use default no-op hooks for other callers and test that observation leaves model values, arbitration, and request behavior unchanged.

Persist stream/round/request keys, known source keys, bounded transition times, omission counters, and a reasoned coverage outcome. Distinguish capture-side preparation omissions from dispatch-side candidates withheld and from pending-task replacements; counts with different observation points must not be summed as if disjoint.

The matching inspector checks exact source membership in frame sidecars with bounded streaming reads and writes a separate verification result. Video readback then establishes readable corresponding frames. Queue acceptance remains `accepted` until these checks establish `verified-written`; a normal video completion marker alone does not establish coverage. Missing keys in historical sessions mean unknown coverage. No new replay decoder or conversion tool is part of this change.

### 7. Verify the startup sequence using controlled boundaries

**Choice.** Extend current host-frame, camera, recording-session/storage, and ROS continuous-camera tests. Add synchronization barriers in fixtures for prepare, attach, stage, publish, task replacement, detector execution, and OFF. Build through `./build.sh --test`.

**Reason.** The defect depends on ordering across capture, recording, dispatch, and asynchronous inference.

**Rejected alternative.** Sleeping for the observed 133 ms can pass without testing the critical ordering.

**Consequence.** Cover both detection misses and initialization failures, exact historical sources, receipt rejection, diagnostics off, request-generation changes, unavailable worker fallback, and unaffected inputs. A synthetic supplier can use the November 4 identities 8884 through 8892 without claiming to reconstruct their missing pixels.

Production verification uses an identified HZ-RK3576_WuKong Board, existing models, system RKNN Runtime 2.3.2, and driver 0.9.8. Cover at least 20 ON/OFF rounds plus at least 60 seconds of combined workload. Every individual load run stays at or below 180 seconds. Retain commands, exit codes, environment/model/executable identities, raw logs, frame sidecars, video readback, startup latency, actual display/processing rates, OFF response, and resource changes outside disposable build caches.

## Risks / Trade-offs

- Initial target acquisition becomes dependent on recorder preparation and temporary input rejection. Measure the added latency; release filtering on explicit terminal failure using existing bounded preparation/health paths.
- Accepted video can fail before writing. Report failed coverage and verify exact saved-frame membership; never equate acceptance with persistence.
- Frame decoration can break concrete backend checks. Preserve `HostFrame` type and reference-counted pixels; test preprocessing and view lifetimes.
- Cross-round status or callbacks can contaminate evidence. Match tokens, freeze on OFF, and keep hook state alive until inference users stop.
- Shared-memory identity changes can mismatch binaries. Package and deploy parent and video worker together; do not mix revisions.
- Evidence loss can conceal failure. Preserve bounded summaries, explicit unknown/failed outcomes, and separately retained validation results.
- The November 4 deployment's executable hash is known from collection metadata, but its exact source revision is not established. Historical recordings demonstrate the startup sequence; they are not acceptance evidence for this proposed implementation.

## Migration Plan

1. Implement the tasks after review of these planning artifacts.
2. Run deterministic checks and the supported RK3576 build. Validate compatibility of unchanged inputs and ordinary topic recording.
3. Package the matching node, video worker, and inspector. Follow the existing production deployment guide and recheck actual board/runtime/camera identities.
4. Execute the bounded target checks and retain exact source-to-video verification. Keep production `visualized_result=false`.
5. Roll back by restoring the previous complete release and restarting the service. Retain both releases' recordings and matching inspectors. Disabling `recording.enabled` restores the existing tracking-only startup path but does not provide startup video coverage.

Planning creates no source changes, deployment, or acceptance result. The delta specifications are the normative contract; all verification tasks remain open until executed.
