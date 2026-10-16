# Detection and Tracking Arbitration

## Purpose

Keep delayed detections associated with the frame they describe, so image-history eviction does not create an invalid comparison or pretend that recovery was performed. Keep queued detector input current while preserving bounded concurrency and existing watchdog behavior.

## ADDED Requirements

### Requirement: Source comparison data survives image eviction

Metadata means value-only frame identity, capture time, source tracker box, source Kalman prediction, and their validity flags. An outstanding request is accepted and pending, executing, or awaiting result disposition. The system SHALL retain its source metadata until disposition. Metadata MUST NOT retain image resources.

#### Scenario: Original image has left history
- **WHEN** request 62 for frame 744 remains outstanding while image history advances to frames 748–777
- **THEN** frame 744 metadata remains available for result arbitration, and its metadata does not keep its image alive

### Requirement: Arbitration compares boxes from the same source frame

The system SHALL compare a positive detection only with valid comparison boxes associated with that request's source frame and tracking round. A missing source box MUST NOT be replaced by a current or unrelated previous-frame box.

#### Scenario: Current position differs from source position
- **WHEN** a delayed detection overlaps its source tracker box by 0.584 but overlaps the current box by 0
- **THEN** arbitration uses 0.584 for the overlap test and records the source-frame identity

#### Scenario: Neither comparison basis exists
- **WHEN** a positive detection has no valid source tracker box and no valid source prediction
- **THEN** arbitration records unavailable comparison context without fabricating a zero-overlap or jump decision

### Requirement: Missing context is neutral for unconfirmed-result counting

An unconfirmed-result counter records usable detector evidence that does not confirm tracking. A positive result without the required source comparison context SHALL leave that counter unchanged. Missing context alone MUST NOT confirm tracking or cause a target-loss transition.

#### Scenario: Missing metadata at four unconfirmed results
- **WHEN** the counter is 4 and a positive result cannot be compared because source context is unavailable
- **THEN** the counter remains 4, and this absence alone neither clears tracking nor resets the counter to 0

### Requirement: Partial comparison data has explicit eligibility

While Tracking, IoU arbitration SHALL require a valid source tracker box. A source prediction alone MUST NOT confirm tracking. If only the source prediction is absent, the system SHALL retain the existing tracker-only jump comparison.

#### Scenario: Prediction without a tracker observation
- **WHEN** a positive result has a valid source prediction but no source tracker observation during Tracking
- **THEN** it cannot establish IoU agreement, and the unconfirmed-result counter remains unchanged

#### Scenario: Tracker observation without prediction
- **WHEN** a positive result has a valid source tracker box but no source prediction
- **THEN** its source IoU and existing tracker-only jump check remain available

### Requirement: Recovery requires source images

Recovery means initializing on the detection's source image and replaying subsequent buffered images. The system SHALL require the source image before attempting recovery. Missing pixels MUST NOT be reported as an attempted or successful initialization.

#### Scenario: Metadata exists but recovery image is gone
- **WHEN** a positive detection has valid source metadata but its source image has been evicted
- **THEN** historical comparison remains available, recovery is recorded as unavailable, and no initialization is attempted on a different image

#### Scenario: Detecting cannot initialize from metadata
- **WHEN** the system is Detecting and receives a positive result whose source image is absent
- **THEN** it remains Detecting and continues offering eligible detector input

### Requirement: Usable evidence retains existing arbitration thresholds

With valid source context, the system SHALL retain the configured overlap and jump thresholds. Genuine disagreement and completed no-target detections SHALL retain their existing unconfirmed-result treatment. Image absence alone MUST NOT increment that counter.

#### Scenario: Metadata-only agreement
- **WHEN** NanoTrack is healthy, the source comparison passes the existing agreement rules, and only source pixels are missing
- **THEN** tracking continues under the existing agreement rules without requiring initialization

#### Scenario: Genuine disagreement without recovery pixels
- **WHEN** source metadata proves overlap below the reset threshold, no jump branch takes precedence, and the source image is absent
- **THEN** the existing low-overlap unconfirmed increment still occurs, and unavailable recovery is recorded separately

#### Scenario: Fifth completed no-target result
- **WHEN** five consecutive completed no-target results reach the configured loss limit of 5
- **THEN** the system clears tracking and emits TargetLost under the existing loss rule

### Requirement: Unavailable recovery cannot become successful jump acceptance

A detection jump is a position or size change identified by the existing source-frame checks. When accepting a repeated jump requires recovery but its source image is missing, the system MUST NOT report successful recovery or clear the unconfirmed-result counter as if recovery succeeded.

#### Scenario: Jump tolerance reached without images
- **WHEN** repeated source-comparable detection jumps reach the acceptance threshold but the source image is unavailable
- **THEN** recovery remains unavailable, the result remains unconfirmed, and the existing loss limit still applies

