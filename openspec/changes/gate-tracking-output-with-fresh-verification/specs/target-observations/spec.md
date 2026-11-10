# Spec Delta

## MODIFIED Requirements

### Requirement: 框的浮点精度和有效性明确

Published boxes SHALL preserve floating-point centers and sizes and be clipped to delivered image bounds. Valid bbox output SHALL require verified, unexpired tracking permission, finite components, and positive clipped dimensions. Otherwise bbox and line-of-sight (LOS) SHALL both have empty frame_id. Missing or invalid rotation SHALL invalidate only LOS when the box is otherwise eligible. Integer display boxes MUST NOT replace floating publication boxes.

#### Scenario: 小数中心保留
- **WHEN** an authorized prediction has center (768.5,480.25), dimensions (20.5,30.25), and lies inside the image
- **THEN** publication preserves those floating values rather than rounding to integers

#### Scenario: 预测完全越界
- **WHEN** tracking permission is present but the predicted box has no positive-area intersection with the image
- **THEN** both bbox and LOS have empty frame_id and no image-external target is reported valid

#### Scenario: 有框但无相机姿态
- **WHEN** an authorized floating box exists but camera rotation is absent or invalid
- **THEN** bbox remains camera_frame while LOS has empty frame_id and zero values; an identity rotation is not invented

#### Scenario: Initialized but unverified track
- **WHEN** NanoTrack has initialized and caught up but acquisition verification is incomplete
- **THEN** both observation messages remain invalid regardless of the display box or prediction being available

### Requirement: 重复抑制与速度复位各自生效

Within a round, observation timestamps SHALL increase strictly. Non-increasing image-reference samples SHALL suppress both messages with a diagnostic reason, without automatically suppressing images or state changes. New rounds SHALL reset publication guards and LOS history. Invalid targets, unavailable rotation, successful reanchoring, and trust revocation SHALL reset LOS rate history before timestamp suppression. Normal confirmation MUST NOT act as reanchoring.

#### Scenario: 多帧选到同一姿态
- **WHEN** prediction is enabled and two image updates choose the same pose reference time
- **THEN** only the first pair is published; image output and state transitions retain their independent rules

#### Scenario: 重复时间上发生重锚定
- **WHEN** successful reanchoring uses an already published reference time
- **THEN** that pair is suppressed but the next increasing-time valid LOS starts with zero angular velocity

#### Scenario: 新轮允许较早时间
- **WHEN** a completed old round last published at 100 s and a new isolated round starts at 50 s
- **THEN** the new round accepts 50 s without inheriting the old timestamp guard

#### Scenario: Trust-loss notification overtakes an image sample
- **WHEN** a revocation event at 1090 ms follows a valid sample at 1080 ms and later recovery proposes an image-reference sample at 1085 ms
- **THEN** the 1085 ms pair is suppressed; valid sample times are not forged to overtake the event, and LOS history remains reset

## ADDED Requirements

### Requirement: Trust loss emits an explicit paired invalidation event

Revoking previously valid output SHALL attempt one paired bbox and LOS notification with empty frame_id. Its timestamp SHALL be `max(node_now_us, last_observation_us + 1)` microseconds. Use node time sampled at revocation and the last observation time read at serialized construction. The pair SHALL represent revocation, not the detector image. Its identity SHALL be separate from image and OFF events. Repeated expiry checks MUST NOT repeat the event.

#### Scenario: Old detection reveals a new loss event
- **WHEN** a detector result describing 1000 ms is consumed at 1090 ms after an observation at 1080 ms and revokes permission
- **THEN** an invalid pair uses 1090 ms rather than 1000 ms; previously published observations are not rewritten or retracted

#### Scenario: Repeated timer callbacks after loss
- **WHEN** successive callbacks observe the same expired track before any new verified acquisition
- **THEN** they do not repeatedly create revocation-event pairs; ordinary invalid image observations remain governed by their normal publication rules

### Requirement: Revocation and pending loss registration are atomic

Revocation SHALL register its pending loss event in the same protected value-state transition that removes permission. Event identity SHALL include round, revoked attempt, and revocation revision. A publication path MUST NOT observe revoked permission without the associated ordering state. Duplicate callbacks SHALL refer to the same event and its disposition.

#### Scenario: Supervisor pauses immediately after revocation
- **WHEN** the supervisor revokes A at 1090 ms and pauses before acquiring publication access
- **THEN** the worker sees both revoked permission and A's pending event, and cannot bypass that event when admitting later output

### Requirement: Revocation publication is independent of consumed images

A revocation notification SHALL not require a consumed image, inference completion, or catch-up completion. Notification construction and publication SHALL not acquire model or image-processing locks. It SHALL retain existing paired recording and per-topic publication diagnostics. Failure of one topic's publication MUST NOT erase the other outcome.

#### Scenario: No further images arrive
- **WHEN** a valid track's confirmation expires while acquisition provides no additional image
- **THEN** the independent expiry path attempts an invalid pair and records its event and publication times

#### Scenario: Reliable publisher call is delayed
- **WHEN** a previously entered publication call delays delivery or return across revocation
- **THEN** local permission remains revoked and the delay is observable; the system does not claim that subscribers received an atomic pair or a hard delivery deadline

