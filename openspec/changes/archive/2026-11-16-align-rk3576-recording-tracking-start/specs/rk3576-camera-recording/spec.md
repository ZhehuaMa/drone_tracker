# rk3576-camera-recording Spec Delta

## ADDED Requirements

### Requirement: Recording reports acceptance for the exact current candidate

Acceptance means an original camera frame has passed final capture checks and entered its session's bounded encoder-input queue. A receipt SHALL report accepted only after that publication succeeds. Staging, worker readiness, or a change in aggregate counters MUST NOT alone establish acceptance.

#### Scenario: OFF cancels staged pixels
- **WHEN** OFF closes acceptance after pixels are staged but before final publication
- **THEN** the receipt reports cancellation and the candidate is not accepted

#### Scenario: Recording queue rejects an input
- **WHEN** the current frame cannot enter the bounded recording queue
- **THEN** its receipt reports rejection even if earlier or later frames are accepted

#### Scenario: Session attaches during a frame visit
- **WHEN** a frame visit began without a recording receiver and a session attaches before that visit ends
- **THEN** that frame does not inherit acceptance from the newly attached session

### Requirement: Recording status identifies the current request synchronously

A recording request generation identifies one requested ON session within a stream. Registering ON SHALL immediately expose its stream, tracking round, generation, and pending status. Preparation, readiness, and terminal status SHALL retain that identity. An older request's status MUST NOT authorize the current round.

#### Scenario: Previous completion is still visible
- **WHEN** a new ON request is registered after an older recording completed
- **THEN** the new request is pending under its own identity before asynchronous preparation begins

#### Scenario: Old worker cannot be reclaimed
- **WHEN** a new request cannot allocate recording resources because an old worker remains in use
- **THEN** the new request reports terminal unavailability under its own identity

### Requirement: Accepted startup frames enter ordinary video immediately

Startup is the interval from ON to the first successful tracker initialization. An accepted startup frame SHALL enter ordinary encoding and writing without waiting for target detection. Recorder preparation and startup filtering SHALL NOT retain earlier unaccepted preview pixels for later replay.

#### Scenario: No target is found
- **WHEN** the ready recorder accepts frames 100 through 120 and the detector finds no target
- **THEN** the recording contains those accepted frames despite startup filtering remaining active

#### Scenario: Preparation has not completed
- **WHEN** qualified frames 90 through 99 arrive before the receiver attaches
- **THEN** those frames remain outside this video and are not subsequently replayed

### Requirement: Startup coordination preserves existing resource and failure limits

Receipt generation SHALL preserve the four-frame and 64 MiB raw-recording limits. Capture and control callbacks MUST NOT wait for encoder or storage completion. Preparation and close SHALL retain their separate three-second budgets. Warmup SHALL retain its independent budget. Existing qualified-input progress checks SHALL remain effective during startup filtering.

#### Scenario: Qualified inputs are continuously rejected
- **WHEN** a prepared session receives qualified inputs but makes no persistence progress
- **THEN** the existing five-second no-progress rule terminates that recording rather than leaving startup waiting indefinitely

#### Scenario: Recording preparation fails
- **WHEN** resource preparation fails or exceeds its existing budget
- **THEN** the current request publishes a terminal failure that allows healthy tracking to continue

### Requirement: Startup coverage evidence distinguishes acceptance from saved video

Coverage evidence SHALL retain the first executed YOLO source and first successful initialization source when those events occur. YOLO is the asynchronous target detector. A source identifies the original image, rather than the later processing frame. Evidence SHALL distinguish accepted, verified-written, unavailable, cancelled, and not-applicable outcomes. Acceptance alone MUST NOT be reported as verified-written coverage.

#### Scenario: Initialization uses an older image
- **WHEN** detection executes on frame 100 and successful tracker initialization processes that result while handling frame 108
- **THEN** evidence identifies frame 100 as the initialization source

#### Scenario: Accepted pixels are lost to a recording failure
- **WHEN** a startup source was accepted but recording fails before writing it
- **THEN** coverage reports the failure and does not claim verified-written coverage

#### Scenario: Diagnostics are off
- **WHEN** initial detection and initialization occur with diagnostics disabled
- **THEN** bounded source evidence remains available for recording coverage without enabling diagnostic event collection

#### Scenario: No initialization occurs
- **WHEN** OFF ends a normally recorded round without any successful tracker initialization
- **THEN** the initialization source is absent with an explicit no-initialization outcome rather than a fabricated frame identity

### Requirement: Coverage metadata remains bounded and compatible

Startup counters SHALL separate preparation omissions, recording rejections, pending-task replacements, and recorder drops. New coverage metadata SHALL be additive and bounded per round. Missing startup fields in older recordings SHALL mean unknown coverage. Verification SHALL match exact session and frame identities against saved frame records.

#### Scenario: Healthy video starts after preparation
- **WHEN** eight qualified startup frames preceded attachment and all later offered frames were accepted
- **THEN** evidence reports eight preparation omissions separately from zero recorder drops

#### Scenario: Historical recording has no receipts
- **WHEN** inspection reads a November 4 recording without the new startup fields
- **THEN** ordinary recording inspection still works and startup coverage is unknown

#### Scenario: Written coverage is verified
- **WHEN** exact startup source identities match saved frame records and video readback confirms the corresponding frames
- **THEN** the verification result can report verified-written coverage for those sources
