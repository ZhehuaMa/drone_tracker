# Spec Delta

## ADDED Requirements

### Requirement: Topic recording has a dedicated optional CPU role

A role-pinned version-1 policy SHALL support optional `roles.topic_recording`. When present, both the topic subscription executor and bag writer SHALL use that role. Its explicit value SHALL be an integer in the allowed set and available on the target. When absent, legacy runtime policies SHALL retain executor `ros_transport` and writer `recording` separately. No policy or `mode: inherit` SHALL preserve inherited affinity without claiming CPU 2.

#### Scenario: Standard installed role
- **WHEN** the selected policy permits `[2, 4, 5, 6, 7]` and explicitly assigns `topic_recording: 2`
- **THEN** both dedicated topic threads request and confirm `{2}` before work, while their other configured roles remain unchanged

#### Scenario: Legacy policy used directly at runtime
- **WHEN** a valid version-1 role-pinned policy omits `topic_recording`, specifies `ros_transport: 6` and `recording: 7`, and the node is launched directly with it
- **THEN** the recording executor retains CPU 6, the bag writer retains CPU 7, and logs do not claim CPU 2 binding

#### Scenario: Invalid explicit topic role
- **WHEN** `topic_recording` is explicitly null, non-integer, negative, outside the allowed set, or unavailable on the target
- **THEN** policy validation fails with the field and reason identified rather than treating it as an omitted legacy field

#### Scenario: Explicit inherited mode
- **WHEN** the node has no explicit policy or selects a valid `mode: inherit` policy
- **THEN** no per-role topic binding is imposed and actual inherited affinity is not reported as guaranteed CPU 2 placement

### Requirement: Topic threads confirm binding before work and on recreation

Each newly created topic executor or bag writer SHALL apply and read back its selected role before work. Its creator SHALL wait for that confirmation. A binding failure SHALL prevent that worker from starting work, clean up its resources, and report recording unavailable or failed while healthy tracking continues. Binding MUST NOT depend on thread IDs, creation order, addresses, or post-startup thread scans.

#### Scenario: A second session creates a new writer
- **WHEN** OFF closes one session and another ON creates its replacement writer under explicit topic CPU 2
- **THEN** the new writer independently confirms `{2}` before opening or writing the new bag, while the dedicated executor retains its selected binding

#### Scenario: Executor binding is rejected
- **WHEN** the operating system rejects the recording executor's target mask or returns a different readback
- **THEN** the creator observes the error, no recording callback runs on that worker, any created thread is joined, and healthy tracking remains usable

#### Scenario: Writer binding is rejected
- **WHEN** a session writer cannot apply or confirm its selected topic mask
- **THEN** it does not open or write the bag, the session reports the original failure, and healthy tracking and independent camera recording continue

### Requirement: Topic placement preserves other affinity responsibilities

Topic-role binding SHALL leave general ROS resource creation on `ros_transport` and camera-video controllers and encoders on `recording`. It MUST NOT change other existing thread or process masks, scheduling priority, CPU frequency control, or kernel isolation. CPU affinity SHALL describe allowed execution rather than exclusive core ownership.

#### Scenario: Concurrent camera video and topic recording
- **WHEN** the standard policy selects topic recording 2, ROS transport 6, and camera recording 7 during a camera ON round
- **THEN** both topic workers confirm CPU 2, ROS communication helpers retain their transport placement, and the camera controller and encoder retain CPU 7

#### Scenario: Topic recording is disabled
- **WHEN** `need_save=false` and an independent camera-video session is enabled
- **THEN** no dedicated topic worker is created, camera-video resources still use their selected recording role, and other thread masks remain unchanged
