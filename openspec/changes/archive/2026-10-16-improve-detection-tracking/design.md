# Design

## Context

A delayed detector result can cause tracking to clear its output because the original image and its comparison boxes have been evicted together. This design preserves the boxes needed to judge the original detection, while keeping image-based recovery conditional on actual pixel availability. See proposal.md for motivation and the recorded example.

The inspected source is commit `cb977c204615ade46a62d198d821ed93de449108` in the remote worktree. `BufferedFrame` in `hybrid_tracker.cpp` currently combines a shared image reference with boxes. The tracker retains 30 processed frames by default. This history advances with processed tracker inputs, not every camera capture. Positive results fall back to a current display box when the source entry is absent.

`YoloV8Detector` owns one executing operation, one pending input, and one completed-result slot. Its `isBusy()` reports pending occupancy; it does not mean that inference is executing. `request()` rejects pending occupancy and unread results. The worker moves pending input under its mutex before executing outside the lock.

The watchdog is the software counter for tracking updates without a newly read YOLO result. It is separate from the unconfirmed-result counter, which counts detector evidence that does not confirm the tracked target. Any result read currently resets the watchdog before source lookup. The user explicitly requires all of that watchdog behavior to remain unchanged.

Relevant existing specifications protect immutable shared frames, timestamps, round cleanup, and diagnostic neutrality. The normal-operation scheduling changes here are intentional algorithm changes, not side effects of cleanup or instrumentation. This change does not relax those resource or diagnostic contracts.

## Goals / Non-Goals

**Goals:**

- Make historical comparison independent of pixel retention.
- Keep missing evidence distinct from negative detector evidence.
- Bound pending work while offering a recent image at each existing submission opportunity.
- Make every changed decision reconstructible in raw events and regression reports.

**Non-Goals:**

- Change the watchdog, add an elapsed-time guard, or require usable confirmation to reset it.
- Add a maximum detection-age limit. The user selected context-based handling and deferred such a limit.
- Increase image history, accelerate inference, change model inputs, alter preprocessing, or parallelize recovery.
- Change ROS topics, state values, submission intervals, confidence or overlap thresholds.
- Repair unrelated Kalman coordinate conventions, change the acceptance rule for partially failed historical replay, or redesign every target-loss state transition.

## Decisions

### 1. Carry value-only source metadata with the request and result

**Choice:** Define a platform-independent source-comparison record owned by the request lifecycle. It contains round identity, tracker frame/update identities, original capture time, dimensions, source tracker box, source prediction, and explicit validity flags. Copy or move it into the completed result. An outstanding request is accepted but still pending, executing, or awaiting result disposition.

Use a business-owned round generation that is valid even with diagnostics disabled; diagnostic identifiers alone must not control arbitration. Advance or isolate that generation at the existing reset/round boundary.

Finalize the source-frame NanoTrack observation after its update and before offering that frame to the detector. A prior display box is not a valid observation merely because it exists. Preserve the source-frame prediction separately. During Detecting, an absent tracker observation remains explicitly absent. Later arbitration must not rewrite an already submitted snapshot.

**Why:** The association survives image eviction and stays with the request without an unbounded global history.

**Rejected alternative:** Retain every image longer, or retain shared frame references in the metadata. Both keep much larger pixel buffers alive. A second independent history map also creates unnecessary eviction and cleanup coordination.

**Consequence:** Metadata memory is bounded by the existing request/result slots and temporary handoff values. Image ownership remains with the bounded image history and actual executing/pending consumers. Overwrite, supersession, consumption, and cleanup release the corresponding metadata. Tests must distinguish a real source observation from a held or predicted box.

### 2. Classify availability before arbitration, without an age cutoff

**Choice:** Check round identity and source comparison validity separately from source-image availability. Never fall back to a current or unrelated previous-frame box. Use the existing overlap and jump checks only with the source-frame inputs they require. Intersection-over-union (IoU) is overlapping box area divided by their combined area.

| Result/context | Arbitration | Recovery |
|---|---|---|
| Current-round positive; valid source basis; pixels present | Existing source-based thresholds | Existing recovery path is available |
| Current-round positive; valid source basis; pixels absent | Same historical comparison | Unavailable; no initialization event is invented |
| Positive; required source basis absent | Explicitly uncomparable; preserve unconfirmed count | Detecting may still initialize if its existing image requirements are met |
| Completed normal no-target result | Existing negative-result treatment | No invented positive recovery |
| Previous-round result | Reject from current-round arbitration | Never use old pixels or metadata in the new round |

