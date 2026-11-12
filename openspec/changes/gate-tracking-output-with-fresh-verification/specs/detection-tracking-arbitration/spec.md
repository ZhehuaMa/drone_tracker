# Spec Delta

## RENAMED Requirements

- FROM: `### Requirement: Usable evidence retains existing arbitration thresholds`
- TO: `### Requirement: Usable evidence retains overlap thresholds and revokes trust promptly`
- FROM: `### Requirement: Unavailable recovery cannot become successful jump acceptance`
- TO: `### Requirement: Unavailable recovery cannot authorize tracking`
- FROM: `### Requirement: Submission cadence and inference search stay unchanged`
- TO: `### Requirement: Submission cadence and inference search preserve bounded verification`
- FROM: `### Requirement: Watchdog threshold and timeout action remain unchanged`
- TO: `### Requirement: Elapsed-time attempt limits govern no-result cleanup`
- FROM: `### Requirement: Result age remains observational`
- TO: `### Requirement: Result age constrains confirmation eligibility`
- FROM: `### Requirement: External state meanings and recovery acceptance remain compatible`
- TO: `### Requirement: External state values and verified recovery acceptance are explicit`

## MODIFIED Requirements

### Requirement: Arbitration compares boxes from the same source frame

Positive detections SHALL be compared only with a genuine tracking measurement from the same source image, round, and applicable acquisition attempt. Retained request metadata or attempt-specific replay evidence SHALL supply that comparison. Current-image boxes, unrelated previous boxes, held display boxes, and old track instances MUST NOT substitute for missing evidence.

#### Scenario: Current position differs from source position
- **WHEN** a delayed detection overlaps its source measurement by 0.584 but has zero overlap with the current image's box
- **THEN** arbitration uses 0.584 and records the source image and track-attempt identities

#### Scenario: Neither comparison basis exists
- **WHEN** a positive detection has neither eligible submitted-source evidence nor replay-produced evidence for its source image
- **THEN** arbitration records unavailable context without inventing zero overlap or confirming tracking

#### Scenario: Preinitialization request has replay evidence
- **WHEN** F2 was submitted before D1 initialized attempt B, and B subsequently replayed F2 successfully
- **THEN** D2 can be compared with B's F2 measurement without rewriting its original request snapshot or using old attempt A's box

### Requirement: Partial comparison data has explicit eligibility

IoU is intersecting box area divided by combined box area. Confirmation SHALL require a genuine same-image tracker measurement. A prediction alone MUST NOT confirm tracking. Missing prediction metadata SHALL not prevent overlap-based verification. Legacy position and size jump rules SHALL have no verification authority while motion validation is a nonblocking stub.

#### Scenario: Prediction without a tracker observation
- **WHEN** a positive result has a valid source prediction but no eligible tracking measurement
- **THEN** it cannot establish overlap agreement or renew confirmation, and the unconfirmed count remains unchanged

#### Scenario: Tracker observation without prediction
- **WHEN** a positive result has an eligible source tracker measurement but lacks a source prediction
- **THEN** overlap verification can proceed without a motion-jump veto

### Requirement: Usable evidence retains overlap thresholds and revokes trust promptly

Boxes SHALL be compared after clipping to their source-image bounds. A unique candidate with IoU at least `iou_reset_threshold` SHALL qualify as overlap agreement. `iou_calibrate_threshold` SHALL remain available for agreement diagnostics. A fresh applicable closing ambiguity or completed-plan disagreement/no-target verdict SHALL revoke permission immediately. A partial-region miss or unrelated candidate SHALL remain neutral while declared coverage is incomplete. `lost_max_count` SHALL govern abandonment of provisional attempts rather than delayed publication loss.

#### Scenario: Metadata-only agreement
- **WHEN** fresh same-image metadata provides a unique qualifying match but source pixels have been evicted
- **THEN** an eligible track can be confirmed without reinitialization or access to those pixels

#### Scenario: Genuine disagreement without recovery pixels
- **WHEN** fresh applicable completed-plan evidence establishes disagreement and the original image is unavailable
- **THEN** permission is revoked, the unconfirmed count advances once, recovery is marked unavailable for that result, and searching continues

