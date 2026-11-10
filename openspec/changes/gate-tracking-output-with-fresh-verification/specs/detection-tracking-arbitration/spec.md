# Spec Delta

## RENAMED Requirements

- FROM: `### Requirement: Usable evidence retains existing arbitration thresholds`
- TO: `### Requirement: Usable evidence retains overlap thresholds and revokes trust promptly`
- FROM: `### Requirement: Unavailable recovery cannot become successful jump acceptance`
- TO: `### Requirement: Unavailable recovery cannot authorize tracking`
- FROM: `### Requirement: Submission cadence and inference search stay unchanged`
- TO: `### Requirement: Submission cadence and inference search preserve bounded verification`
- FROM: `### Requirement: Watchdog threshold and timeout action remain unchanged`
- TO: `### Requirement: Frame-count watchdog governs internal cleanup`
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

Boxes SHALL be compared after clipping to their source-image bounds. A unique candidate with IoU at least `iou_reset_threshold` SHALL qualify as overlap agreement. `iou_calibrate_threshold` SHALL remain available for agreement diagnostics. A usable disagreement or completed no-target result SHALL revoke permission immediately. `lost_max_count` SHALL govern abandonment of provisional attempts rather than delayed publication loss.

#### Scenario: Metadata-only agreement
- **WHEN** fresh same-image metadata provides a unique qualifying match but source pixels have been evicted
- **THEN** an eligible track can be confirmed without reinitialization or access to those pixels

#### Scenario: Genuine disagreement without recovery pixels
- **WHEN** usable source evidence shows no matching candidate and the original image is unavailable
- **THEN** permission is revoked, the unconfirmed count advances once, recovery is marked unavailable for that result, and searching continues

#### Scenario: Fifth completed no-target result
- **WHEN** a provisional attempt reaches five unconfirmed completed results with `lost_max_count=5`
- **THEN** the attempt is discarded; output has already been invalid since loss of trust, and result five does not initiate the first invalidation

### Requirement: Unavailable recovery cannot authorize tracking

Reinitialization SHALL require the detection's original image. Missing pixels MUST NOT be treated as successful recovery. Jump counters MUST NOT authorize replacement tracks in this version. Resetting an unconfirmed count MUST NOT substitute for initialization, successful catch-up, and later-image verification.

#### Scenario: Jump tolerance reached without images
- **WHEN** legacy diagnostic displacement would have reached the old jump tolerance but the seed image is unavailable
- **THEN** no jump-accept branch runs, no recovery is reported successful, and output cannot become valid from that result

### Requirement: Submission cadence and inference search preserve bounded verification

Searching and provisional acquisition SHALL retain one submission opportunity per processed image. Verified tracking SHALL retain `yolo_interval`. Normal interaction SHALL preserve source snapshot, single result read, current submission or skip, then saved-result arbitration. The existing region, whole-image, and tiled cascade SHALL remain. Motion stubs MUST NOT introduce jump early returns or extra inference workers.

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

### Requirement: Frame-count watchdog governs internal cleanup

The existing strict threshold `counter > lost_max_count * yolo_interval * 3` SHALL remain an internal no-result cleanup rule. It SHALL clear the abandoned track state and watchdog count. Independent time-based permission expiry SHALL apply even before that threshold. Cleanup of an already invalid track MUST NOT create another TargetLost transition or authorize worker restart.

#### Scenario: Existing sixty-update boundary
- **WHEN** `lost_max_count=5`, `yolo_interval=4`, and an initialized attempt receives no results for 61 updates
- **THEN** the frame-count cleanup does not fire at update 60 and fires at update 61; output can have expired earlier, without another loss event at cleanup

#### Scenario: Interval-one fifteen-update boundary
- **WHEN** `lost_max_count=5`, `yolo_interval=1`, and an initialized attempt receives no results for 16 updates
- **THEN** the frame-count cleanup does not fire at update 15 and fires at update 16; it does not replace elapsed-time confirmation expiry

### Requirement: Result age constrains confirmation eligibility

Result age is elapsed time from source capture to the decision in a consistent time domain. Positive results at or beyond `confirmation_timeout_ms` SHALL not verify acquisition or renew output permission. Old results SHALL remain diagnosable. They MUST NOT gain freshness from arrival, available pixels, or usable comparison metadata.

While searching with invalid output, an otherwise eligible old positive result with its original image SHALL remain eligible to seed provisional initialization. This seed use MUST NOT independently revoke a verified instance, change an existing instance's unconfirmed counter, or replace an active provisional attempt. Wrong-round, already disposed, and inapplicable-attempt results MUST NOT use this exception. Later-image verification SHALL still require fresh evidence and a genuine same-image measurement from the new attempt.

#### Scenario: Old result with usable context
- **WHEN** a verified instance receives a result with valid source metadata that is 120 ms old under a 100 ms confirmation lifetime
- **THEN** it cannot confirm tracking or restore output permission and is recorded as stale rather than as usable disagreement

#### Scenario: Old seed remains provisional
- **WHEN** searching has invalid output and receives an otherwise eligible positive result that is 120 ms old under a 100 ms lifetime, with its original image still buffered
- **THEN** the result can seed provisional initialization, but cannot authorize output; a fresh detection on a strictly later image must independently verify the new attempt

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

## ADDED Requirements

### Requirement: Detector results retain candidates from the successful search stage

Each positive result SHALL retain the configured-class detections surviving existing confidence filtering and duplicate suppression in its first successful search stage. It SHALL identify their source image and searched region. Keeping candidates MUST NOT imply every stage or every image region was searched.

#### Scenario: ROI contains two balloons
- **WHEN** the preferred region produces two surviving target-class detections
- **THEN** both candidates are returned with that region's provenance; the existing cascade need not run whole-image inference for the same request

### Requirement: Association selects a unique overlap match

Association determines which detection supports the tracked target. Exactly one candidate meeting the same-image IoU threshold SHALL establish agreement. A higher-scoring unrelated candidate MUST NOT displace that match. Multiple qualifying matches SHALL be ambiguous. Zero matches with usable tracking context SHALL be disagreement. Initial seed selection SHALL remain highest-score provisional selection until later verification.

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
