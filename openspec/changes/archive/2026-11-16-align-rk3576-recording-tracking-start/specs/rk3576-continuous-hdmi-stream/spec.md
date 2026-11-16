# rk3576-continuous-hdmi-stream Spec Delta

## ADDED Requirements

### Requirement: Initial algorithm inputs have matching recording acceptance

For SC233HGS continuous preview with video recording enabled, startup SHALL admit only frames accepted by the current recording request until the first successful tracker initialization. Startup spans ON through that initialization. Admission is a per-frame input filter, not a pause in video recording. Other input modes SHALL retain their existing startup behavior.

#### Scenario: Recorder preparation trails ON
- **WHEN** ON occurs, frames 8884 through 8891 arrive during preparation, and recording accepts frame 8892
- **THEN** no initial algorithm input comes from 8884 through 8891
- **AND** detection can start from 8892 or a later frame accepted by that same request

#### Scenario: Readiness changes while an old frame is queued
- **WHEN** a frame captured without acceptance remains queued after the recorder becomes ready
- **THEN** that frame cannot become an initial algorithm input

#### Scenario: Latest task replaces a pending task
- **WHEN** an accepted startup task is replaced before consumption
- **THEN** the replacement must independently satisfy the current request's acceptance rule

### Requirement: Startup filtering ends on successful tracker initialization

YOLO detects a box; NanoTrack initializes local tracking from that box's historical image. The startup filter SHALL remain active through negative detections, positive results without initialization, and failed initialization attempts. It SHALL end after a successful current-round initialization. Subsequent steady-state processing SHALL retain the existing scheduling and recording-drop policy.

#### Scenario: First detector request finds nothing
- **WHEN** YOLO finds no target in recorded frame 100 and recording rejects frame 105
- **THEN** startup filtering remains active and frame 105 is withheld from the algorithm

#### Scenario: Positive detection cannot initialize the tracker
- **WHEN** YOLO detects a target but tracker initialization fails
- **THEN** subsequent initial detection inputs continue to require recording acceptance

#### Scenario: Initialization succeeds from historical input
- **WHEN** a result from accepted frame 110 successfully initializes tracking while frame 118 is being processed
- **THEN** the filter ends for that round and the initialization source is frame 110

#### Scenario: A later recovery follows target loss
- **WHEN** the round already completed its first initialization and later loses the target
- **THEN** this startup filter is not reactivated for recovery

### Requirement: Startup filtering preserves preview and video independence

While waiting for the first eligible algorithm input, healthy capture, unannotated HDMI preview, and telemetry SHALL continue. Accepted frames SHALL continue recording independently of target presence. After algorithm processing begins, the existing pairing of consumed images with their own results SHALL remain in effect. Withheld frames MUST NOT create algorithm observations or debug images.

#### Scenario: Preparation takes 150 milliseconds
- **WHEN** ON starts a controlled 150 ms preparation delay
- **THEN** preview and telemetry continue while detection waits for an accepted input

#### Scenario: Recording continues without a target
- **WHEN** recording accepts frames for ten seconds without a successful detection
- **THEN** those frames enter ordinary video while eligible detection requests continue

#### Scenario: Recording rejects a candidate after pairing begins
- **WHEN** initial detection is active and a newly captured frame is rejected by recording
- **THEN** that candidate produces no algorithm observation or debug image
- **AND** a previous box is not overlaid onto its newer preview pixels

### Requirement: Recording failure releases startup filtering for the current round

A terminal recording failure or explicit unavailability SHALL release startup filtering for the current round. Healthy tracking SHALL then continue from its current live dispatch boundary. The system SHALL report that recording coverage is unavailable. It MUST NOT retroactively accept earlier frames or use a stale request's terminal status.

#### Scenario: Preparation times out
- **WHEN** the current request reaches its recording preparation failure boundary
- **THEN** tracking can proceed without waiting for recording and the failure is reported

#### Scenario: Old worker prevents another recording session
- **WHEN** current-round recording is unavailable because old recording resources remain unreclaimed
- **THEN** tracking starts through the existing failure-isolation path with unavailable coverage

#### Scenario: Worker fails after some accepted detection inputs
- **WHEN** recording fails before the round's first tracker initialization
- **THEN** filtering is released and the round cannot claim complete startup video coverage

### Requirement: OFF invalidates startup eligibility immediately

OFF SHALL invalidate the current round's startup admission before returning from control handling. It SHALL discard unconsumed startup tasks. Old preparation completions, receipts, and inference outputs MUST NOT reopen that round or authorize another. Startup waiting MUST NOT add a wait for inference or storage to OFF.

#### Scenario: OFF arrives during recording preparation
- **WHEN** OFF closes a waiting round and that recorder subsequently becomes ready
- **THEN** it cannot submit an algorithm input and healthy preview continues

#### Scenario: Rapid restart receives an older accepted receipt
- **WHEN** ON, OFF, and ON create successive request generations in one stream
- **THEN** only receipts matching the active round and request generation authorize startup inputs

#### Scenario: OFF display response
- **WHEN** OFF arrives during startup filtering on a healthy display path
- **THEN** the existing 500 ms clear-and-off display requirement remains satisfied

### Requirement: Startup acceptance is verified by exact saved-frame membership

A healthy completed recording SHALL contain the first executed YOLO input and the first successful tracker-initialization source when those events occurred under startup filtering. Verification SHALL compare exact session and frame identities with saved frame metadata and video readback. A successful build or aggregate zero-drop counter MUST NOT substitute for this check.

#### Scenario: Normal completed round
- **WHEN** a healthy round executes YOLO on frame 100 and first initializes from frame 110
- **THEN** both exact source frames are present in that session's metadata and readable video

#### Scenario: Initialization never occurs
- **WHEN** a normally completed recording contains negative detector requests but no initialization
- **THEN** first-YOLO coverage is checked and initialization coverage is explicitly not applicable

#### Scenario: Production evidence is collected
- **WHEN** the identified HZ-RK3576_WuKong Board completes the targeted checks
- **THEN** evidence includes at least 20 ON/OFF rounds and at least 60 seconds of combined tracking, recording, and HDMI workload
- **AND** every individual load run is at most 180 seconds
- **AND** evidence reports source, executable, models, configuration, Runtime and driver identities, commands, exit statuses, startup latency, preview behavior, OFF response, and resource changes
