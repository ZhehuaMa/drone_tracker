# Design

## Context

A local tracker can follow background while its observations remain marked valid. This design withholds uncertain output immediately and lets acquisition or recovery continue internally. See [proposal.md](proposal.md) for the recorded incident and its limits.

The inspected baseline is `e17c43ebe9326379d5c070cff91488282471716d`, initially checked out on `codex/tracking-output-validity` from `upstream/rk3576`. The relevant existing behavior is:

- `SourceComparison` in `src/hybrid_tracker/yolo_detector.h` retains source-image boxes and identities without owning pixels. Comparison can survive image eviction; reinitialization still requires the original image.
- The detector uses one worker, one replaceable pending input, and a protected unread result. A normal update captures source evidence, takes one completed-result snapshot, submits or skips current input, and then arbitrates that snapshot. Snapshot means the saved result owned by this update, not a second model execution.
- `catchupTracker()` initializes on the detection's image and replays subsequent buffered images. It currently accepts partial replay failure and does not retain every replayed box for later verification.
- Current publication permission is effectively `m_tracker_initialized && m_has_display_box`. NanoTrack execution failure and detector disagreement can therefore leave prediction output marked valid until counters cause cleanup.
- RK3576 publishes floating boxes and line-of-sight (LOS) direction/rate messages through `TargetObservationBuilder`. Valid samples use capture time by default. Optional prediction selects an eligible pose time rather than relabeling an old pose as node-now.
- The filter returns center coordinates, but the prediction rectangle used for preferred search regions and comparisons currently treats those coordinates as a top-left corner.

No implementation is included in this planning change. Main specifications currently preserve several legacy behaviors; the deltas explicitly replace them rather than presenting the new policy as an existing contract.

## Goals / Non-Goals

**Goals:**

- Keep target-template initialization separate from authorization to publish valid output.
- Verify initial acquisition and recovery using two distinct detection images and independent same-image tracking evidence.
- Bound output permission using source capture age, including while input or inference stalls.
- Retain the current asynchronous scheduling and model/platform boundaries.
- Specify counter authority, event timing, coordinate conventions, and observability precisely enough for controlled tests.

**Non-Goals:**

- Changing the controller, bridge, flight mode, mission policy, calibration chain, or launch control. No automatic `DISMISS`, `OFF`, or `MISSION` messages.
- Training models or calibrating score/motion thresholds through manually labeled data. Those validators are nonblocking stubs.
- Guaranteeing all boxes are correct, retracting already published messages, guaranteeing subscriber delivery latency, or proving flight safety.
- Adding inference workers, pinning unbounded image history, changing RKNN feature-transfer contracts, or treating host tests as target-board acceptance.

## Decisions

### 1. Use distinct names for results, track state, and lifecycle phases

**Choice:** use the following vocabulary in code-facing diagnostics, tests, and documentation.

| Term | Meaning |
| --- | --- |
| Frame F1 | Original image, capture time, and identity |
| Detection D1 | YOLO box or candidate set describing F1 |
| Tracker measurement T1 | Candidate box from the local NanoTrack/center-stabilization path on F1, before the trusted motion filter; retain raw NanoTrack box and score alongside it |
| Track instance | Target template, current position/size, and state spanning images; a logical instance can reuse the same C++ object |
| Provisional track | Initialized instance not yet authorized for control output |
| Verified track | Instance that completed verification; output still depends on freshness and structural validity |
| Published observation | Bbox and LOS constructed together for one reference time; ROS transports them independently |
| Acquisition attempt | One seed, its initialization, catch-up, and later-image verification |
| Recovery episode | Period after a previously verified track loses trust; can contain several acquisition attempts |
| Confirmation | Fresh detector agreement renewing an already verified instance's permission |
| Revocation | Removing permission for future valid output; it does not retract past messages |

Internal phases are Searching, PreparingProvisional, VerifyingProvisional, and VerifiedTracking. PreparingProvisional includes initialization and catch-up; detections can finish concurrently. Verification is performed when both required paths have supplied evidence. Recovery is an umbrella process, not a synonym for one reinitialization call.

**Why:** the former word "candidate" could mean a detection, one tracker box, or a persistent attempt. **Rejected alternative:** deriving authorization from initialization or the public numeric state alone. **Consequence:** keep separate fields for model initialization, current attempt, and output permission.

