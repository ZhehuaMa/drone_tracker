# Spec Delta

## Purpose

Permit valid target observations only while a verified track has sufficiently fresh supporting detection evidence. Keep acquisition and recovery running without presenting provisional or expired tracking as valid visual measurements.

## ADDED Requirements

### Requirement: Track initialization and output permission are distinct

A track instance is the target template and tracking state spanning several images. A tracker measurement is one box and score on one image. The system SHALL keep an initialized but unverified instance provisional. Provisional measurements MUST NOT authorize valid output.

#### Scenario: Initialization finishes before verification
- **WHEN** initialization and buffered replay finish but no qualifying second detection exists
- **THEN** provisional tracking continues with invalid observations; initialization completion alone does not release valid output

### Requirement: First detection starts an acquisition attempt immediately

An acquisition attempt comprises target-template initialization, buffered replay, and verification. The system SHALL start an attempt from a usable seed detection when its source image is available. It MUST NOT wait for the second detection or five unconfirmed results before starting initialization.

#### Scenario: First recovery detection arrives
- **WHEN** output is invalid and a usable detection has its original image available
- **THEN** provisional reinitialization starts without waiting for another detector result or the unconfirmed-result threshold

### Requirement: Second detection verifies the same-image provisional measurement

Verification SHALL require a detection from a strictly later source image than the seed. It SHALL agree uniquely with the provisional measurement on that image. The detection MUST NOT correct or reinitialize that measurement before the comparison. Reusing the seed or its repeated result MUST NOT count as a second detection.

#### Scenario: Verification without circular correction
- **WHEN** D1 seeds a track on F1 and D2 later detects F2
- **THEN** D2 is compared with the track's independently produced T2 on F2, before any D2-based correction; D1 cannot verify itself

### Requirement: Replay permits verification of requests submitted before initialization

The system SHALL retain successful replay measurements with their source image and acquisition-attempt identities. A second detection SHALL remain eligible when its request predates initialization, provided matching replay evidence exists. Advancing beyond that image MUST NOT substitute a newer measurement.

#### Scenario: D2 completes during D1 initialization
- **WHEN** F2 is submitted before D1 initializes the track, replay produces T2, and YOLO completes D2 while initialization is still running
- **THEN** D2 can verify T2 after replay completes, even if tracking has advanced to F3

#### Scenario: Recovery replaces the old source snapshot
- **WHEN** D2 carries an old track's submission snapshot but the recovery attempt has produced a measurement on F2
- **THEN** verification uses the recovery attempt's retained F2 measurement and leaves the original request metadata unchanged

### Requirement: Confirmation renews permission from source capture time

Confirmation is a qualifying detector agreement supporting the current verified track. Permission SHALL expire at the confirming image's mapped capture time plus `confirmation_timeout_ms`. Result arrival, tracker updates, prediction, counter reset, and publication MUST NOT move that deadline. At the deadline, permission SHALL be expired.

#### Scenario: Slow positive detection
- **WHEN** a confirming image was captured at 1000 ms, its result is consumed at 1080 ms, and the timeout is 100 ms
- **THEN** permission expires at 1100 ms, not 1180 ms; a valid publication attempted at 1100 ms is rejected

### Requirement: Evidence age is independent of observation reference time

Evidence age SHALL be checked against actual decision or publication time in a consistent clock domain. Choosing capture time or pose time as the observation reference MUST NOT make old evidence fresh. A future source timestamp or unresolvable clock discontinuity MUST NOT authorize valid output.

#### Scenario: Capture-time output hides elapsed processing time
- **WHEN** output describes image time 1000 ms but publication occurs at 1120 ms with a 100 ms confirmation lifetime
- **THEN** confirmation from 1000 ms is expired even though the output reference still equals 1000 ms

### Requirement: Standalone replay retains immutable evidence admission anchors

Standalone admission is when a prepared replay image enters tracking after its scheduled wait. Each image SHALL retain that monotonic time as its synthetic evidence anchor. Its age SHALL equal monotonic decision time minus that anchor. Its confirmation deadline SHALL equal that anchor plus the configured lifetime. Initialization, inference, and stalls SHALL consume this lifetime. Later images, result arrival, and replay MUST NOT replace an existing image's anchor.

#### Scenario: Slow update precedes the next replay image
- **WHEN** F1 at source time 1000 ms is admitted at monotonic time 5000 ms, its update finishes at 5120 ms, and a 50 FPS replay admits F2 at source time 1020 ms at monotonic time 5140 ms
- **THEN** a decision at 5170 ms sees F1 age 170 ms and F2 age 30 ms; with a 100 ms lifetime, F2's deadline is 5240 ms and F1 cannot confirm output

