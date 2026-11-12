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

The implementation at `1474c7f` predates the revised seed eligibility, removal of frame-count cleanup, and bounded detector scheduling specified below. Existing dated validation reports retain their original revision and configuration meanings; they do not validate these revisions. Main specifications still preserve several legacy behaviors; the deltas explicitly replace them rather than presenting the final policy as an existing contract.

## Goals / Non-Goals

**Goals:**

- Keep target-template initialization separate from authorization to publish valid output.
- Verify initial acquisition and recovery using two distinct detection images and independent same-image tracking evidence.
- Bound output permission using source capture age, including while input or inference stalls.
- Preserve asynchronous thread separation and model/platform boundaries while replacing indivisible detector cascades with bounded one-call scheduling.
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
2. While searching with invalid output and no active replacement attempt, check the seed's source identity, disposition, geometry, source clock relation, and original F1 pixels. An otherwise eligible positive D1 may seed this attempt even if its evidence age exceeds the confirmation lifetime or its request named an expired attempt. The old attempt tag alone is not a seed rejection. Accept a new attempt identity and publish the short `SeedAccepted` control before creating its target template or starting catch-up. Do not wait for D2. The seed cannot revive its old instance, replace a verified or provisional instance, or change an existing instance's counter.
3. Replay buffered images after F1 in source order. Save each successful T_i with round, attempt, frame, and capture time. Every step must succeed; any preparation, inference, or geometry failure rejects this attempt.
4. Continue provisional updates while the second detector result is pending. Output remains invalid. The single worker continues normal request processing; the update does not wait for it or take a second result snapshot.
5. When D2 is consumed, find the new attempt's T2 on F2. Compare before using D2 to correct any track state. F2 must be later than F1, and the result must be fresh at verification and publication.
6. With a unique eligible overlap match, promote the instance and initialize trusted motion state at the current accepted provisional measurement. The confirming evidence deadline remains anchored to F2's capture time. Internal verification can finish while an earlier loss notification is pending, but publication of the new valid pair and recovery state must follow that notification's completed attempt. Recheck evidence age at actual publication admission; waiting does not extend the deadline.

D2's request can predate initialization. Its original submitted snapshot can be absent or describe an old instance. Do not mutate that snapshot. Select the attempt-specific replay T2 instead. If no matching evidence survives, do not substitute a newer box; request fresh verification and continue invalid output.

The protected result mailbox lets D2 finish while the update is initializing D1. Taking D1 releases mailbox capacity before the tracker completes its arbitration; it does not require D1 initialization to finish. D2 remains a later update's result snapshot. Initialization finishing first means provisional tracking waits only for evidence, not by blocking the worker. D2 finishing first means evidence waits for the replay comparison. Any result that becomes stale during this join cannot authorize output.

**Why:** waiting to initialize would waste available processing time and prevent building T2. **Rejected alternative:** requiring D2 submission after initialization or accepting D1 as its own verification. **Consequence:** extend catch-up reporting from a final box to bounded per-image value records. Retain comparison records for outstanding accepted requests and the active attempt, release them on result disposition or attempt cleanup, and never let them retain image ownership. A new request carries a track-attempt identity when one exists. An undisposed preinitialization request can be explicitly associated to the new attempt only if its image is strictly later than the seed and that attempt replayed the image successfully. Never reapply a result already disposed against another attempt. Original submitted metadata remains unchanged.

### 2a. Separate confirmation eligibility from new-attempt seeding

**Choice:** evaluate the two uses of a result separately. Confirmation asks whether the result has fresh, same-image evidence for the applicable instance. Seeding asks whether its detection and original pixels can start new work while Searching with invalid output and no active replacement. An old request's attempt tag describes its origin; it does not by itself make its detection pixels unusable as a seed.

Apply current-round, source-image identity, undisposed-request, finite positive geometry, clock-relation, and original-pixel checks before accepting a seed. Reject already disposed results and wrong-round results. If B is already preparing, verifying, or verified, a late A result cannot replace B under this seed rule. Missing original pixels remain a seed rejection even if metadata survives. A stale positive retains the same restricted seed use; staleness never permits confirmation or counter changes against an existing instance.

Use this concrete sequence, with zero-based image numbers:

1. A expires around image 35 and registers its pending loss event. Its permission stays revoked.
2. Previously undisposed D32 arrives while image 42 is processed. Its request was submitted while A existed. F32 pixels are still in the bounded history.
3. Accept D32 once as B's seed. Give B a new attempt identity and permission revision, zero unconfirmed count, and a candidate deadline measured from this seed acceptance. Preserve D32's original request metadata naming A. Send `SeedAccepted(B)` before the expensive work in the next step.
4. Initialize B on F32 and replay every buffered image through the current image. Store B's measurements separately from A's submitted comparison boxes. Any failed replay step rejects B. Initialization and replay do not authorize output.
5. A fresh D41 can verify B only if B independently produced T41, F41 is strictly later than F32, and the same-image comparison has one eligible overlap match. D41 may have been submitted before B existed. Its own original metadata remains unchanged. An A snapshot cannot replace B's T41, and D32 cannot verify the attempt it seeded.
6. Publish B's valid observations and recovery state only after A's pending loss attempt finishes and actual-age, revision, and geometry checks pass. Reset trusted velocity and LOS rate history at verification as already specified.

This example describes eligibility, not a claim that D41 will actually match or remain fresh. Record one terminal result disposition for each consumed stage job, separately from its parent plan outcome. Separate `seeded_new_attempt` from confirmation, unavailable seed pixels, duplicate disposal, wrong-round rejection, and protection of an active replacement. Retain the original attempt, the accepted seed's new attempt, and the reason each use was accepted or rejected.

**Why:** discarding a first-consumed detection only because it was requested during expired A wastes a usable recovery image. **Rejected alternatives:** silently renewing expired A, rewriting its request to claim B provenance, or reapplying a result already consumed against another attempt. **Consequence:** remove the blanket old-attempt seed exclusion while preserving distinct confirmation checks, one-time disposition, bounded ownership, and fresh independent verification.

### 3. Return regional candidates and distinguish incomplete search from disagreement

**Choice:** each stage job runs the model on one region and returns all configured-class candidates surviving existing filtering and non-maximum suppression. A stage job is one scheduled inference call. A discovery plan is a declared multiscale search of one fixed source image. It has its own identity and a coverage ledger, meaning the regions actually searched and their outcomes. Jobs and plans have separate completion records.

Clip candidates and the genuine same-image tracker measurement to the source bounds. Exactly one candidate with IoU at least `iou_reset_threshold` is agreement; IoU is intersecting area divided by combined area. Multiple qualifying matches remain fresh ambiguity and revoke permission. `iou_calibrate_threshold` remains diagnostic. A prediction, held display rectangle, or remembered search region cannot supply the measurement.

A partial region with no matching candidate is `PartialNoAgreement`. It neither renews permission nor increments the adverse-result counter. A whole-image inference alone is also incomplete when the declared plan includes tiles. A completed fixed-image plan with no candidates can produce `CompleteNoTarget`. A completed applicable comparison plan with candidates but no agreement can produce `CompleteDisagreement`. Missing comparison context, stale evidence, cancellation, or an errored/incomplete plan cannot supply either usable adverse verdict.

For confirmation, success is the first unique eligible same-image agreement, not merely the first class-positive region. An unrelated positive cannot end confirmation search. Searching without an active instance can pause on its first positive seed candidate, retain the highest-score seed choice, and finish the plan when the tracker accepts that seed. Keep every stage's candidates and region provenance separate; do not merge them into an apparent single inference. A plan-level verdict records coverage and dispositions, not a fabricated candidate set.

Each stage job has one terminal result disposition. A final coverage marker accompanies the last stage result; it does not require another model call. The tracking consumer finalizes the plan after its ordered stage dispositions are known. Only one eligible adverse verdict per plan and applicable attempt can affect the counter. An ambiguity or agreement that closes a plan prevents a later in-flight stage from generating a second adverse verdict for it. Original result identity and separate seed eligibility still govern any late positive's permitted use.

Example: stage R1 returns an unrelated object scoring 0.94, while later R2 uniquely matches the source track at 0.88. R1 is partial no-agreement and cannot falsely prove target absence. R2 can confirm. Empty tiles searched on F10 and F11 never combine into absence on either image.

**Why:** quick partial searches can provide useful agreement but cannot establish the old completed-cascade absence verdict. **Rejected alternatives:** treating every crop miss as loss, stopping confirmation at an unrelated class-positive result, accumulating cross-image coverage, or incrementing the counter for every empty tile. **Consequence:** the current first-successful-stage rule is explicitly superseded for confirmation; complete fresh adverse evidence, ambiguity, actual tracker failures, and independent expiry retain their own authority.

### 3a. Keep inference asynchronous and schedule one model call at a time