Public states remain Detecting=0, TargetFound=1, Tracking=2, and TargetLost=3. Searching and provisional phases report Detecting. Verification success produces TargetFound, followed by Tracking on the next normal verified update. Revocation immediately removes local permission and registers one pending loss event. Its ordered TargetLost publication attempt can occur later if publication access is delayed. That attempt precedes subsequent recovery-state publications, including Detecting or a newly verified instance's TargetFound. Diagnostics keep local transition time separate from publication-attempt time; neither establishes subscriber receipt. OFF and device errors retain their own lifecycle semantics.

### 2. Initialize on D1 immediately and retain replay evidence for D2

**Choice:** use this numbered acquisition sequence:

1. Read completed detection D1 describing F1. Submit the current image F2 according to the existing ordering before costly saved-result processing.
2. While searching with invalid output, check the seed's identity, disposition, geometry, source clock relation, and original F1 pixels. An otherwise eligible positive D1 may seed this attempt even if its evidence age exceeds the confirmation lifetime. Accept a new attempt identity and create its target template immediately. Do not wait for D2. A stale D1 cannot revoke or replace a verified track, replace an active provisional attempt, or change the old instance's counter.
3. Replay buffered images after F1 in source order. Save each successful T_i with round, attempt, frame, and capture time. Every step must succeed; any preparation, inference, or geometry failure rejects this attempt.
4. Continue provisional updates while the second detector result is pending. Output remains invalid. The single worker continues normal request processing; the update does not wait for it or take a second result snapshot.
5. When D2 is consumed, find the new attempt's T2 on F2. Compare before using D2 to correct any track state. F2 must be later than F1, and the result must be fresh at verification and publication.
6. With a unique eligible overlap match, promote the instance and initialize trusted motion state at the current accepted provisional measurement. The confirming evidence deadline remains anchored to F2's capture time. Internal verification can finish while an earlier loss notification is pending, but publication of the new valid pair and recovery state must follow that notification's completed attempt. Recheck evidence age at actual publication admission; waiting does not extend the deadline.

D2's request can predate initialization. Its original submitted snapshot can be absent or describe an old instance. Do not mutate that snapshot. Select the attempt-specific replay T2 instead. If no matching evidence survives, do not substitute a newer box; request fresh verification and continue invalid output.

The current result slot already lets D2 finish while the update is initializing D1. It remains a later update's result snapshot. Initialization finishing first means provisional tracking waits only for evidence, not by blocking the worker. D2 finishing first means evidence waits for the replay comparison. Any result that becomes stale during this join cannot authorize output.

**Why:** waiting to initialize would waste available processing time and prevent building T2. **Rejected alternative:** requiring D2 submission after initialization or accepting D1 as its own verification. **Consequence:** extend catch-up reporting from a final box to bounded per-image value records. Retain comparison records for outstanding accepted requests and the active attempt, release them on result disposition or attempt cleanup, and never let them retain image ownership. A new request carries a track-attempt identity when one exists. An undisposed preinitialization request can be explicitly associated to the new attempt only if its image is strictly later than the seed and that attempt replayed the image successfully. Never reapply a result already disposed against another attempt. Original submitted metadata remains unchanged.

### 3. Return candidate detections and match by same-image overlap

**Choice:** extend `YoloResult` to contain all configured-class candidates surviving existing confidence filtering and non-maximum suppression in the first successful stage. Non-maximum suppression removes overlapping duplicate detections. Retain `best` if useful for compatibility and highest-score provisional seed selection.

Clip detection and tracking boxes to their source-image bounds. Exactly one candidate with IoU at least `iou_reset_threshold` is agreement. IoU is intersecting area divided by combined area. Multiple qualifying candidates are ambiguous; zero with usable context is disagreement. A lack of source context is neutral and does not renew permission. `iou_calibrate_threshold` remains a diagnostic distinction for strong agreement, not an additional mandatory threshold. Keep YOLO class-confidence thresholds; the deferred score validator concerns NanoTrack's appearance score.

Example: A overlaps the tracked target and scores 0.88; unrelated B scores 0.94. A confirms the track. B's higher score cannot force replacement. The cascade still stops at its first successful region or tile; this design does not claim visibility of competing objects outside searched regions.