#### Scenario: Stall expires retained replay evidence
- **WHEN** F2's immutable admission anchor is 5140 ms and processing or publication stalls until 5240 ms under a 100 ms lifetime
- **THEN** F2 cannot authorize output, even if its result has just arrived or a later image is admitted; replaying F2 during catch-up does not refresh its anchor

### Requirement: Synthetic replay timing does not replace production capture age

Standalone replay SHALL retain source timestamps for ordering and observation references. Its evidence clock SHALL be identified as synthetic. Production and independently timed regression input SHALL include pre-admission age in their capture-to-monotonic relation. Standalone decoding and scheduled waits before admission MUST NOT be reported as camera capture age. Synthetic replay MUST NOT establish production timing acceptance.

#### Scenario: Production frame arrives late
- **WHEN** a production image is already 80 ms old at tracker admission and its detection consumes another 30 ms under a 100 ms lifetime
- **THEN** its evidence age is 110 ms and it cannot confirm output; the standalone admission convention does not reduce that age to 30 ms

### Requirement: First usable adverse evidence revokes permission

The system SHALL revoke permission on the first usable same-image detector disagreement, ambiguous association, completed no-target search, tracking execution failure, invalid measurement geometry, or confirmation expiry. It MUST NOT wait for a counter threshold. Missing comparison context SHALL neither confirm nor independently demonstrate disagreement.

#### Scenario: First disagreement with count below five
- **WHEN** the current verified track receives a usable disagreement while its unconfirmed count is zero
- **THEN** output becomes invalid and recovery begins immediately; count five is not a prerequisite

#### Scenario: Missing comparison data
- **WHEN** a positive result has no eligible source measurement and the current confirmation has not expired
- **THEN** it does not renew permission or increment the unconfirmed count; the existing deadline continues unchanged

### Requirement: Recovery and its individual attempts are distinct

Recovery is the process after a verified track loses trust. It SHALL begin at revocation. It SHALL search when no usable seed image exists. A seed with source pixels SHALL start a provisional attempt without waiting for a counter threshold. Successful verification SHALL be required to end recovery and restore valid output.

#### Scenario: Disagreement supplies a recovery seed
- **WHEN** a disagreement revokes the verified track and its detection has a usable source image
- **THEN** that detection can seed immediate provisional reinitialization but cannot also serve as the attempt's second verification

### Requirement: Unconfirmed counter manages unsuccessful attempts

The YOLO unconfirmed counter SHALL count distinct usable disagreements and completed no-target results against the active track instance. Missing context, stale results, repeated results, and absent replies SHALL not increment it. Verified confirmation SHALL clear it. Reaching `lost_max_count` SHALL abandon an unsuccessful provisional attempt, not delay revocation or recovery entry.

#### Scenario: Five-result attempt budget
- **WHEN** a provisional attempt accumulates five unconfirmed results with `lost_max_count=5`
- **THEN** that attempt is discarded and searching resumes; output was already invalid before result five

### Requirement: Counter reset cannot revive an abandoned instance

A new attempt SHALL start with its own zero unconfirmed count. Round reset SHALL clear the count. Abandonment SHALL invalidate the attempt identity before accepting another seed. Counter clearing alone MUST NOT authorize output or extend a confirmation deadline.

#### Scenario: Retry after exhausted budget
- **WHEN** attempt A exhausts its budget and attempt B starts
- **THEN** B starts at zero, an A result cannot change B's count or permission, and B remains provisional until separately verified

### Requirement: Jump counters have no authority while motion validation is deferred

The YOLO jump counter and NanoTrack jump counter SHALL remain inactive and reset in this version. Neither jump-tolerance acceptance nor jump-triggered early reset SHALL gate acquisition, verification, or publication. Existing jump parameters SHALL be accepted for configuration compatibility and reported as inactive.

#### Scenario: Large displacement with matching detector evidence
- **WHEN** an otherwise eligible same-image measurement exceeds the legacy pixel-jump threshold
- **THEN** no jump counter rejects it or accepts a replacement; verification follows active overlap, freshness, identity, and structural checks

### Requirement: Annotation-dependent validators are nonblocking stubs