For partial context, a valid source tracker box is required for the existing IoU-based tracking arbitration. A source prediction alone cannot supply that overlap or confirm tracking. When the source tracker box is valid but the source prediction is absent, retain the existing tracker-only jump comparison; when both exist, retain their existing combined jump rule. Do not compare against an unrelated box to fill either field. Detecting initialization still needs the source image, not a pre-existing tracker box.

**Why:** Image absence describes resource availability; it does not prove that YOLO and NanoTrack disagree. Age alone also has no approved rejection threshold.

**Rejected alternative:** Treat every image miss as a zero IoU, reject every delayed result, or invent a millisecond limit without evidence.

**Consequence:** Healthy tracking can accept historical agreement even after pixels are gone. Genuine low overlap still takes the existing unconfirmed increment; missing pixels alone do not. A jump that needs reinitialization cannot clear the unconfirmed counter by pretending unavailable recovery succeeded. Results with no usable comparison leave existing evidence unchanged.

### 3. Replace only pending work under the existing concurrency boundary

**Choice:** Extend request admission so newer validated input can replace an unstarted pending input. Replace the entire bundle: image, source identity, capture time, search region, and comparison record. Keep current stop, cleanup, invalid-input, and unread-result protections. Update callers so pending occupancy no longer prevents offering a replacement at an eligible submission opportunity.

The detector mutex determines whether replacement or worker pickup wins. Move the chosen request into the worker atomically. Release superseded image ownership safely without touching an executing request's resources. Emit one superseded cancellation carrying the old and successor request identities.

**Why:** Replacing only unstarted work reduces queue age without interrupting a live model call or growing a backlog.

**Rejected alternative:** Append requests to a queue, replace running input, or wait for a quiet period before starting the latest input.

**Consequence:** There remains one inference worker and at most one pending request. A short-lived clear image can be replaced by a worse image. Submission cadence remains every processed frame while Detecting and every configured interval while Tracking. A pending replacement is not a new inference. Unread-result admission remains protected; this change does not redesign the existing worker result-publication slot.

### 4. Freeze the watchdog as an independent compatibility contract

**Choice:** Preserve `m_no_detection_frames`, its branch placement, and its reset ordering. In the initialized branch, a successful `get_result()` resets it even when subsequent arbitration cannot use the result. A false return increments it. The strict limit stays `> lost_max_count * yolo_interval * 3`.

With defaults 5 and 4, update 60 does not time out; update 61 clears tracking initialization and boxes and resets the counter. The next processed frame enters Detecting. Do not add a TargetLost publication to that path. Do not introduce a separate usable-result counter, wall-clock deadline, worker restart, or Detecting watchdog.

**Why:** This is an explicit user constraint and isolates evaluation of the three requested changes.

**Rejected alternative:** Reset only on useful results or convert the counter to elapsed time.

**Consequence:** Faster tracking still shortens the effective timeout in seconds. Repeated uncomparable replies can still suppress this watchdog. Tests and reports must acknowledge this limitation rather than imply a freshness guarantee.

### 5. Preserve state meanings and isolate the changed decision points

**Choice:** Keep the existing four external states and normal transitions:

| Current state/path | Event | Outcome |
|---|---|---|
| Reset | Round starts | Detecting=0 |
| Detecting | Positive detection with source pixels; existing initialization/replay rule accepts | TargetFound=1 |
| Detecting | No target, image unavailable, or initialization fails | Remain Detecting |
| TargetFound | Next normal initialized update | Tracking=2 |
| Tracking | Accepted historical agreement or ordinary successful tracking | Remain Tracking |
| Tracking | Temporary NanoTrack failure below existing reset limits | Retain existing prediction/tracking behavior |
| Tracking | Existing unconfirmed counter reaches its limit | Clear boxes and emit TargetLost=3 |
| TargetLost | Next input finds tracker uninitialized | Detecting |
| Tracking | Persistent NanoTrack jump or watchdog expiration | Existing reset/early-return behavior; detection resumes without a new TargetLost rule |

The modified branches concern unavailable historical operands and unavailable recovery only. Existing overlap bands, no-target handling, initialization-failure streak handling, and partial-replay acceptance stay in place when their operands exist. Historical replay means updating NanoTrack through stored images after initialization on an older source frame.

**Why:** Correcting one arbitration error does not require a broader external state-machine redesign.

**Rejected alternative:** Add a new stale-result state or force every reset through TargetLost.

**Consequence:** Some existing reset paths still do not publish TargetLost. State values alone do not prove that a fresh detector confirmation was received.

### 6. Extend evidence without changing established metric meanings