#### Scenario: Fifth completed no-target result
- **WHEN** a provisional attempt reaches five distinct eligible adverse plan verdicts or closing ambiguities with `lost_max_count=5`
- **THEN** the attempt is discarded; output has already been invalid since loss of trust, and result five does not initiate the first invalidation

### Requirement: Unavailable recovery cannot authorize tracking

Reinitialization SHALL require the detection's original image. Missing pixels MUST NOT be treated as successful recovery. Jump counters MUST NOT authorize replacement tracks in this version. Resetting an unconfirmed count MUST NOT substitute for initialization, successful catch-up, and later-image verification.

#### Scenario: Jump tolerance reached without images
- **WHEN** legacy diagnostic displacement would have reached the old jump tolerance but the seed image is unavailable
- **THEN** no jump-accept branch runs, no recovery is reported successful, and output cannot become valid from that result

### Requirement: Submission cadence and inference search preserve bounded verification

Searching and provisional acquisition SHALL retain one submission opportunity per processed image. Verified tracking SHALL retain `yolo_interval`. Normal interaction SHALL preserve source snapshot, single result read, current submission or skip, then saved-result arbitration. The dedicated YOLO worker SHALL schedule one synchronous model call at a time between targeted checks and fixed-image discovery stages. Submission SHALL not enqueue a full cascade for each image. The tracking thread MUST NOT synchronously execute or wait for YOLO. Motion stubs MUST NOT introduce jump early returns or extra inference workers.

#### Scenario: Continuous pending replacement
- **WHEN** eligible provisional frames continuously replace a pending request while another request executes
- **THEN** the single worker can take the current pending request without waiting for input to stop changing

#### Scenario: Tracking reads on an update without submission
- **WHEN** verified tracking has a completed result on an image outside the configured submission interval
- **THEN** the result is read once and processed, while the current submission is skipped for the interval reason

#### Scenario: Interval-one configuration offers every processed image
- **WHEN** verified tracking processes valid images with `yolo_interval=1`
- **THEN** each normal update offers its current image under the existing request-admission rules; this does not guarantee acceptance or execution of every image

#### Scenario: A different supported interval remains configurable
- **WHEN** verified tracking uses a valid configuration with `yolo_interval=4`
- **THEN** submission follows that configured interval, while confirmation expiry still uses elapsed evidence age and does not silently extend its deadline

#### Scenario: Current source observation precedes delayed arbitration
- **WHEN** current image B produces a tracking observation before old result A initiates recovery
- **THEN** B's submitted snapshot retains its original observation; recovery evidence for B is stored separately under its new attempt

#### Scenario: Existing early return does not consume output
- **WHEN** input is invalid or its timestamp repeats before the normal detector interaction point
- **THEN** no extra result read or submission is introduced; independent confirmation expiry still runs

#### Scenario: Legacy jump does not create an early return
- **WHEN** a valid current observation exceeds old jump thresholds while motion validation is deferred
- **THEN** the normal detector interaction still occurs and candidate verification is not rejected by a hidden jump rule

### Requirement: Elapsed-time attempt limits govern no-result cleanup

The legacy no-result threshold `counter > lost_max_count * yolo_interval * 3` SHALL have no cleanup authority. Delayed or absent detector results alone MUST NOT clear a provisional track, its boxes, or its eligible comparison measurements before `candidate_timeout_ms`. That timeout SHALL use monotonic elapsed time from seed acceptance and SHALL be serviced by the worker at its first opportunity at or after the deadline. New replies and partial progress MUST NOT restart it.

Independent confirmation expiry SHALL revoke output permission even when input or inference is stalled. The usable-adverse-result budget at `lost_max_count`, genuine execution or geometry failures, and OFF/service shutdown SHALL retain their separate handling. Removing frame-count cleanup MUST NOT permit unbounded image or measurement retention, concurrent model reset, or release of buffers still used by an inference worker. This revision SHALL add no detector-health timeout or worker-restart policy. Cleanup after an already registered trust loss MUST NOT create a second TargetLost event.

#### Scenario: Interval-one delayed result crosses the former boundary
- **WHEN** a structurally valid provisional attempt has no completed result at updates 15 and 16 with `lost_max_count=5` and `yolo_interval=1`, and its candidate deadline has not arrived
- **THEN** no frame-count cleanup occurs, its comparison evidence remains eligible, and a later fresh matching result may verify it before the candidate deadline