Appearance-score and motion-limit validation SHALL return allow with diagnostic status `not_evaluated`. They MUST NOT reject, delay, or withhold verification. YOLO's existing class-confidence filtering SHALL remain active. Tracking execution failures and non-finite model output SHALL remain structural failures rather than appearance-threshold decisions.

#### Scenario: Weak positive NanoTrack score
- **WHEN** a structurally valid candidate has a low positive NanoTrack score but passes active verification rules
- **THEN** the score stub does not block verification and is recorded as not evaluated

#### Scenario: No manual annotations available
- **WHEN** controlled acceptance is run without manually labeled targets
- **THEN** acquisition, timeout, invalidation, and recovery contracts can be checked without enabling the deferred validators

### Requirement: Attempt timeout is separate from confirmation lifetime

A provisional attempt SHALL expire after `candidate_timeout_ms` of monotonic elapsed time from accepting its seed. Initialization completion and partial detector progress MUST NOT restart that timeout. Timeout SHALL abandon the attempt and keep observations invalid. Waiting for a detector MUST NOT block initialization or normal provisional updates.

#### Scenario: Missing second detection
- **WHEN** initialization completes at 200 ms but the attempt reaches its 1000 ms limit without verification
- **THEN** the attempt is discarded at the first servicing opportunity at or after its deadline, and searching continues with invalid output

### Requirement: Trusted motion state excludes rejected measurements

Only measurements accepted under an authorized track SHALL update the control-facing motion state. Provisional state SHALL remain separate. Rejection after an internal tracker update SHALL mark that track untrusted. Successful recovery verification SHALL initialize trusted motion state without velocity inherited from the rejected track.

#### Scenario: Suspect update precedes delayed disagreement
- **WHEN** a current NanoTrack update runs before an older usable disagreement is consumed
- **THEN** that update is not committed to trusted motion state or published as valid after revocation

### Requirement: Independent expiry survives inference and input stalls

RK3576 observation permission SHALL be checked independently of image-processing completion. The expiry callback SHALL revoke permission at its first servicing opportunity at or after the deadline. It MUST NOT wait for model execution, catch-up, or `consumed=true`. Every valid publication SHALL independently recheck expiry.

#### Scenario: Blocked inference at deadline
- **WHEN** inference remains blocked across the confirmation deadline and the expiry callback is serviced
- **THEN** permission is revoked and a paired invalid notification is attempted without waiting for inference to finish

### Requirement: Round and attempt identities protect delayed work

Every provisional track and retained verification measurement SHALL identify its round and attempt. Results associated only with an abandoned attempt MUST NOT confirm or revoke a replacement. An undisposed preinitialization request SHALL be eligible for explicit reassociation only when its image is later than the new seed and has matching replay evidence. Original request metadata SHALL remain unchanged.

#### Scenario: Old result arrives after recovery
- **WHEN** B has replaced A and an A-associated result without eligible B replay evidence is later consumed
- **THEN** B's permission, confirmation deadline, and counter remain unchanged

### Requirement: Stale results cannot act as current adverse evidence

Result eligibility SHALL be checked separately for seeding and changes to permission or counters. A stale result has source age at or beyond `confirmation_timeout_ms`. Stale results MUST NOT verify acquisition, renew or independently revoke permission, or change an existing instance's unconfirmed counter. Already disposed, wrong-round, and inapplicable-attempt results SHALL remain diagnostic only.

#### Scenario: Delayed but still fresh mismatch
- **WHEN** D1 describes 1000 ms, is consumed at 1090 ms, belongs to the applicable instance, and the confirmation lifetime is 100 ms
- **THEN** its same-image disagreement can revoke permission even if a valid observation was published at 1080 ms

#### Scenario: Stale negative result
- **WHEN** a completed no-target result is 120 ms old under a 100 ms confirmation lifetime
- **THEN** it neither increments the current count nor independently revokes a newer eligible confirmation; the current instance's own deadline remains in force

### Requirement: Old positive results can seed only provisional work

While searching with invalid output, an otherwise eligible stale positive result SHALL remain eligible to seed a provisional attempt with its original pixels available. This exception MUST NOT authorize output or replace an active verified or provisional instance. The attempt SHALL start with the normal zero unconfirmed count and seed-acceptance timeout. Fresh later-image verification SHALL remain necessary to authorize output.

#### Scenario: Old seed followed by fresh later-image verification
- **WHEN** searching accepts D1 captured at 1000 ms at decision time 1120 ms with original F1 pixels available, `confirmation_timeout_ms=100`, and `candidate_timeout_ms=1000`, then consumes D2 captured at 1150 ms at decision time 1180 ms with a unique same-image match to independently produced T2
- **THEN** D1 starts only provisional work with an attempt deadline of 2120 ms, and D2 verifies that attempt with a confirmation deadline of 1250 ms; D1 does not authorize output or supply its confirmation time