**Why:** a single highest-score result hides both a correct lower-score match and ambiguity. **Rejected alternative:** accepting the highest-score box as identity evidence. **Consequence:** candidate storage is bounded by the existing model output and suppression results, not an accumulation across all frames or all search stages. Similar balloons can still be indistinguishable.

### 4. Separate control permission from tracking and counter cleanup

**Choice:** a small value-only validity state records round, attempt, phase, permission revision, seed identity, confirmation source time, deadline, and revocation reason. A permission revision changes whenever previous work must no longer authorize publication. Updates carry the revision under which they were produced.

For a verified instance, the first usable mismatch, ambiguous result, completed no-target search, actual tracking failure, invalid geometry, or deadline expiry revokes output. Recovery starts immediately. If that adverse result contains an eligible seed and its original pixels, initialize a new provisional attempt immediately; otherwise search broadly. A no-target result cannot seed recovery.

The raw NanoTrack update currently mutates local position before returning. A rejected update can remain on an untrusted internal branch, but cannot be committed into the trusted rotation-aware filter. The center-smoothing filter and optional DCF remain local to that branch; neither can restore permission. Provisional predictions used to find search regions do not become control output. On verification, initialize trusted state from accepted provisional tracking and clear old velocity and LOS rate history.

| Counter | New authority |
| --- | --- |
| YOLO unconfirmed | Count each distinct usable adverse completed result once against its instance; abandon a provisional attempt at `lost_max_count` |
| YOLO jump | Retained/reset but inactive; cannot accept a moved detection after repeated jumps |
| NanoTrack jump | Retained/reset but inactive; cannot reject a candidate or create a legacy early return |
| No-result frame watchdog | Retain `counter > lost_max_count * yolo_interval * 3` for internal cleanup only |

The first adverse result against verified A can increment A's diagnostic count and revoke it. If its positive detection seeds B, B starts at zero. During B's provisional phase, further usable mismatches or no-target results increment B without automatically replacing B on every result. A unique fresh match verifies B and clears its count. At the attempt limit or attempt timeout, abandon B and search for a new seed. Round reset clears all counters. Check identity and result disposition for every use. Check evidence freshness before verification, confirmation, detector-based revocation, or counter changes; the stale-positive seed exception applies only while searching with invalid output. Missing context, stale evidence, no reply, or a result already disposed does not increment the unconfirmed count. An irrelevant old attempt's result cannot affect its replacement. Resetting the frame watchdog or unconfirmed count never renews permission.

**Why:** counters decide whether to keep internal work, not how long bad observations may be published. **Rejected alternative:** waiting for five results to invalidate or using repeated jumps as proof of identity. **Consequence:** `lost_max_count` and legacy jump settings need explicit configuration documentation; their changed roles must not be hidden behind unchanged names.

### 5. Keep annotation-dependent checks nonblocking

**Choice:** appearance-score and motion-envelope hooks return allow plus `not_evaluated`. A motion envelope would bound movement, scale changes, and prediction residuals, but no calibrated bounds are introduced now. The two jump counters stay zero. Large motion and a weak positive NanoTrack score alone do not block candidate verification.

Structural checks remain active: initialized model state, successful preparation/execution, finite outputs, positive box size, and valid source identity and clock relation. Verification additionally requires same-image unique overlap and fresh evidence. The stale-seed exception does not relax structural checks or verification freshness. Existing DCF or center-filter thresholds select how to stabilize the center; they must not silently reject a verification candidate or authorize control output. Negative execution-failure sentinels remain actual failure signals.

**Why:** the user explicitly deferred manual annotation and threshold calibration. **Rejected alternative:** reusing old 150-pixel/size-ratio jump vetoes as apparently disabled validators. **Consequence:** smooth background drift can pass local checks until detector evidence disagrees or expires. Mechanical acceptance needs no manual labels and cannot establish a false-track accuracy rate.

### 6. Enforce elapsed evidence age, not a convenient output timestamp

**Choice:** add startup-only positive integer settings, available in ROS and replay configuration:

| Setting | Initial default | Meaning |
| --- | --- | --- |
| `tracker.confirmation_timeout_ms` | 100 | Maximum source-evidence age permitting valid output |
| `tracker.candidate_timeout_ms` | 1000 | Maximum elapsed time from seed acceptance for one provisional attempt |
| `tracker.expiry_check_interval_ms` | 10 | RK3576 supervisor check interval; must not exceed confirmation lifetime |