#### Scenario: Existing sixty-update boundary
- **WHEN** injected elapsed time stays before the candidate deadline while a provisional attempt receives no result through updates 60 and 61 with `yolo_interval=4`
- **THEN** the attempt is not abandoned for its update count; candidate expiry still occurs at the same elapsed deadline used with interval 1

#### Scenario: Candidate deadline and stalled verified permission
- **WHEN** a provisional attempt reaches its candidate deadline without verification, or a verified instance reaches its confirmation deadline while inference remains blocked
- **THEN** the worker abandons the provisional attempt at its first servicing opportunity, the independent supervisor revokes verified output without waiting for inference, and later cleanup produces no duplicate loss event

### Requirement: Result age constrains confirmation eligibility

Result age is elapsed time from source capture to the decision in a consistent time domain. Positive results at or beyond `confirmation_timeout_ms` SHALL not verify acquisition or renew output permission. Old results SHALL remain diagnosable. They MUST NOT gain freshness from arrival, available pixels, or usable comparison metadata.

While Searching with invalid output and no active replacement, an otherwise eligible positive result with its original image SHALL remain eligible to seed new provisional initialization. This includes a request submitted during an expired or abandoned attempt and an otherwise eligible result too old to confirm output. Its original attempt tag alone MUST NOT disqualify that seed. This seed use MUST NOT revive expired permission, change an existing instance's unconfirmed counter, or replace an active provisional or verified instance. Wrong-round, already disposed, invalid-source-identity, invalid-clock, and missing-original-pixel results MUST NOT use this exception. Later-image verification SHALL still require fresh evidence and a genuine same-image measurement independently produced by the new attempt; original old-attempt comparison boxes MUST NOT substitute.

#### Scenario: Old result with usable context
- **WHEN** a verified instance receives a result with valid source metadata that is 120 ms old under a 100 ms confirmation lifetime
- **THEN** it cannot confirm tracking or restore output permission and is recorded as stale rather than as usable disagreement

#### Scenario: Old seed remains provisional
- **WHEN** searching has invalid output and receives an otherwise eligible positive result that is 120 ms old under a 100 ms lifetime, with its original image still buffered
- **THEN** the result can seed provisional initialization, but cannot authorize output; a fresh detection on a strictly later image must independently verify the new attempt

#### Scenario: Fresh old-attempt result seeds a distinct recovery attempt
- **WHEN** A expires around image 35, previously undisposed D32 arrives while image 42 is processed, its request names A, and its original pixels remain available with no active replacement
- **THEN** the result can seed B under a new attempt identity without reviving A; a later fresh D41 may verify only against B's independently replayed T41, and the seed cannot verify itself

### Requirement: External state values and verified recovery acceptance are explicit

The system SHALL preserve Detecting=0, TargetFound=1, Tracking=2, and TargetLost=3. Initialization alone SHALL remain provisional. Catch-up SHALL require every replayed step to succeed before verification eligibility. Later-image verification SHALL precede TargetFound. Verified-track revocation SHALL remove local permission immediately and register a TargetLost publication attempt. That attempt SHALL precede later recovery-state publications even when delayed. Deferred jump rules MUST NOT cause legacy early reset or jump acceptance.

#### Scenario: Initialization and normal tracking
- **WHEN** a seed detection initializes and catches up successfully, followed by a fresh matching later-image detection
- **THEN** output stays invalid and state Detecting before verification; verification permits TargetFound and then Tracking on the next normal verified update, while publication of either state follows any preceding loss attempt and its ordering checks

#### Scenario: Revocation precedes delayed state publication
- **WHEN** adverse evidence revokes the verified instance while a prior publication call delays access to publication serialization
- **THEN** local permission is removed immediately, the pending TargetLost attempt waits for publication access, and later recovery-state publications cannot overtake it

#### Scenario: Partial replay retains its existing rule
- **WHEN** initialization succeeds but a buffered replay step fails
- **THEN** the legacy partial-success rule is superseded: the attempt is not eligible for verification, output stays invalid, and another attempt is required