**Choice:** retain one dedicated YOLO worker. It performs synchronous SDK calls, while the tracking thread submits image/context values and reads at most one result at each normal update. The tracking thread never calls or waits synchronously for YOLO. One region, whole-image pass, or tile is one scheduling quantum: the amount of device work completed before choosing again.

At every call boundary, apply OFF, round, and attempt controls; obtain result capacity; prune ineligible waiting work; then select the next call. The active synchronous call finishes before another job can use its SDK input storage. A control received during execution takes effect at that boundary. At most one already-running call can outlive the state for which it was dispatched; its returned result keeps its original identity and is rechecked before use.

Keep two logical choices: `T`, a targeted check on the latest eligible submitted image, and `D`, the next stage of a fixed-image discovery plan. If only one is eligible, serve it. If both remain eligible, serve at most two consecutive T calls, then one D stage and reset the targeted burst count. Serving D resets the count; serving T with no D waiting keeps the count capped at two, so newly eligible D obtains the next turn. Empty queues require no inference. The initial 2:1 policy is an explicit experimental scheduling choice, not a flight limit or a throughput guarantee; it adds no public configuration parameter in this revision.

Permission expiry is never postponed to satisfy this fairness rule. If capacity cannot serve both workloads within the lifetime, record the missed confirmation opportunity and retain invalid output. Model calls, preprocessing, postprocessing, SDK contention with NanoTrack, and tracker consumption must be measured separately. A 40 ms isolated call does not establish 25 useful confirmations per second or a 60 FPS tracking guarantee.

**Why:** an indivisible seven/eight-call cascade prevents newer checks from using the worker for approximately 280 ms in the recorded workload. **Rejected alternatives:** adding more submissions to an already replaceable slot, a whole-cascade FIFO for each image, preempting an SDK call, or adding inference workers before contention validation. **Consequence:** work is interleaved only between completed calls and remains within existing model/preprocessing/platform contracts.

### 3b. Bound pending work, image ownership, and discovery cancellation

**Choice:** retain one active model call, one latest-frame mailbox, one fixed-image discovery plan, one protected unread result, and one result locally owned by the tracking consumer. The frame mailbox is shared by the pending targeted image and the candidate image for the next discovery plan; it is not two frame queues. A discovery plan stores fixed source identity, declared region order, coverage, and classification progress. Remaining tiles are plan state, not an enqueued list of model calls. Active-call and plan references can alias the same image.

For example, Searching starts whole-image inference for D10 on F10. F11 becomes pending; F12 replaces F11 before it is selected. D10 continues through its remaining regions on F10. Finishing or cancelling D10 selects the then-latest eligible image, not an old FIFO entry. A new frame does not automatically cancel the active plan at every call boundary, which would continually starve later tiles.

Cancel queued work at OFF/round change, explicit supersession, or invalid source/clock/attempt eligibility. Drop an already expired unstarted targeted check as confirmation work. Do not cancel a discovery source solely because its confirmation age has expired while Searching: its positive may still seed under the separate seed rules. Cancel when original-pixel or complete catch-up eligibility required for its intended use is lost. A retained seed image alone cannot repair missing intermediate history. When an abandoned plan restarts on a new image, rotate the first tile of the new tile sequence; reset coverage for the new source rather than carrying it forward.

An expired A cancels queued A-only confirmation jobs. The already-running call finishes; its first-consumed positive may seed B only while Searching with no active replacement. Candidate timeout, actual failure, or an abandoned preparation similarly invalidates queued work for that attempt. None of these controls restarts a model or reintroduces frame-count cleanup.

The paused plan may retain its source `FrameRef`, but no SDK buffer binding or input lease can be reused across jobs before device completion. Stage results and control messages own values, not pixels. OFF stops dispatch, cancels waiting jobs/plans, wakes capacity waits, waits for active access, and joins the worker before releasing resources. Old-round controls and results cannot act on a later round.

When a targeted call has no existing discovery plan, its source may occupy the one plan slot as an awaiting-classification fallback. This keeps original pixels available without an image-owning feedback message. A definite empty region activates the declared remaining whole-image/tile search without a full tracker decision. Nonempty candidates wait for the short same-image classification; partial no-agreement activates the fallback, while agreement closes it. The fallback ledger references the original immutable targeted job as its initial searched region without rewriting that job's parent identity. If a plan already exists, later misses cannot create additional plans or add coverage from another image.