These are explicit engineering defaults awaiting workload measurement, not flight-safe limits. A seed whose image is available can start provisional work even if it is too old to confirm current output; only fresh later evidence can release permission. The candidate timeout bounds such attempts.

Seed eligibility and confirmation eligibility have different purposes. A stale result means its source age is at least `confirmation_timeout_ms`. A stale positive result may start provisional work only while searching with invalid output, with original pixels and otherwise eligible identity, disposition, geometry, and clock relation. It cannot verify, renew or independently revoke permission, change an existing instance's unconfirmed counter, or replace an active provisional attempt. Wrong-round, already disposed, and inapplicable-attempt results remain diagnostic only. A new attempt's normal zero count is initialization of new state, not a stale-evidence counter action.

Example: D1 captured at 1000 ms is consumed at 1120 ms under the 100 ms lifetime. While searching, it may seed an attempt if F1 pixels remain available. Its attempt deadline is 2120 ms with the 1000 ms candidate timeout. D2 captured at 1150 ms and consumed at 1180 ms may verify an independent T2 on that image. Permission then expires at 1250 ms, based only on D2. If D2 is already 100 ms old at verification, the attempt remains invalid.

**Configuration choice:** update `parameters/rk3576_sc233hgs_production_params.yaml`, `parameters/rk3576_ar0234_camera_params.yaml`, `parameters/rk3576_video_params.yaml`, `parameters/replay_fp16.yaml`, and `parameters/replay_int8.yaml` to start with `tracker.yolo_interval: 1`. Keep the existing configurable cadence and support other valid interval values. Do not impose interval 1 as a validity or startup requirement. Preserve each replay input's FPS and leave unrelated Jetson parameter templates unchanged.

The actual production camera operates at 60 FPS. If the tracker processes 60 images per second, intervals 4 and 1 offer detection submission every approximately 66.7 ms and 16.7 ms respectively. Processing fewer images, pending replacement, unread-result protection, and inference execution can reduce the completion rate. Interval 1 is a configuration starting point, not a promise of 60 YOLO completions per second. The frame-count watchdog still uses the configured interval: `lost_max_count=5` gives cleanup at update 16 for interval 1 and update 61 for interval 4.

**Why:** more frequent submission opportunities reduce the deliberate gap between supporting detections. **Rejected alternative:** requiring interval 1 in every configuration or silently extending confirmation lifetime for slower settings. **Consequence:** test template values separately from configurable scheduling and measure actual evidence ages and load on the target board.

Map source capture time into a monotonic decision clock using an explicit relation to the source time domain. In production, subtract age already accrued before result consumption from the remaining lifetime; never start a fresh 100 ms interval on receipt. Use checked integer arithmetic. Future capture times, inconsistent clock relations, and clock rollback revoke or withhold permission rather than creating negative age. Once mapped, elapsed-time enforcement is monotonic. Keep valid observation reference time separate: `publish_prediction=false` uses capture time; prediction uses the existing eligible pose reference. The 300 ms pose rule is unrelated to the 100 ms detector rule.

At `now >= deadline`, permission is expired. A confirming image at 1000 ms consumed at 1080 ms has only 20 ms remaining. A publication at 1100 ms fails the final check, even if its selected image/pose reference is older.

The shared algorithm receives time through an explicit injected timing boundary rather than including ROS or RKNN details. Identify the evidence clock mapping in requests, retained comparison evidence, and reports:

| Mapping identity | Input mode | Evidence anchor |
| --- | --- | --- |
| `capture-monotonic-v1` | Production input and independently timed regression input | Actual source capture mapped into the monotonic execution clock using the recorded source-clock relation; preserve age accrued before tracker admission |
| `standalone-admission-v1` | Slowed standalone replay | Monotonic time sampled once when a prepared image enters tracking after its scheduled replay wait, immediately before its tracker update |

An evidence anchor is the immutable execution-clock time from which one source image's confirmation lifetime is measured. For standalone replay, preserve the source timestamp separately for image order, filter updates, and observation references. Use this sequence:

1. Decode and prepare the image, retaining its source timestamp. Complete the existing replay wait without changing the source FPS or scheduler.
2. Sample `anchor_ns` from the injected monotonic clock immediately before admitting the prepared image to its tracker update. Associate it with the source image identity.
3. Carry that original value into detector request context and any retained replay measurement for the image. Catch-up reuses the image's anchor; it does not treat replay execution as a new admission. Value records follow the existing bounded request/attempt lifetime and do not own pixels.
4. At a decision or publication check, calculate `age_ns = now_ns - anchor_ns` and `deadline_ns = anchor_ns + confirmation_timeout_ms * 1000000`. Use checked integer arithmetic and reject negative elapsed time or an invalid clock relation. At `now_ns >= deadline_ns`, the evidence is expired.

Initialization, asynchronous detector execution, catch-up, waits for later updates, and publication stalls after an image's admission all consume that image's lifetime. Admitting a newer image cannot replace an older image's anchor. The provisional-attempt deadline still starts at seed acceptance and uses its separate candidate timeout.

Decoding, copying, and scheduled waiting before standalone admission are outside this synthetic evidence-age measure. Preserve their execution metrics separately. This convention does not claim to reconstruct physical camera capture age. Production and independently timed regression input use `capture-monotonic-v1`; an image already 80 ms old at admission that then spends 30 ms in detection is 110 ms old, not 30 ms old.

Deterministic standalone example, with all values below expressed in milliseconds:

1. At 50 FPS, F1 has source time 1000 and admission anchor 5000. A controlled slow update finishes at 5120.
2. The existing slowed scheduler waits its next 20 ms period. F2 has source time 1020 and admission anchor 5140. F1's anchor remains 5000.
3. At decision time 5170, F1's evidence age is 170 and F2's is 30. With a 100 ms lifetime, F2's deadline is 5240. F1 cannot confirm output.
4. A stall until 5240 expires F2 exactly at the boundary. A result arriving then, catch-up replay of F2, or admission of F3 cannot give F2 another lifetime.

**Why:** this defines the slowed standalone runner's synthetic evidence age without changing its source timeline or hiding time spent after admission. **Rejected alternative:** a fixed source-to-execution origin for that runner, which accumulates prior processing and intentional pacing lag as if every newly admitted image had physically waited since capture. Also reject remapping retained images on result arrival, which would hide actual evidence delay. **Consequence:** record the mapping identity and original anchors, and reject same-condition evidence-age comparisons between the two mappings. The standalone runner's unit rotation and synthetic clock do not establish production timing or camera-rotation acceptance. Controlled tests inject both domains and exercise stalls without depending on machine speed.

**Why:** `ctx.now_us` may now be the selected capture/pose reference rather than actual node time. **Rejected alternative:** deriving permission from that field or from completed-result counts. **Consequence:** preserve separate timestamps in reports and make delay/clock behavior testable before tuning defaults.

### 7. Publish loss as an event, not a correction to history

**Choice:** use the existing paired floating-observation builder and bag path. Add a trust-loss notification reason separate from OFF. Revoking permission and registering its pending loss event are one value-state transition. Identify the event by round, revoked attempt, and revocation revision. At serialized publication, construct one invalid bbox/LOS pair and clear LOS rate history. Its event timestamp is `max(node_now_us, last_observation_us + 1)` in microseconds, using checked conversion to nanoseconds. Here `node_now_us` is sampled at revocation; `last_observation_us` is read at serialized construction after any already admitted pair. Record actual construction and publication times separately. Do not use the delayed detector image's old time.

Example:

1. Image F1 at 1000 ms produces tracking measurement T1; its detection is still running.
2. Normal processing publishes an observation at reference time 1080 ms.
3. At 1090 ms, D1 reveals disagreement with T1. Revoke the current applicable instance.
4. Publish the invalid pair at event time 1090 ms. Already published messages and already applied commands cannot be retracted.

Duplicate expiry callbacks do not repeat this event. Once the pending loss attempt has completed, ordinary invalid image observations can continue at valid increasing image-reference times. If the event time overtakes a later-arriving image's reference, the existing builder suppresses that image pair; do not forge a valid sample time or clear the guard to force it through. New rounds reset the guard as before. Clock rollback remains explicit: invalid event timing does not manufacture increasing valid capture times, and recovery may need the existing OFF/ON clock-boundary reset.

Trust loss keeps recovery, enabled recording, and continuous preview alive. It does not set `off_notified_`, stop the camera, close the bag, or invoke `notifyObservationOff()`. If OFF arrives with a pending loss event, close normal admission, finish that event's attempt, then attempt the separate OFF notification before sealing the round. An old-round callback cannot emit the event again or affect a new round. Loss is not a mission command. Keep topic names, message types, QoS, floating precision, and per-topic publication outcomes unchanged.