#### Scenario: Native tracker jump retains its existing reset path
- **WHEN** a structurally valid measurement exceeds the former NanoTrack jump tolerance
- **THEN** the legacy reset path is superseded while the motion validator is a stub; active structural, overlap, and freshness checks decide eligibility

### Requirement: Saved result has one disposition per update

Each normal tracker update SHALL take at most one immutable stage-result snapshot and dispose that job once. Submitted request, source image/time, original metadata, round, stage-job identity, and parent-plan identity SHALL remain associated. A later result MUST NOT replace the local snapshot or trigger a second read. Parent-plan completion SHALL be finalized separately after the ordered stage dispositions are known; it MUST NOT duplicate an adverse counter action.

#### Scenario: New result cannot replace local snapshot
- **WHEN** A is taken and B completes while the tracking thread arbitrates A
- **THEN** this update disposes only A, B stays protected for a later update, and A's job is not applied twice

#### Scenario: Original provenance and cleanup rules remain applicable
- **WHEN** a saved positive belongs to an old round or lacks pixels/history required for seed recovery
- **THEN** round and seed checks reject that use without substituting current pixels, rewriting original metadata, or inventing recovery

#### Scenario: Completion marker does not double count regional results
- **WHEN** the last stage carries complete coverage after earlier partial misses were disposed
- **THEN** the tracker may finalize one eligible plan verdict after all required comparisons are classified; partial misses do not count as separate adverse results

### Requirement: Round cleanup releases metadata and images safely

Round cleanup SHALL stop dispatch, cancel pending input and discovery plans, and wake mailbox/control waits. It SHALL wait for active device access before releasing image/session storage and clear old results and controls before reuse. In-flight model calls MUST NOT be interrupted by freeing buffers. Repeated frame numbers MUST NOT allow old-round work to affect a new round.

#### Scenario: OFF during execution and replacement
- **WHEN** A executes, C is pending, a plan is paused, and OFF is followed by a new round at frame 1
- **THEN** C and the plan are cancelled, A's device access ends before release, capacity waits terminate safely, and old results/controls cannot affect the new frame 1

## ADDED Requirements

### Requirement: Detector results retain candidates from the successful search stage

Each positive regional result SHALL retain configured-class candidates surviving existing confidence filtering and duplicate suppression in that stage. It SHALL identify its source image, job, parent plan when present, and searched region. Candidates from different stages MUST NOT be presented as one inference. Regional success MUST NOT imply complete search coverage.

#### Scenario: ROI contains two balloons
- **WHEN** the preferred region produces two surviving target-class detections
- **THEN** both candidates are returned with that region's provenance; a unique applicable agreement can close confirmation without completing unrelated regions

### Requirement: Association selects a unique overlap match

Exactly one candidate meeting the same-image IoU threshold SHALL establish agreement. A higher-scoring unrelated candidate MUST NOT displace it. Multiple qualifying matches SHALL be ambiguous. Zero matches in partial coverage SHALL be neutral; completed applicable same-image coverage without agreement SHALL establish disagreement. Initial seed selection SHALL remain highest-score provisional selection until later verification.

#### Scenario: Higher score belongs to another balloon
- **WHEN** A scores 0.88 and uniquely overlaps the source tracker box, while B scores 0.94 elsewhere
- **THEN** A confirms the track; B does not replace A merely because its score is higher

#### Scenario: Multiple plausible matches
- **WHEN** two retained detections meet the configured overlap threshold with the same tracker measurement
- **THEN** no candidate confirms tracking and observations remain or become invalid

### Requirement: Filter predictions use the rectangle convention expected by consumers

A filter prediction `(cx, cy, w, h)` SHALL become rectangle `(cx-w/2, cy-h/2, w, h)` before region generation, source-prediction snapshots, or rectangle comparisons. Conversion SHALL preserve floating precision. Display rounding MUST NOT change the conversion's meaning.

#### Scenario: Prediction conversion and region center
- **WHEN** the filter returns `(100,50,40,20)`
- **THEN** consumers receive top-left rectangle `(80,40,40,20)`, and a doubled region without minimum-size or boundary constraints is `(60,30,80,40)` centered at `(100,50)`

#### Scenario: Same prediction in both metadata and region generation
- **WHEN** a prediction is used to choose a region and retained for later detector comparison
- **THEN** both uses describe the same physical rectangle rather than shifting its center by half its width and height