### Requirement: New eligible input replaces only an unstarted request

Eligible input means an input passing existing validation at an existing submission opportunity. The system SHALL replace an occupied pending request with newer eligible input. It SHALL keep at most one pending request. It MUST NOT modify an executing request or overwrite an unread completed result as part of replacement.

#### Scenario: Latest pending input wins
- **WHEN** A executes, B is pending, and newer eligible C is offered without an unread-result admission block
- **THEN** B is superseded, C becomes pending, and A continues with its original input

#### Scenario: Invalid replacement preserves useful work
- **WHEN** B is pending and proposed replacement C fails input validation
- **THEN** C is rejected and B remains pending

#### Scenario: Completed result stays protected
- **WHEN** a completed result is unread and a new submission encounters the existing unread-result admission rule
- **THEN** the completed result remains readable and admission retains that protection

### Requirement: Request replacement preserves its input association

Each accepted request SHALL keep its image, capture time, source identity, search region, and metadata association consistent. The worker SHALL take a complete request. A superseded request SHALL have an explicit terminal disposition.

#### Scenario: Replacement races with worker pickup
- **WHEN** replacement C and pickup of pending B occur concurrently
- **THEN** the worker receives either complete B or complete C, no mixed input is used, and any superseded request is accounted for exactly once

### Requirement: Submission cadence and inference search stay unchanged

The system SHALL retain every-frame submission opportunities while Detecting and the configured processed-frame interval while Tracking. It SHALL retain the existing region, full-image, and tile search cascade. Replacement MUST NOT introduce an additional inference worker or wait for input to stop changing.

#### Scenario: Continuous pending replacement
- **WHEN** new eligible frames continue arriving while an executing request finishes
- **THEN** the worker can pick up the current pending input without waiting for a quiet period

### Requirement: Round cleanup releases metadata and images safely

A tracking round is one ON-to-OFF processing period. Round cleanup SHALL cancel pending requests and wait for executing frame access to finish. It SHALL clear old results and retained metadata before reuse. Results MUST NOT cross rounds with repeated frame identifiers.

#### Scenario: OFF during execution and replacement
- **WHEN** A executes and C is pending at OFF, followed by a new round starting at frame 1
- **THEN** C is cancelled, A finishes its resource access before cleanup returns, old metadata is released, and old results cannot affect new frame 1

### Requirement: Watchdog counting and reset behavior remain unchanged

The watchdog counts initialized-tracker updates without a newly read detector result. The system SHALL increment that counter only in the existing no-result branch. Any result read in the tracking branch SHALL reset it before arbitration, including a result with unavailable source context.

#### Scenario: Unusable reply still resets the current watchdog
- **WHEN** the no-result counter is 60 and a positive detector result is read with unavailable source metadata
- **THEN** the watchdog counter becomes 0 even though the result cannot confirm tracking

### Requirement: Watchdog threshold and timeout action remain unchanged

The watchdog SHALL retain the strict condition counter > lost_max_count * yolo_interval * 3. Timeout SHALL clear tracking initialization and boxes, reset the counter, and resume Detecting on the next processed frame. It MUST NOT gain an elapsed-time limit, a worker restart, or an additional TargetLost transition.

#### Scenario: Existing sixty-update boundary
- **WHEN** lost_max_count is 5, yolo_interval is 4, and no result is read during 61 consecutive initialized-tracker updates
- **THEN** update 60 does not trigger timeout, update 61 performs the existing timeout action, and the next processed frame enters Detecting

### Requirement: Result age remains observational

Result age means the elapsed time from source capture to result consumption on a consistent time basis. The system SHALL preserve capture time and record available age evidence. This change MUST NOT add a maximum result-age rejection rule.

#### Scenario: Old result with usable context
- **WHEN** a result is old but belongs to the current round and has valid source comparison context
- **THEN** elapsed age alone does not reject it

### Requirement: External state meanings and recovery acceptance remain compatible

The system SHALL retain Detecting=0, TargetFound=1, Tracking=2, and TargetLost=3. It SHALL retain ROS interfaces, ordinary loss transitions, and synchronous recovery acceptance outside explicit missing-context handling.

#### Scenario: Initialization and normal tracking
- **WHEN** a usable positive detection has source images and initialization succeeds under the existing recovery acceptance rule
- **THEN** the output enters TargetFound and the next normal initialized update enters Tracking

#### Scenario: Partial replay retains its existing rule
- **WHEN** initialization succeeds and some replay steps fail but the existing recovery rule still accepts the operation
- **THEN** the change does not silently replace that rule with an all-steps-must-succeed requirement

#### Scenario: Native tracker jump retains its existing reset path
- **WHEN** consecutive NanoTrack jumps reach the existing tolerance
- **THEN** the existing early-return reset path remains in effect without adding a TargetLost publication