**Choice:** Add explicit supersession and context-disposition evidence in `src/diagnostics/`, using append-only enum extensions and documented payloads where the existing 256-byte record permits them. Do not renumber old events or reasons. Keep image-history lookup evidence and add independent metadata-availability evidence. Include the actual source comparison boxes, counter changes, and recovery-unavailable disposition.

Update binary readers, inspectors, regression collection, metric definitions, and tests together. Version the diagnostic interpretation/metric additions and reject unsupported encodings explicitly. Historical records remain readable; absent new fields remain unavailable. Use exact identities, not nearest timestamps. No new logging thread, inference, pixel copy, or blocking storage call is introduced for diagnostics.

**Why:** Existing collection requires every accepted request to finish execution or cancel. Pending replacement must reconcile instead of looking like missing evidence.

**Rejected alternative:** Count superseded requests as no-target results or silently reinterpret every old HistoryMiss as a metadata miss.

**Consequence:** Reference and candidate share unchanged definitions for established metrics. New fields can be unavailable in the old build. Their absence does not erase a valid old image-history miss.

## Processing Sequence

1. Validate a selected frame and keep its original capture identity and timestamp. Maintain the existing bounded image history.
2. If initialized, run NanoTrack and obtain the source-frame observation and prediction with correct validity.
3. At the existing submission opportunity, offer the complete source snapshot and image. Replace pending input only if admission permits it.
4. Read detector output using the existing tracking-branch placement. Apply the unchanged watchdog reset or no-result increment at that point.
5. Classify the result's round, metadata, and image availability. Perform only supported historical comparisons.
6. Continue current tracking on accepted agreement. Perform existing synchronous recovery only when source images are available. Keep valid negative evidence distinct from missing context.
7. Apply existing loss limits and state transitions, finalize ordinary output, and release disposed request metadata. Record actual publication timing without retiming old results.

Example: A executes while B waits. Eligible C replaces B and B receives a superseded terminal record. A's result arrives after its source pixels have been evicted, but its value-only metadata remains. Agreement can preserve tracking without initializing from a wrong image. The watchdog resets because A's result was read, exactly as before.

## Risks / Trade-offs

- [Old agreement can prolong a wrong track] Preserve original times and expose age. The absence of an age cutoff and the unchanged watchdog are explicit limitations.
- [A useful queued frame is replaced by an occluded or blurred frame] Keep the executed-input identity and supersession chain; evaluate controlled brief-visibility cases.
- [Metadata accidentally holds images or grows over time] Use value-only request-owned records, weak-reference lifetime tests, and long replacement/round cycles with memory observations.
- [Source snapshots accidentally contain previous-frame boxes] Finalize only genuine current-frame observations and test asymmetric moving boxes with deterministic injected results.
- [Replacement races with pickup or OFF] Use the existing mutex and cleanup wait; test both interleavings with controlled barriers rather than relying on timing sleeps.
- [Recovery still blocks publication] Report actual replay steps and update/publication gaps. Bounded or asynchronous recovery is outside scope.
- [Higher valid-box ratio hides incorrect tracking] Use controlled known-outcome tests for loss behavior. The fixed unannotated fixture provides continuity evidence, not accuracy.

## Migration Plan

1. Implement the scoped production and diagnostic changes with deterministic tests. Keep configuration and model files unchanged.
2. Run the remote worktree's supported build/test entry point, `./build.sh --test`, in its documented matching SDK image. Container results establish build/test behavior, not NPU timing.
3. Create isolated reference/candidate artifacts with source and executable hashes. Use the same production board, fixture, models, parameters, Runtime 2.3.2, driver 0.9.8, scheduling controls, and diagnostic mode.
4. Obtain at least three qualified runs per version under existing deadlines and supply-lateness rules. Retain rejected runs and reasons. Recompute full-clip and core-window metrics from raw evidence.
5. Review continuity, delay, supersession, recovery cost, memory, and deterministic loss cases before any separate deployment decision. Do not claim overall improvement without defined acceptance rules.
6. Roll back by restoring the previous executable and matching diagnostic tools. Keep all prior evidence immutable; no model, system Runtime, or production configuration migration is needed.

Planning creates documents only. The implementation and all hardware evaluation remain unchecked tasks for a later apply request.

## Evidence References

Historical evidence is available on mzh in the main checkout's ignored directory `/home/mzh/nickluo/drone_tracker/.docker-artifacts/tracking-regression/e555-before-after-20261014/board-evidence/`. The comparison report and candidate run-002 events explain request 62; candidate run-003 explains a synchronous recovery with 19 replay steps. These are migrated historical measurements, not validation of this proposed change or of the remote host's timing.