After a newer source image supplies a valid agreement for the same attempt, its feedback explicitly supersedes any older discovery plan. Cancel the older plan's unstarted stages. A stage already in flight finishes safely, but its superseded-plan no-target/disagreement cannot revoke the newer agreement or change its counter. Record the supersession rather than presenting the old plan as completed adverse coverage. Positive results keep their original identity and can have only the uses separately allowed by current seed/confirmation eligibility.

**Why:** bounded latest input keeps checks current without accumulating pixels or cancelling broad coverage forever. **Rejected alternatives:** fourteen queued calls for two images, unbounded paused plans, or freeing buffers to interrupt inference. **Consequence:** limits and cancellation reasons are observable and tested, including a stalled consumer and missing replay history.

### 3c. Validate the targeted crop with existing models

**Choice:** establish crop effectiveness before relying on targeted-check frequency. The identified temporary 0.40/200 ms board run had no ROI-only success among 68 completed requests. Its successful detections needed additional search stages. This is evidence for a crop investigation, not proof of a particular model defect.

Compare exact saved source pixels using the current minimum-320-pixel ROI, the exact region that previously succeeded, and a region with that successful size/aspect ratio recentered at the genuine same-image tracker measurement. Keep models, input contracts, and the comparison profile fixed. Check crop bounds, target inclusion where manually inspectable, resize/padding transforms, submitted input bytes, filtering, and candidates. A roughly 35-pixel object becomes about 70 pixels wide after a 320-to-640 resize, versus about 30 in a 739-to-640 resize. The scale change is measurable; its effect on detection remains a hypothesis to test.

Prefer a region size/aspect ratio supported by that comparison. Preserve the last successful search region as a bounded value-only hint, with its source and producing stage identity. It supplies no tracker measurement or permission. Invalidate an attempt-specific hint on its round/attempt transition; any retained Searching hint remains explicitly untrusted and is rechecked against new image bounds. If a chosen crop cannot provide effective checks, record that failure before claiming a scheduler performance gain. The formal confidence/lifetime defaults and NanoTrack device-feature transfer stay unchanged.

**Why:** scheduling more of an ineffective crop does not create fresh agreement. **Rejected alternatives:** assuming smaller crops are better, changing model normalization to improve scores, or treating an old detection location as current evidence. **Consequence:** crop-only, scheduling-only, and combined experiments have separate identities and outcomes.

### 3d. Separate result capacity from tracker-decision feedback

**Choice:** reserve one result-mailbox credit before every model call. A credit is permission to write the next result into an empty protected slot. Completion publishes one immutable stage result there. The tracker taking that result releases the credit immediately, before full arbitration, initialization, catch-up, or publication finishes. The next call can run while the first result is locally processed. If its result fills the mailbox, further calls wait for capacity. No result is overwritten.

This capacity backpressure is not a full tracker-decision barrier after every inference. An empty partial region with known remaining coverage can continue its plan when capacity and scheduling permit, without waiting for full tracking-state arbitration. In VerifyingProvisional or VerifiedTracking, the worker continues eligible jobs using the latest published phase values; later feedback cancels obsolete queued work. It does not wait globally for every regional match decision.

There is one narrow semantic pause: when a Searching discovery stage produces a positive seed candidate, pause only that plan's remaining regions while the tracker decides seed eligibility. Other eligible work can run with available capacity. If none exists, the worker can idle until that short disposition arrives. On acceptance, the tracker posts `SeedAccepted` before expensive model initialization or catch-up. This value-only control carries round, new attempt/revision, seed identity, and region hint. It closes the old discovery plan and enables later submitted-image checks. Rejection closes obsolete work and leaves Searching to select newer eligible input; it cannot become a no-target verdict.

Phase controls also identify attempt abandonment, permission revocation, and round closure. Apply them at call boundaries without modifying original request metadata. A later image submitted before `SeedAccepted` can be checked using the new region hint, but its original attempt/revision snapshot stays unchanged. Only independently produced B replay evidence permits its later association to B.

Use this acquisition sequence:

1. D10 becomes a positive regional result; the worker pauses only its Searching plan.
2. The tracking update takes D10, freeing result capacity, and performs the current-image submission before saved-result arbitration.
3. Eligibility succeeds; the tracker allocates B and sends `SeedAccepted(B)` before starting initialization/catch-up.
4. YOLO checks an already submitted later image F20 while the tracker prepares B. If no newer submitted image exists, it cannot substitute a camera-current image or renew an anchor.
5. D20 waits in the protected slot if preparation is still running. On a later update, it verifies only if B produced T20 and evidence remains fresh. D10 cannot verify its own seed.
6. Loss-before-recovery publication and actual-age admission checks remain required.