### Requirement: Detector work remains bounded under continuous input

The detector SHALL retain at most one active model call, one shared latest pending-image mailbox, one fixed-image discovery plan, one unread result, and one result taken by the tracker. Remaining regions SHALL be plan state rather than queued calls. New images MUST NOT automatically restart the fixed-image plan. Image ownership MAY alias across these bounded roles.

#### Scenario: Two images do not enqueue fourteen calls
- **WHEN** F10 starts a whole-image/six-tile plan and newer F11 then F12 arrive before it finishes
- **THEN** F12 replaces unstarted F11; the F10 plan retains its fixed source and next-region position without creating per-image cascade queues

### Requirement: Scheduling gives bounded service to targeted and discovery work

A scheduling quantum SHALL be one completed model call. At a boundary, the worker SHALL apply lifecycle controls, reserve result capacity, remove ineligible work, and select an eligible job. When both targeted and discovery work remain eligible, at most two targeted calls SHALL precede a discovery stage. Serving discovery SHALL reset that burst count. This initial policy MUST NOT extend evidence deadlines.

#### Scenario: Both work types remain ready
- **WHEN** targeted and discovery work continuously remain eligible and result capacity is released
- **THEN** service repeats targeted, targeted, discovery; no seven-stage request monopolizes execution and overload still permits independent expiry

#### Scenario: Only one work type is eligible
- **WHEN** only targeted work or only discovery work is eligible
- **THEN** the worker serves that work without waiting for an empty work type; a targeted burst count stays capped at two until discovery is served

### Requirement: Partial search does not establish whole-image absence

Partial empty or nonmatching regional results SHALL be neutral for permission and adverse counters. Completed no-target/disagreement verdicts SHALL require the declared coverage on one source image and applicable fresh comparison evidence. Errors, cancellation, missing context, or cross-image tile coverage MUST NOT become completed adverse evidence. Closing ambiguity SHALL retain immediate adverse handling.

#### Scenario: Whole-image pass misses a small object
- **WHEN** the declared multiscale plan includes tiles but only its whole-image stage has returned no target
- **THEN** no complete no-target verdict or adverse counter increment occurs; remaining coverage can be scheduled without waiting for full tracker arbitration

#### Scenario: Tiles belong to different source images
- **WHEN** tile 1 was searched on F10 and tile 2 on F11
- **THEN** those records do not combine into completed coverage of either image

### Requirement: Confirmation search continues past unrelated regional positives

Confirmation search SHALL seek a unique eligible same-image agreement. An unrelated positive in partial coverage MUST NOT terminate it or renew permission. Searching without an active instance SHALL pause its discovery plan on a positive seed candidate and retain highest-score seed selection. Every stage SHALL retain separate candidate provenance.

#### Scenario: Later region agrees with the track
- **WHEN** R1 returns unrelated candidate B at 0.94 and R2 returns uniquely overlapping A at 0.88 on the same image
- **THEN** R1 remains partial no-agreement and R2 may confirm; R1's higher score does not hide the matching candidate

### Requirement: Result capacity is released at take rather than full arbitration

The worker SHALL reserve an empty protected result slot before inference. Completion SHALL publish into that slot without overwrite. Taking the result SHALL release capacity before arbitration or catch-up finishes. A full mailbox SHALL stop additional inference without blocking the tracking thread on YOLO. Partial searches MUST NOT require a full tracker-decision barrier after every call.

#### Scenario: Inference overlaps catch-up
- **WHEN** the tracker takes A and starts expensive preparation while the worker completes B
- **THEN** B occupies the mailbox safely; further calls wait for capacity, and A's local snapshot is neither overwritten nor delayed pending B

### Requirement: Seed feedback precedes expensive preparation

A positive Searching result SHALL pause only its discovery plan while seed eligibility is decided. Other eligible work SHALL remain schedulable with capacity. On seed acceptance the tracker SHALL send a short round/attempt/seed/region control before initialization or catch-up. Acceptance SHALL cancel remaining old-plan stages and enable later-image checks. Seed rejection SHALL leave output invalid.