**Why:** downstream consumers cannot retract history and the timestamp guard would reject a detector-image-time notification. **Rejected alternative:** silently stop publishing, backdate invalidation, or call OFF/DISMISS. **Consequence:** the unchanged controller can continue prediction-based guidance; this tracker change only establishes honest visual-measurement validity.

### 8. Keep expiry independent of the inference thread

**Choice:** the ROS node owns a lightweight expiry supervisor serviced by the ROS executor thread, separately from the image/model worker thread. It uses the same round/attempt/revision state as final publication admission. Model state and image buffers remain single-worker-owned. A supervisor can invalidate permission or request deferred cleanup but never concurrently reset a model.

Use a short validity-state lock for value transitions, including atomic registration of a pending loss with revocation. Reuse the existing publication serialization boundary for the builder, timestamp guard, bag-pair submission, state publication, and publication admission. When both locks are needed, acquire publication serialization before the validity-state lock. The supervisor releases the validity lock before requesting publication access. Never hold a model lock while taking the publication boundary. Never hold the validity lock across inference, catch-up, image mapping, disk I/O, or ROS publication. Use revision checks when crossing from worker work to publication.

A timer expiry first revokes local permission and records the event. The serialized publisher attempts its invalid pair without waiting for inference. At the final admission point, normal output rechecks identities, revision, and actual evidence age. A pair admitted before revocation or a ROS call already in progress cannot be recalled; no later valid pair may be admitted under the old revision. State publication must also be centralized so a stale worker cannot overwrite TargetLost with Tracking.

Pending loss is also an ordering condition for new attempts, not just old revisions. Under publication serialization, finish preceding loss-notification work before admitting another image observation pair or a later phase's state publication. Either the supervisor or a worker reaching this boundary can service the same event; event identity and serialized disposition prevent duplicate attempts. Initialization and verification of B can proceed independently, but B's valid pair and TargetFound/Tracking publication cannot overtake A's loss attempt. Do not create a separate unbounded notification queue or require a dedicated publisher thread.

A loss attempt completes after invalid-pair construction, paired bag submission has been attempted when recording is enabled, both topic publication calls have been attempted, and their independent outcomes and the TargetLost publication attempt have been recorded. A bag or topic failure does not skip the other topic's attempt. Completion does not require subscriber acknowledgment or successful delivery. Mark the event disposed before admitting recovery output; a duplicate callback must not retry it afterward. If publication remains blocked, the event stays pending and later output admission stays closed, while permission remains revoked and internal recovery can continue.

Example with a delayed supervisor:

1. At 1090 ms, the supervisor revokes A and registers event E under the short validity lock. It is then paused before publication access.
2. The worker observes invalid permission and initializes B. At 1160 ms it verifies B with fresh evidence from 1150 ms. E is still pending; B's evidence deadline remains 1250 ms.
3. At 1161 ms, a publication path obtains serialization and services E before any B observation or later-phase state. It attempts A's invalid pair and TargetLost publication, then records E's disposition.
4. It rechecks B's identity, revision, geometry, and evidence age before any valid admission. An eligible B sample must also pass the unchanged timestamp guard. If E instead blocks until 1250 ms, B's evidence has expired and cannot authorize output.
5. When the supervisor resumes, it finds E already disposed and emits nothing for E. It may still revoke a genuinely expired B through a distinct event.

**Why:** revoking A before verifying B does not by itself ensure that A's publication attempt finishes before B's output. A short value-state lock and a publication lock protect different operations. **Rejected alternative:** checking only old output revisions, relying on lock acquisition fairness, or waiting for ROS subscriber acknowledgment. **Consequence:** publication admission must drain preceding pending loss work, and tests must pause the supervisor between revocation and publication.

The nominal check interval is not a hard delivery bound. If an expiry callback is delayed by scheduling, record the delay; final valid publication checks still reject expired permission. Existing reliable ROS calls can delay return or delivery, and a prior call can delay serialized notification. Report those delays rather than claiming immediate subscriber action. Process death is outside an in-process timer's guarantee.