With an illustrative 40 ms per call, D0 can finish three discovery stages by 120 ms. The tracker takes it at 125 ms and sends acceptance before preparation ending at 190 ms. A submitted F7 check can run from 125 to 165 ms and wait until 190 ms. If F7's original anchor is 117 ms, its age is 73 ms then, leaving 127 ms under the separately identified 200 ms test profile. No part of this example promises actual board timing or renews F7's anchor.

**Why:** early acceptance feedback enables overlap, while a protected mailbox preserves evidence during slow preparation. **Rejected alternatives:** waiting for initialization to finish before enabling YOLO, waiting for every full tracker decision after every call, or allowing result overwrite to keep the device busy. **Consequence:** capacity release and semantic disposition are separate events in tests and diagnostics.

### 3e. Make scheduling phase-specific without inventing fresh inputs

**Choice:** select work from the latest published phase and immutable admitted images:

| Phase | Work and restrictions |
| --- | --- |
| Searching | Run discovery on one fixed image; a validated remembered region may lead its declared plan. No model call waits synchronously on the tracking thread. |
| PreparingProvisional | After early seed acceptance, check later submitted images using the seed's successful region as a hint; retain any metadata predating initialization. |
| VerifyingProvisional | Offer targeted checks frequently and give an eligible discovery plan its bounded share. Verification waits for same-image evidence, not by blocking the detector. |
| VerifiedTracking | Prefer fresh targeted confirmation, with the same bounded discovery share when broader work is waiting. Partial misses never extend permission. |
| Revoked/recovery | Keep output invalid, cancel queued expired-instance confirmation work, and search until an eligible seed starts a replacement. Old in-flight positives follow separate seed eligibility. |

A published control state is a value snapshot, not authority to publish observations. The final tracker/publication checks remain authoritative if a job finishes after that snapshot became obsolete. Camera arrival, job dispatch, result delivery, and plan continuation never replace an image's original capture/admission anchor.

**Why:** worker scheduling needs current purpose without owning algorithm state or borrowing camera SDK details. **Rejected alternative:** treating the newest captured but unsubmitted frame as available during a blocked tracker update. **Consequence:** catch-up blocking and unavailable fresh submissions remain explicit latency evidence rather than hidden timing corrections.

### 4. Separate control permission from tracking and counter cleanup

**Choice:** a small value-only validity state records round, attempt, phase, permission revision, seed identity, confirmation source time, deadline, and revocation reason. A permission revision changes whenever previous work must no longer authorize publication. Updates carry the revision under which they were produced.

For a verified instance, the first usable completed-plan disagreement, fresh ambiguous match, completed no-target search, actual tracking failure, invalid geometry, or deadline expiry revokes output. A partial regional miss or unrelated candidate remains neutral while the declared search is incomplete. Recovery starts immediately. If that adverse result contains an eligible seed and its original pixels, initialize a new provisional attempt immediately; otherwise search broadly. A no-target result cannot seed recovery.

The raw NanoTrack update currently mutates local position before returning. A rejected update can remain on an untrusted internal branch, but cannot be committed into the trusted rotation-aware filter. The center-smoothing filter and optional DCF remain local to that branch; neither can restore permission. Provisional predictions used to find search regions do not become control output. On verification, initialize trusted state from accepted provisional tracking and clear old velocity and LOS rate history.

| Counter | New authority |
| --- | --- |
| YOLO unconfirmed | Count each distinct eligible adverse plan verdict or closing ambiguity once against its instance; abandon a provisional attempt at `lost_max_count` |
| YOLO jump | Retained/reset but inactive; cannot accept a moved detection after repeated jumps |
| NanoTrack jump | Retained/reset but inactive; cannot reject a candidate or create a legacy early return |
| No-result frame watchdog | Removed; missing replies and processed-frame counts have no cleanup authority |

The first adverse result against verified A can increment A's diagnostic count and revoke it. If its positive detection seeds B, B starts at zero. During B's provisional phase, further eligible completed-plan mismatches or no-target verdicts increment B without automatically replacing B on every result. A unique fresh match verifies B and clears its count. At the attempt limit or attempt timeout, abandon B and search for a new seed. Round reset clears all counters. Check identity and result disposition for every use. Check evidence freshness before verification, confirmation, detector-based revocation, or counter changes; the stale-positive seed exception applies only while searching with invalid output. Missing context, stale evidence, no reply, or a result already disposed does not increment the unconfirmed count. An irrelevant old attempt's result cannot affect its replacement. Clearing the unconfirmed count never renews permission.