#### Scenario: Stale positive cannot displace a current instance
- **WHEN** an otherwise eligible positive result is 120 ms old under a 100 ms lifetime and a verified or provisional instance is still active
- **THEN** the old result does not replace that instance, change its permission or counter, or reset its attempt timeout

#### Scenario: Old seed has no original pixels
- **WHEN** searching receives an old positive result with source metadata but its original image has been evicted
- **THEN** no provisional initialization starts from that result, no different image substitutes for it, and searching continues with invalid output

### Requirement: Seed age exception preserves identity and clock checks

The stale-seed exception SHALL retain source identity, result disposition, geometry, and clock-relation checks. Wrong-round, already disposed, and inapplicable-attempt results MUST NOT seed an attempt. Future source timestamps and unresolvable clock discontinuities MUST NOT use the exception.

#### Scenario: Old positive belongs to the previous round
- **WHEN** searching receives an old positive result with available pixels but its request belongs to a previous round
- **THEN** the result remains diagnostic only and cannot seed the current round

#### Scenario: Source time cannot establish elapsed age
- **WHEN** a positive result has a future source timestamp or an unresolvable source-clock discontinuity
- **THEN** the result cannot use the stale-seed exception to start an attempt or authorize output

### Requirement: Public states distinguish loss from provisional acquisition

The system SHALL preserve `Detecting=0`, `TargetFound=1`, `Tracking=2`, and `TargetLost=3`. Searching and provisional phases SHALL report Detecting. Verified phases SHALL report TargetFound, then Tracking on the next normal update. Revocation SHALL remove local permission immediately and register one TargetLost publication attempt. That ordered attempt MAY be delayed but SHALL precede later recovery-state publications. OFF and device failures MUST NOT be relabeled algorithmic loss.

#### Scenario: Recovery verification pending
- **WHEN** a verified track loses trust and its replacement initializes successfully but awaits D2
- **THEN** local permission is immediately revoked and valid output remains withheld; the TargetLost publication attempt precedes subsequent Detecting recovery publication, even if notification is delayed

#### Scenario: Publication is delayed after local revocation
- **WHEN** local permission is revoked at 1090 ms but publication serialization is unavailable until 1161 ms
- **THEN** no new valid pair is admitted under revoked permission, the TargetLost attempt occurs at the later publication opportunity, and diagnostics retain revocation and publication times separately without claiming subscriber receipt at 1090 ms

### Requirement: Deadline settings are explicit and reproducible

The system SHALL expose startup-only positive integer `tracker.confirmation_timeout_ms`, `tracker.candidate_timeout_ms`, and `tracker.expiry_check_interval_ms`. Defaults SHALL be 100, 1000, and 10 respectively. The check interval SHALL not exceed the confirmation timeout. Reports SHALL retain effective settings. These defaults MUST NOT be described as established flight-safe limits.

#### Scenario: Invalid startup or runtime edit
- **WHEN** a timeout is zero, the check interval exceeds confirmation lifetime, or a runtime timeout change is requested
- **THEN** startup rejects the invalid setting or the runtime edit is refused without partially updating the active policy

### Requirement: Verification and revocation are observable

Diagnostics SHALL identify source frame, round, attempt, detector request, comparison origin, and verification outcome. They SHALL distinguish capture time, detector consumption time, output reference time, revocation time, and publication attempt time. They SHALL report counter actions, inactive jump checks, and `not_evaluated` validators without claiming tracking accuracy.

#### Scenario: Delayed mismatch diagnosis
- **WHEN** D1 from 1000 ms disagrees at 1090 ms after an observation at 1080 ms
- **THEN** evidence records all three epochs and the revocation event; it does not claim to retract the earlier observation

### Requirement: Missing-vision policy does not control the mission

This change SHALL use explicit invalid observations as its downstream loss signal. It MUST NOT send `DISMISS`, `OFF`, `MISSION`, or fabricated target measurements to alter controller behavior. Acceptance SHALL state that the unchanged controller can continue prediction-based guidance after invalid vision.

#### Scenario: Vision loss during interception
- **WHEN** the tracker revokes permission during an active mission
- **THEN** invalid bbox and LOS are published and recovery continues without mission commands or edits to the controller