**Why:** a timeout checked only after `update()` cannot respond during blocked inference or absent input. **Rejected alternative:** a timer in the worker loop, waiting for `consumed=true`, or concurrent model reset. **Consequence:** concurrency tests need controlled inference barriers, publication races, state ordering, and OFF/ON isolation.

### 9. Correct filter rectangle coordinates at the boundary

**Choice:** convert `(cx, cy, w, h)` to `(cx-w/2, cy-h/2, w, h)` before constructing `kf_pred_box`. Reuse that correctly typed rectangle for preferred search regions, buffered/source prediction metadata, and comparison diagnostics. Keep floating values until the existing display conversion. Prefer a small named conversion helper so coordinate semantics are visible.

Example: `(100,50,40,20)` becomes `(80,40,40,20)`. An unconstrained doubled search rectangle is `(60,30,80,40)` and remains centered at `(100,50)`. Test non-square boxes, fractional centers, clipping, and minimum-region constraints; those later operations must not shift an unconstrained center by half a box.

**Why:** the current constructor changes the apparent prediction center, affecting both where detection searches and what later comparison means. **Rejected alternative:** adjusting each downstream function or preserving the mismatch for reference compatibility. **Consequence:** this is an explicit algorithmic correction in the delta, with deterministic geometry tests and replay comparison. Do not claim it explains the November drift without evidence.

### 10. Integrate reporting and validation without requiring annotation

**Choice:** extend existing diagnostic records and replay/regression reporting with phase, attempt, permission revision, comparison origin (submitted or replay), candidate count, match outcome, evidence age/deadline, counter disposition, validator status, and revocation/publication event times. Include pending loss identity, attempt completion/disposition, and later-output admission ordering. Version structured formats where fields or meanings change. Preserve existing exact integer timestamp handling and independent publication outcomes.

Record `capture-monotonic-v1` or `standalone-admission-v1`, the source timestamp, evidence anchor, decision time, deadline, and declared units as exact integers. Keep stale-seed use separate from fresh verification and diagnostic-only rejection. Apply the `rk3576-tracking-regression` delta's reporting rules explicitly: old records retain their original age-observation-only meaning, missing evidence is unavailable, and unknown formats are rejected. Existing compatible metrics remain independently usable; a clock-mapping difference prevents a same-condition timing pass for the affected metrics.

Report availability separately from expiry correctness. Retain each existing frame-valid ratio's definition and state its denominator, sample count, and measurement window. Keep ordinary invalid image output, timestamp-suppressed output, and absent output distinguishable. Trust-loss and OFF event pairs do not enter frame-valid ratios. Do not substitute a changed denominator without a declared metric-version change.

For each recovery episode, record revocation, successful local verification, first subsequent valid-pair admission, and independent topic publication outcomes as distinct milestones. For example, revocation at 1090 ms, verification at 1160 ms, and valid admission at 1180 ms give 70 ms to verification and 90 ms to admission. Neither milestone proves subscriber delivery. An episode that does not restore output within the measurement window is incomplete, not zero-duration or successful recovery. Also retain request completion rate, processed-image rate, confirmation-age distributions, notification delay, and resource/load observations.

Protocol tests can pass while valid-output availability is zero. Report that outcome directly. If no versioned availability or recovery-duration threshold was supplied, mark performance criteria unset and do not infer an overall performance pass. Thresholds must be frozen before the measured candidate run; do not tune them to its results. The initial 100 ms lifetime remains an engineering default pending identified production-workload measurements.

Tests use real shared state logic with controlled model outputs and clocks. They cover D1/D2 completion orders, intermediate replay failures, missing/evicted images, retained metadata, multiple candidates, stale/duplicate/old-attempt results, first-result invalidation, inactive jump rules, coordinate conversion, expiry with blocked inference, paired bag submission, LOS reset, timestamp suppression, publication races, and OFF/ON cleanup. Pause the supervisor after revocation but before publication, allow B to verify, and prove that A's loss attempt precedes B's publication. Also cover event disposal after publication failures, B expiring while E is pending, and OFF draining pending loss before its final notification. Update existing tests that explicitly assert legacy first-detection validity, partial-replay success, jump early returns, or observational-only ages; do not delete unrelated scheduling coverage.

Inject the exact multi-frame standalone example above, including the stall at 5240 ms. Confirm that old anchors survive detector and catch-up paths and that a late production frame retains its pre-admission age. Reporting tests cover historical formats, mapping incompatibility, zero availability, incomplete recovery episodes, and unset performance thresholds.