Remove the legacy `counter > lost_max_count * yolo_interval * 3` branch and its no-result counting/reset contract. Delayed or absent detector replies alone do not clear a provisional instance, its measurements, or its boxes. The independent confirmation timer still revokes output permission on time. The worker abandons a provisional instance at its first servicing opportunity at or after `candidate_timeout_ms`, measured from seed acceptance; progress and new replies cannot restart that deadline. Usable adverse-result exhaustion and genuine execution or geometry failures retain their own abandonment paths. An already expired verified instance can enter recovery without a second loss event from later internal cleanup.

Keep image history and value-only measurement storage bounded and retain explicit OFF/service-shutdown ownership. Permission expiry continues during stalled input or inference; it does not require concurrent model reset. This revision adds no detector-health timeout, new configuration switch, model restart, or unsafe release of resources still used by the device. Existing request execution and disposal diagnostics can describe slow or absent progress without imposing a new reset policy.

**Why:** the old frame-count limit can discard provisional work during a normal detector search, and changes its elapsed meaning with FPS and `yolo_interval`. **Rejected alternatives:** increasing `lost_max_count` to hide detector latency, retaining a configurable frame-count reset, waiting for five results to invalidate, or using repeated jumps as proof of identity. **Consequence:** `lost_max_count` remains the usable-adverse-result budget only. Confirmation expiry, candidate timeout, actual failures, and shutdown each have explicit responsibilities; tests must prove no cleanup merely at the former numeric boundaries.

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

Seed eligibility and confirmation eligibility have different purposes. A stale result means its source age is at least `confirmation_timeout_ms`. A stale positive result may start provisional work only while searching with invalid output and no active replacement, with original pixels and otherwise eligible identity, disposition, geometry, and clock relation. A request naming expired A is not inapplicable for seeding solely because of that tag; decision 2a defines the separate seed checks. The result cannot verify, renew or independently revoke permission, change an existing instance's unconfirmed counter, or replace an active provisional attempt. Wrong-round and already disposed results remain diagnostic only. A new attempt's normal zero count is initialization of new state, not a stale-evidence counter action.

Example: D1 captured at 1000 ms is consumed at 1120 ms under the 100 ms lifetime. While searching, it may seed an attempt if F1 pixels remain available. Its attempt deadline is 2120 ms with the 1000 ms candidate timeout. D2 captured at 1150 ms and consumed at 1180 ms may verify an independent T2 on that image. Permission then expires at 1250 ms, based only on D2. If D2 is already 100 ms old at verification, the attempt remains invalid.

**Configuration choice:** update `parameters/rk3576_sc233hgs_production_params.yaml`, `parameters/rk3576_ar0234_camera_params.yaml`, `parameters/rk3576_video_params.yaml`, `parameters/replay_fp16.yaml`, and `parameters/replay_int8.yaml` to start with `tracker.yolo_interval: 1`. Keep the existing configurable cadence and support other valid interval values. Do not impose interval 1 as a validity or startup requirement. Preserve each replay input's FPS and leave unrelated Jetson parameter templates unchanged.

The actual production camera operates at 60 FPS. If the tracker processes 60 images per second, intervals 4 and 1 offer detection submission every approximately 66.7 ms and 16.7 ms respectively. Processing fewer images, pending replacement, unread-result protection, and inference execution can reduce the completion rate. Interval 1 is a configuration starting point, not a promise of 60 YOLO completions per second. `yolo_interval` controls submission cadence only; it does not scale confirmation lifetime, candidate timeout, or a no-result cleanup threshold.

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

Record stage-job and parent-plan identities separately from submitted image/request identities. Preserve searched-region coverage, cancellation/replacement reasons, seed acceptance and capacity-release times, targeted-burst decisions, and parent-plan terminal verdicts. Separate preprocessing, model execution, postprocessing, queued waiting, unread-mailbox waiting, and tracker catch-up blocking when measurable; unavailable components stay unavailable. Version fields and metric meanings that change, keeping historical whole-cascade request counts distinct from new one-call job counts. Report maximum outstanding frame/result ownership and no-overwrite evidence.