#### Scenario: Positive search result starts provisional B
- **WHEN** the tracker accepts D10 and initialization of B will take longer than another detector call
- **THEN** early acceptance enables a submitted F20 check during preparation; D20 can verify only against B's independently produced T20 and D10 cannot verify its own seed

### Requirement: Phase controls preserve admitted source identity

Scheduling SHALL use the latest admitted image and published phase values. Controls received during a call SHALL apply at the next completed-call boundary. Original source metadata and evidence anchors MUST NOT be rewritten. Queued expired-instance checks SHALL be cancelled; an already-running positive SHALL follow separate seed/confirmation eligibility when consumed.

#### Scenario: Revocation races with a detector call
- **WHEN** A expires while its detector call executes and a replacement has not started
- **THEN** the call finishes safely, cannot renew A, and its eligible first-consumed positive can seed B without changing its original provenance

#### Scenario: Catch-up blocks newer submissions
- **WHEN** capture advances while the tracking thread has submitted no new image during catch-up
- **THEN** the worker uses only eligible submitted images or waits; no camera-current image or renewed anchor is invented

### Requirement: Targeted misses can start one same-image fallback plan

When no discovery plan exists, a targeted source SHALL be retainable in that one slot while classification completes. A definite empty region SHALL enable remaining declared search without a full tracker decision. Nonmatching-candidate feedback SHALL enable it asynchronously. The ledger SHALL reference the immutable initial job. Later misses MUST NOT add plans or mix source-image coverage.

#### Scenario: First miss creates bounded discovery work
- **WHEN** a targeted F20 check finds no candidates and no plan exists, followed by an F21 miss
- **THEN** one F20 fallback can search its remaining regions, F21 cannot append another plan or modify its coverage, and the original F20 job remains immutable

### Requirement: Newer agreement supersedes older discovery evidence

A valid same-attempt agreement on a newer source image SHALL supersede an older discovery plan. Its queued stages SHALL be cancelled at the next call boundary. An in-flight call SHALL finish safely, but no terminal adverse verdict from that superseded plan SHALL revoke the newer permission or change its counter. Supersession and late-result dispositions SHALL be explicit.

#### Scenario: Old no-target completion follows a newer confirmation
- **WHEN** F30 confirms A while an older F20 discovery stage is still executing
- **THEN** remaining F20 stages are cancelled, the running call finishes under its original identity, and its old plan cannot override F30 with no-target or disagreement

### Requirement: Discovery cancellation preserves useful coverage opportunities

New input SHALL replace only unstarted pending input. Plans SHALL be cancelled on OFF, round change, explicit supersession, or lost source/history eligibility for their intended use. A stale Searching positive MAY retain seed eligibility. A restarted plan SHALL reset source coverage and rotate its initial tile after cancellation so repeated restarts do not always omit the same late tiles.

#### Scenario: Normal input does not starve late tiles
- **WHEN** new images arrive during each call of one eligible discovery plan
- **THEN** its declared fixed-image coverage continues; an explicitly cancelled restart uses a new source/ledger and rotates its initial tile

### Requirement: Targeted regions require evidence of detection effectiveness

Targeted checks SHALL use a region policy evaluated with existing models and exact source pixels. Evaluation SHALL compare the current ROI, the previous successful region, and that region's scale/aspect recentered at the same-image measurement. Remembered regions SHALL remain bounded hints without permission authority. Input normalization, model contracts, and formal confidence/lifetime defaults MUST remain unchanged by this crop evaluation.

#### Scenario: Current crop supplies no usable positives
- **WHEN** current ROI checks fail but another region detects the same source workload
- **THEN** region/resize/padding/input/candidate evidence determines the evaluated hint policy; higher submission frequency alone is not reported as improved detection effectiveness

## REMOVED Requirements

### Requirement: Watchdog counting and reset behavior remain unchanged
**Reason**: Detector silence measured in processed updates no longer controls track cleanup. Independent permission expiry and the elapsed provisional-attempt limit provide the applicable time bounds.
**Migration**: Remove the no-result watchdog counting/reset contract and its frame-threshold cleanup branch. Keep `lost_max_count` for distinct usable adverse completed results against a provisional attempt. Preserve historical watchdog diagnostics as historical evidence, bounded resource ownership, genuine failure handling, and safe round/service shutdown.