**Why:** logging that a detection completed does not establish which image or attempt it confirmed. **Rejected alternative:** parsing display text or using green recorded boxes as reference labels. **Consequence:** acceptance can establish protocol and timing behavior without manual annotations. Quantitative accuracy and false-valid episode duration against the real balloon remain unmeasured until labeled data is supplied in future work.

## Risks / Trade-offs

- **Detector slower than 100 ms or gaps between qualifying results** -> valid-output availability can be low even with interval-1 templates and a 60 FPS camera. More submission opportunities can also increase detector load. Measure actual processed-image rate, confirmation-age distributions, completion rate, recovery duration, and load on the production board; keep unavailable output rather than silently extending the deadline. Report zero availability and incomplete recoveries explicitly. Retain configurable intervals and tune explicit settings through review, not threshold suppression. Expiry correctness alone does not establish usable vision.
- **Both models agree on the wrong object** -> a fresh confirmation can still be false. Preserve ambiguity handling and record that deferred appearance/motion checks and unlabeled replay do not prove identity or accuracy.
- **Provisional attempts repeatedly fail** -> retain a fixed per-attempt budget and timeout, broaden search, and release obsolete attempt records. Do not keep reinitializing on every provisional mismatch and resetting its budget.
- **History too short for detection latency** -> comparison metadata may survive but seed pixels or replay evidence may not. Record unavailable recovery separately; do not pin unbounded images or substitute a different image.
- **Revocation races with publication, recovery, or OFF** -> register pending loss atomically with revocation and drain it at common publication serialization before later output or the OFF notification. Use explicit permission revisions, separate loss/OFF event identities, and controlled race tests. A delayed loss attempt can withhold otherwise verified recovery output; recheck freshness rather than extending its lifetime. Already admitted ROS output remains a stated limit.
- **Clock changes or delayed publisher calls** -> revoke locally, preserve valid-reference timestamps, report event and publication delay, and do not promise downstream deadlines.
- **Shared algorithm changes affect other backends** -> preserve SDK boundaries and compile compatibility; include existing Jetson output eligibility in integration review. RK3576 acceptance does not establish Jetson hardware equivalence or authorize a Jetson deployment.
- **Controller continues guiding without vision** -> document as an external limitation. No workaround command or controller change is part of this change.

## Migration Plan

1. Implement the shared validity state, coordinate conversion, retained replay evidence, candidate association, and nonblocking stubs under this change's tasks. Update behavioral tests and configuration contracts together.
2. Wire RK3576 paired publication and independent expiry to the same state. Preserve OFF/ON, QoS, recording, and preview behavior. Keep both normal and event observations diagnosable.
3. Run applicable controlled tests and the repository build/test workflow in its configured SDK environment. Record exact commands, statuses, source revision, and unavailable checks. Review shared Jetson compilation/eligibility without claiming physical validation.
4. On an identified HZ-RK3576_WuKong production board, run short targeted replay/input-stall and ON/OFF checks using the system RKNN Runtime 2.3.2 and driver 0.9.8. Record model and input hashes, effective parameters, clock-mapping identity, observation age, notification delay, resource cleanup, valid-output availability, and both recovery milestones. State measurement windows and incomplete episodes. Keep synthetic standalone results separate from independently timed capture/regression results. Report unset performance criteria when no versioned availability or recovery threshold was supplied. Keep each targeted performance/load run within the repository's 180 s limit.
5. Compare the November 4 input qualitatively and report protocol timing separately from tracking accuracy. No manual annotation task blocks this change. Do not claim flight-safe control response or physically deploy for free flight on the strength of these checks alone.
6. If rollback is required, deploy the previous complete binary/configuration pair through the existing deployment workflow. Do not selectively re-enable legacy jump acceptance or relax confirmation checks inside the new state machine. State explicitly that rollback restores the old false-valid exposure.

## Open Questions

- What confirmation lifetime and candidate timeout give acceptable availability on the actual production workload? The initial values and deterministic boundary behavior are fixed above; tuning them later does not change the chosen architecture or require annotated targets.
- What accuracy improvement is demonstrated on manually labeled violent-motion recordings? That evaluation and future real score/motion validators are deferred and do not block the protocol acceptance defined here.