Record `capture-monotonic-v1` or `standalone-admission-v1`, the source timestamp, evidence anchor, decision time, deadline, and declared units as exact integers. Keep stale-seed use separate from fresh verification and diagnostic-only rejection. Apply the `rk3576-tracking-regression` delta's reporting rules explicitly: old records retain their original age-observation-only meaning, missing evidence is unavailable, and unknown formats are rejected. Existing compatible metrics remain independently usable; a clock-mapping difference prevents a same-condition timing pass for the affected metrics.

Report seed eligibility separately from confirmation eligibility, retaining original request-attempt provenance and the new seed-attempt identity. Report removed frame-count cleanup as inactive for the revised policy. Historical watchdog counts and timeout events keep their historical meaning; a missing new disposition field is unavailable. Do not reinterpret old watchdog firings as elapsed-time candidate expiry.

Report availability separately from expiry correctness. Retain each existing frame-valid ratio's definition and state its denominator, sample count, and measurement window. Keep ordinary invalid image output, timestamp-suppressed output, and absent output distinguishable. Trust-loss and OFF event pairs do not enter frame-valid ratios. Do not substitute a changed denominator without a declared metric-version change.

For each recovery episode, record revocation, successful local verification, first subsequent valid-pair admission, and independent topic publication outcomes as distinct milestones. For example, revocation at 1090 ms, verification at 1160 ms, and valid admission at 1180 ms give 70 ms to verification and 90 ms to admission. Neither milestone proves subscriber delivery. An episode that does not restore output within the measurement window is incomplete, not zero-duration or successful recovery. Also retain request completion rate, processed-image rate, confirmation-age distributions, notification delay, and resource/load observations.

Protocol tests can pass while valid-output availability is zero. Report that outcome directly. If no versioned availability or recovery-duration threshold was supplied, mark performance criteria unset and do not infer an overall performance pass. Thresholds must be frozen before the measured candidate run; do not tune them to its results. The initial 100 ms lifetime remains an engineering default pending identified production-workload measurements.

Tests use real shared state logic with controlled model outputs and clocks. They cover D1/D2 completion orders, intermediate replay failures, missing/evicted images, retained metadata, multiple candidates, stale/duplicate/old-attempt results, first-result invalidation, inactive jump rules, coordinate conversion, expiry with blocked inference, paired bag submission, LOS reset, timestamp suppression, publication races, and OFF/ON cleanup. Pause the supervisor after revocation but before publication, allow B to verify, and prove that A's loss attempt precedes B's publication. Also cover event disposal after publication failures, B expiring while E is pending, and OFF draining pending loss before its final notification. Update existing tests that explicitly assert legacy first-detection validity, partial-replay success, jump early returns, or observational-only ages; do not delete unrelated scheduling coverage.

Add the expired-A D32-to-B sequence, active-B replacement protection, duplicate/wrong-round/evicted-pixel seed rejection, and D41 verification using B replay evidence. Replace former frame-count cleanup assertions with delayed-result cases spanning updates 15/16 and 60/61 under intervals 1 and 4. Use injected clocks to keep the candidate deadline unexpired across those updates, then advance to that exact deadline and require abandonment. Independently expire output despite an input or inference stall and require only one loss event. Actual failures and the usable-adverse-result budget remain separate cases.

First compare crop choices on exact pixels, then scheduling with unchanged crops, then their combination. Controlled scheduler tests require bounded two-image submissions rather than fourteen queued calls, persistent discovery under continuous input, T/T/D fairness, no inference on the tracking thread, and no full-decision barrier for partial misses. Exercise positive Searching plan pause, early `SeedAccepted`, completion during preparation, credit release at take, full-mailbox waiting, and obsolete controls during a call. Test partial-empty and unrelated candidates, closing ambiguity, complete single-image no-target/disagreement exactly once, stage provenance, fixed-image coverage, cancelled plans, first-miss fallback activation, no accumulation from later-image misses, newer-agreement supersession, tile rotation, history loss, and safe OFF in every worker state. Retain source/attempt/clock and independent expiry tests.

Inject the exact multi-frame standalone example above, including the stall at 5240 ms. Confirm that old anchors survive detector and catch-up paths and that a late production frame retains its pre-admission age. Reporting tests cover historical formats, mapping incompatibility, zero availability, incomplete recovery episodes, and unset performance thresholds.

**Why:** logging that a detection completed does not establish which image or attempt it confirmed. **Rejected alternative:** parsing display text or using green recorded boxes as reference labels. **Consequence:** acceptance can establish protocol and timing behavior without manual annotations. Quantitative accuracy and false-valid episode duration against the real balloon remain unmeasured until labeled data is supplied in future work.

## Risks / Trade-offs