### Requirement: Revocation orders future publication without retroactive claims

Publication admission SHALL check current round, attempt, revocation revision, and expiry immediately before authorizing a valid pair. Work carrying an older revision MUST NOT start a new valid publication after revocation. Publication already admitted before revocation and messages already handed to ROS cannot be retracted. Diagnostics SHALL distinguish admission, revocation, publication calls, and delivery uncertainty.

#### Scenario: In-flight inference finishes after revocation
- **WHEN** an update started under permission revision 7 but returns after revision 8 revoked it
- **THEN** its otherwise valid box cannot start a new valid observation publication under revision 7

### Requirement: Pending loss attempts precede subsequent image observations

Under shared publication serialization, preceding pending loss attempts SHALL complete before a new image observation pair is admitted. Already admitted pairs retain their existing exception. Recovery initialization and verification MAY continue internally. After loss disposition, valid admission SHALL recheck identity, revision, geometry, and actual evidence age. Waiting MUST NOT renew evidence or relabel its reference time.

#### Scenario: Recovery verifies before the supervisor resumes
- **WHEN** A is revoked at 1090 ms, its supervisor pauses before publication, and B verifies at 1160 ms from an image captured at 1150 ms with a 100 ms confirmation lifetime
- **THEN** a path acquiring publication serialization services A's loss attempt before admitting B's pair, and B's confirmation deadline remains 1250 ms

#### Scenario: Notification delay consumes the recovery lifetime
- **WHEN** B has a 1250 ms confirmation deadline and A's pending loss attempt finishes at or after 1250 ms
- **THEN** B's evidence cannot authorize valid output; later fresh evidence and the normal recovery rules govern any subsequent admission

#### Scenario: Ordinary invalid image arrives before pending loss is serviced
- **WHEN** A has a pending loss event and a normal update proposes an invalid image observation with a later reference time
- **THEN** the publication path services A's event first, then applies the unchanged timestamp guard to the image pair

### Requirement: Loss attempt completion uses independent publication outcomes

A loss attempt SHALL complete after pair construction, enabled paired bag-submission attempt, both topic publication attempts, and recording their outcomes. Bag or topic failure MUST NOT skip the other topic's attempt. Completion MUST NOT require subscriber acknowledgment. The event SHALL be disposed before later output admission and MUST NOT be retried after recovery output.

#### Scenario: One invalid topic fails before recovery output
- **WHEN** A's invalid bbox publication throws but its LOS publication returns normally
- **THEN** both outcomes are retained, A's event is disposed after the attempts, and a resumed duplicate callback cannot retry A's notification after B's output

### Requirement: Public recovery states follow the loss transition

State publication SHALL share loss-event ordering. The pending event's TargetLost publication attempt SHALL precede subsequent Detecting, TargetFound, or Tracking publication for later phases. The loss ordering condition SHALL remain pending until that state attempt and the invalid-pair attempt complete. Stale worker states MUST NOT overwrite a later transition.

#### Scenario: B verifies while A's TargetLost is pending
- **WHEN** B verifies internally before A's loss notification and TargetLost publication have been attempted
- **THEN** B's TargetFound publication waits for A's loss attempt; the externally attempted sequence preserves TargetLost before recovery states

### Requirement: Trust loss does not stop the tracking round

Trust-loss notification SHALL keep recovery and enabled recording active. It MUST NOT invoke OFF cleanup, stop preview, issue mission commands, or manufacture an input frame. It SHALL be eligible for paired bag submission before round sealing. A later operator OFF SHALL retain its existing separate one-time invalid-notification behavior.

#### Scenario: Expiry followed by operator OFF
- **WHEN** confirmation expiry creates a trust-loss event and the operator later sends OFF
- **THEN** recovery remains possible between the events, and OFF independently performs its existing lifecycle and final notification

### Requirement: OFF drains pending loss before its separate final notification

OFF SHALL close normal admission and finish preceding pending loss attempts before its separate final invalid notification and round sealing. Round isolation SHALL prevent old callbacks from publishing or disposing events in a new round. Loss and OFF SHALL retain distinct event identities and deduplication.

#### Scenario: OFF arrives with an unpublished loss event
- **WHEN** A is revoked, its loss event is pending, and the operator sends OFF before any recovery publication
- **THEN** A's loss attempt precedes the separate OFF notification and bag sealing, and a delayed old callback cannot emit A's event in the next round

### Requirement: Valid observations preserve their existing reference-time contract

Trust gating SHALL preserve `publish_prediction=false` capture-time output. When prediction is enabled, the existing eligible pose-time selection and capture-time fallback SHALL remain. The 300 ms pose-selection rule MUST NOT replace detector-confirmation expiry. Event timestamps MUST NOT be used to relabel valid image measurements.

#### Scenario: Pose is fresh but detector confirmation is expired
- **WHEN** pose selection satisfies its 300 ms rule but supporting detection evidence is older than the 100 ms configured lifetime
- **THEN** the pose does not authorize valid observations; expiry uses actual elapsed evidence age