- **Targeted crops remain ineffective** -> establish crop-only evidence before relying on scheduling, preserve broad discovery, and report unavailable confirmation without changing model contracts.
- **Partial search is mistaken for complete absence** -> retain one-image coverage and ordered stage dispositions; only one eligible terminal plan verdict can act adversely.
- **Fast results outrun a blocked consumer** -> reserve result capacity before execution, bound unread/local values, and measure backpressure without overwriting evidence.
- **Fairness cannot meet both workloads on the board** -> retain the initial T/T/D policy as an identified experiment, report source age and gaps between usable confirmations, and keep expiry authoritative.

- **Detector slower than 100 ms or gaps between qualifying results** -> valid-output availability can be low even with interval-1 templates and a 60 FPS camera. More submission opportunities can also increase detector load. Measure actual processed-image rate, confirmation-age distributions, completion rate, recovery duration, and load on the production board; keep unavailable output rather than silently extending the deadline. Report zero availability and incomplete recoveries explicitly. Retain configurable intervals and tune explicit settings through review, not threshold suppression. Expiry correctness alone does not establish usable vision.
- **Both models agree on the wrong object** -> a fresh confirmation can still be false. Preserve ambiguity handling and record that deferred appearance/motion checks and unlabeled replay do not prove identity or accuracy.
- **Provisional attempts repeatedly fail** -> retain a fixed per-attempt budget and timeout, broaden search, and release obsolete attempt records. Do not keep reinitializing on every provisional mismatch and resetting its budget.
- **History too short for detection latency** -> comparison metadata may survive but seed pixels or replay evidence may not. Record unavailable recovery separately; do not pin unbounded images or substitute a different image.
- **Revocation races with publication, recovery, or OFF** -> register pending loss atomically with revocation and drain it at common publication serialization before later output or the OFF notification. Use explicit permission revisions, separate loss/OFF event identities, and controlled race tests. A delayed loss attempt can withhold otherwise verified recovery output; recheck freshness rather than extending its lifetime. Already admitted ROS output remains a stated limit.
- **Clock changes or delayed publisher calls** -> revoke locally, preserve valid-reference timestamps, report event and publication delay, and do not promise downstream deadlines.
- **Shared algorithm changes affect other backends** -> preserve SDK boundaries and compile compatibility; include existing Jetson output eligibility in integration review. RK3576 acceptance does not establish Jetson hardware equivalence or authorize a Jetson deployment.
- **Controller continues guiding without vision** -> document as an external limitation. No workaround command or controller change is part of this change.

## Migration Plan

1. Implement the shared validity state, coordinate conversion, retained replay evidence, regional candidate association, bounded one-call scheduler, early seed feedback, and nonblocking stubs under this change's tasks. Update behavioral tests and configuration contracts together.
2. Wire RK3576 paired publication and independent expiry to the same state. Preserve OFF/ON, QoS, recording, and preview behavior. Keep both normal and event observations diagnosable.
3. Run applicable controlled tests and the repository build/test workflow in its configured SDK environment. Record exact commands, statuses, source revision, and unavailable checks. Review shared Jetson compilation/eligibility without claiming physical validation.
4. On an identified HZ-RK3576_WuKong production board, run short targeted replay/input-stall and ON/OFF checks using the system RKNN Runtime 2.3.2 and driver 0.9.8. Record model and input hashes, effective parameters, clock-mapping identity, observation age, notification delay, resource cleanup, valid-output availability, and both recovery milestones. State measurement windows and incomplete episodes. Keep synthetic standalone results separate from independently timed capture/regression results. Report unset performance criteria when no versioned availability or recovery threshold was supplied. Keep each targeted performance/load run within the repository's 180 s limit.
5. Compare the November 4 input qualitatively and report protocol timing separately from tracking accuracy. No manual annotation task blocks this change. Do not claim flight-safe control response or physically deploy for free flight on the strength of these checks alone.
6. If rollback is required, deploy the previous complete binary/configuration pair through the existing deployment workflow. Do not selectively re-enable legacy jump acceptance or relax confirmation checks inside the new state machine. State explicitly that rollback restores the old false-valid exposure.

## Open Questions

- What confirmation lifetime and candidate timeout give acceptable availability on the actual production workload? The initial values and deterministic boundary behavior are fixed above; tuning them later does not change the chosen architecture or require annotated targets.
- What accuracy improvement is demonstrated on manually labeled violent-motion recordings? That evaluation and future real score/motion validators are deferred and do not block the protocol acceptance defined here.
