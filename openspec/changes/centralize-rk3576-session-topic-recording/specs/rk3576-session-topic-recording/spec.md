# Spec Delta

## Purpose

Preserve the known application messages exchanged by the bridge, tracker, and interceptor during explicit operating sessions. Make topic coverage, source timestamps, intentional sampling, storage protection, and unexpected recording gaps independently inspectable.

## ADDED Requirements

### Requirement: Fixed startup catalog covers the application interfaces

The RK3576 recorder SHALL use all entries in the application catalog for its default profile. It SHALL freeze resolved names, message types, timestamp rules, delivery settings, and sampling limits at startup. It MUST NOT add topics through runtime graph discovery. For subscription-backed entries, a publisher that starts later SHALL be able to connect to the configured subscription.

#### Scenario: Default application catalog and policies
- **WHEN** the default application profile is selected without name or rate overrides
- **THEN** recording uses the following complete catalog and its declared timestamp and rate defaults

This catalog defines the default application profile. Topic-name overrides select a different resolved name for the same entry and message type; they do not add dynamically discovered topics. `H` means `header.stamp`, `S` means the message's own `stamp`, and `R` means recorder receipt time. Receipt time is the ROS system-clock time captured at recording callback entry. Source timestamps describe the original measurement or event, rather than when the file was written.

| Stable entry ID | Default topic | Message type | Time | Default maximum Hz |
|---|---|---|---|---:|
| body_imu | `/mavros/imu/data` | `sensor_msgs/msg/Imu` | H | 100 |
| gimbal | `/fpv/gimbal` | `sensor_msgs/msg/Imu` | H | 100 |
| odom | `/fpv/odom` | `nav_msgs/msg/Odometry` | H | 50 |
| rc_in | `/mavros/rc/in` | `mavros_msgs/msg/RCIn` | H | 25 |
| flight_state | `/mavros/state` | `mavros_msgs/msg/State` | H | 0 |
| extended_state | `/mavros/extended_state` | `mavros_msgs/msg/ExtendedState` | H | 0 |
| battery | `/mavros/battery` | `sensor_msgs/msg/BatteryState` | H | 0 |
| pressure | `/fpv/static_pressure` | `sensor_msgs/msg/FluidPressure` | H | 25 |
| temperature | `/fpv/temperature_baro` | `sensor_msgs/msg/Temperature` | H | 25 |
| low_level_feedback | `/fpv/low_level_feedback` | `quadrotor_msgs/msg/LowLevelFeedback` | H | 0 |
| gimbal_mode | `/fpv/gimbal_mode` | `std_msgs/msg/String` | R | 0 |
| trigger | `/fpv/tracker_trigger` | `std_msgs/msg/Header` | S | 0 |
| altitude | `/fpv/altitude` | `std_msgs/msg/Float64` | R | 25 |
| gps_raw | `/mavros/gpsstatus/gpsraw` | `mavros_msgs/msg/GPSRAW` | H | 0 |
| gps_fix | `/mavros/global_position/global` | `sensor_msgs/msg/NavSatFix` | H | 0 |
| failsafe | `/fpv/failsafe` | `std_msgs/msg/Bool` | R | 0 |
| motor_rpm | `/fpv/motor_rpm` | `std_msgs/msg/Float32MultiArray` | R | 25 |
| control | `/fpv/control_command` | `quadrotor_msgs/msg/ControlCommand` | H | 0 |
| control_raw | `/fpv/control_command_raw` | `quadrotor_msgs/msg/ControlCommand` | H | 0 |
| arm | `/fpv/arm` | `std_msgs/msg/Bool` | R | 0 |
| offboard | `/fpv/offboard` | `std_msgs/msg/Bool` | R | 0 |
| tracker_state | `/fpv/tracker_state` | `std_msgs/msg/Int8` | R | 0 |
| radar_state | `/fpv/radar_state` | `std_msgs/msg/Int8` | R | 0 |
| mission_state | `/fpv/mission_state` | `std_msgs/msg/Bool` | R | 0 |
| mission_start | `/fpv/mission_start` | `std_msgs/msg/Bool` | R | 0 |
| target_bbox | `/fpv/target_bbox` | `vision_msgs/msg/Detection2D` | H | 0 |
| target_los | `/fpv/target_los` | `geometry_msgs/msg/TwistStamped` | H | 0 |
| tracker_image | `/fpv/tracker_image` | `sensor_msgs/msg/Image` | H | 2 |
| status_text | `/mavros/statustext/send` | `mavros_msgs/msg/StatusText` | H | 0 |
| radar_target | `/fpv/radar/target` | `nav_msgs/msg/Odometry` | H | 0 |
| radar_interceptor | `/fpv/radar/interceptor` | `nav_msgs/msg/Odometry` | H | 0 |

Zero maximum Hz means no frequency sampling. Radar and externally supplied command topics remain catalog entries when their publishers are absent. The optional bench profile adds `bench_status`: `/bench_pid_tuner/status`, `std_msgs/msg/String`, receipt time, no frequency sampling. Framework topics, test-fixture topics, and unrelated domain topics are outside these profiles.

#### Scenario: Late radar publisher without graph rescanning
- **WHEN** the service starts without radar publishers and `/fpv/radar/target` begins publishing during an active session
- **THEN** its preconfigured subscription can record messages without changing the catalog or scanning for new topics

#### Scenario: Unrelated topic
- **WHEN** another node publishes `/unrelated/debug` during a session
- **THEN** the recorder does not subscribe to or record that topic

### Requirement: Topic names and delivery settings are explicit

The recorder SHALL honor startup topic-name overrides and ROS remappings. It SHALL reject conflicting resolved names or invalid recording settings and report recording unavailable. It SHALL preserve healthy tracking when a recording-only setting is invalid. Runtime changes to recording settings SHALL be rejected. Delivery settings SHALL be fixed for each catalog entry.

#### Scenario: Interceptor configured without its packaged profile
- **WHEN** the interceptor uses its bare `/fpv/mission_launch` Boolean input and the recorder's mission_start name is configured to match
- **THEN** the recorder records that Boolean topic rather than assuming `/fpv/mission_start`

#### Scenario: Duplicate resolved names
- **WHEN** startup overrides resolve body_imu and altitude to the same topic name
- **THEN** recording is reported unavailable with the conflicting entries, and healthy tracking remains usable

#### Scenario: Runtime policy update
- **WHEN** a running service receives a request to change a `bag.*` recording setting
- **THEN** the update fails with a restart-required explanation and the frozen policy remains effective

### Requirement: Recording uses explicit operating sessions

With `need_save=true`, a newly established ON round SHALL open one new recording session. OFF, EOF, a round-ending failure, or normal service termination SHALL stop admission and close that session. Service startup and preview alone MUST NOT open a bag. MISSION and DISMISS SHALL be recordable events without independently opening or closing a tracking recording session. Thread placement SHALL follow the topic-recording role contract in `rk3576-thread-affinity`.

#### Scenario: Preview followed by two sessions
- **WHEN** the camera previews for ten seconds, then runs ON/OFF twice
- **THEN** preview creates no bag, and the two established rounds create separate, uniquely identified sessions

#### Scenario: Mission cancellation while tracking continues
- **WHEN** an active session receives DISMISS and the tracker remains ON
- **THEN** DISMISS is recorded and the same recording session continues

#### Scenario: Recording disabled
- **WHEN** `need_save=false` and the service previews or receives ON/OFF
- **THEN** no recording subscriptions or recording executor thread are started and no bag writer or bag files are created

#### Scenario: Installed topic role across two rounds
- **WHEN** the standard installed policy selects topic-recording CPU 2 and an enabled service runs ON/OFF twice
- **THEN** the dedicated callback executor and each round's writer confirm CPU 2, preview still creates no bag, and the two rounds retain distinct session boundaries

### Requirement: Session boundaries retain authoritative control events

The recorder SHALL attempt to admit the initiating ON once when its session opens. It SHALL attempt to admit OFF and the final invalid bbox/LOS pair before sealing that session. Repeated ON MUST NOT create another session. Repeated OFF MUST NOT create another invalid observation pair. Messages arriving after sealing MUST NOT enter the next session through an old session handle.

#### Scenario: OFF during blocked inference
- **WHEN** inference is still running and an established round receives OFF
- **THEN** OFF and the final invalid observation pair are offered before sealing without waiting for inference or disk writes, and late old-round submissions cannot enter a later session

#### Scenario: Delayed response in another module
- **WHEN** the interceptor publishes its mission-stop response after the tracker has sealed the OFF session
- **THEN** that response is outside the recording interval and is not silently attached to a future session

### Requirement: Source timestamps and payloads are preserved

The recorder SHALL select bag timestamps using the catalog's H, S, or R rule. It SHALL preserve original message payloads. It MUST NOT substitute file-write time for a valid source timestamp. It SHALL preserve valid source timestamps that arrive out of order. `ControlCommand.expected_execution_time` MUST NOT replace its header timestamp.

#### Scenario: Delayed control command
- **WHEN** a command has header time 10.000 s, execution time 10.100 s, and recorder receipt time 10.020 s
- **THEN** its bag timestamp is 10.000 s and all three message fields remain unchanged where present

#### Scenario: Header trigger and headerless motor values
- **WHEN** a trigger's own stamp is 20.000 s and a motor_rpm array is received at 20.010 s
- **THEN** their bag timestamps are respectively 20.000 s and 20.010 s

### Requirement: Invalid source-time fallback is visible

For this real-time ROS entry point, a zero source stamp, negative seconds, or nanoseconds outside [0, 1000000000) SHALL use receipt time for bag indexing. Each fallback SHALL increment a per-topic counter and record its reason. The recorder MUST NOT rewrite the source stamp in the payload or reject an otherwise valid stamp merely because its age is large.

#### Scenario: Unset header
- **WHEN** a message in an H entry has a zero header stamp and arrives at 30.000 s
- **THEN** its bag timestamp is 30.000 s, the original zero header is retained, and the timestamp-fallback counter increases

### Requirement: Frequency sampling affects only recording

For a positive maximum frequency f, the recorder SHALL select the first eligible message immediately and subsequent messages at least 1/f monotonic seconds apart. A monotonic clock measures elapsed time without system-clock corrections. It MUST NOT buffer discarded samples for catch-up writes. Original publication rates and algorithm subscriptions SHALL remain unchanged.

#### Scenario: High-rate IMU
- **WHEN** body IMU arrives at 200 Hz for one monotonic second and its recording limit is 100 Hz
- **THEN** retained ordinary samples are at least 10 ms apart and policy omissions are counted; the algorithm's IMU stream is unchanged

#### Scenario: System-clock correction
- **WHEN** system time moves backward during a positive-rate recording policy
- **THEN** sampling spacing continues to use elapsed monotonic time, while retained valid source stamps remain unmodified

### Requirement: Commands and observation boundaries are protected from sampling

Trigger, arm, offboard, mission-start, mission-state, tracker-state, radar-state, failsafe, gimbal-mode, and status-text entries SHALL retain every received active-session event without rate or byte sampling. Control commands SHALL default to full-rate recording. A bbox/LOS recording-rate override SHALL apply to the pair together. The final OFF pair SHALL bypass pair sampling.

#### Scenario: Brief state transition
- **WHEN** failsafe changes false/true/false within 5 ms during an active session
- **THEN** all three received messages are offered to bounded admission rather than frequency-filtered

#### Scenario: Sampled observation pair followed by OFF
- **WHEN** the pair policy selects one regular observation and OFF arrives inside its sampling interval
- **THEN** both messages of the regular selected pair and both messages of the final invalid pair are offered together

### Requirement: Published debug images have independent recording limits

Published `/fpv/tracker_image` messages SHALL be eligible for recording with a default maximum of 2 Hz. Their default byte budget SHALL be 16 MiB/s with a 16 MiB burst allowance. The recorder MUST NOT enable image publication or generate preview images. A session with no admitted image SHALL contain no image topic metadata. Image policy omissions SHALL be reported separately.

#### Scenario: Existing image publisher enabled
- **WHEN** ON processing publishes BGR8 debug images at 30 Hz and image recording limits are available
- **THEN** only policy-selected original images are admitted, with their capture headers unchanged

#### Scenario: Production image switch remains off
- **WHEN** `visualized_result=false` throughout an ON/OFF session
- **THEN** no debug images are generated for recording and the bag contains no tracker_image data or topic metadata

### Requirement: Retained mode is available at session start

The recorder SHALL receive and cache the latest retained gimbal_mode value while enabled but idle. Retained delivery means ROS can provide the publisher's latest saved value to a late subscriber. A session SHALL offer one known initial mode snapshot before accepting ordinary recording callbacks. Its indexing time SHALL be session start, and its snapshot origin SHALL be reported separately from live changes.

#### Scenario: AIM published before ON
- **WHEN** the bridge publishes retained AIM at startup and ON occurs ten seconds later without a mode change
- **THEN** the bag can show initial AIM with session-start indexing time and a cached-snapshot marker, without claiming a new AIM transition occurred at ON

### Requirement: Admission is bounded and keeps observation pairs intact

Retained session work SHALL remain within 256 message slots and 64 MiB of accounted payload, including the writer's in-flight item. Each observation pair SHALL consume two slots. Insufficient capacity SHALL reject both members and count both losses. Admission MUST NOT wait for storage. Dynamic arrays and strings SHALL contribute to payload accounting.

#### Scenario: One remaining message slot
- **WHEN** a queue has one available message slot and receives a bbox/LOS pair
- **THEN** neither member is accepted and both dropped counters increase

#### Scenario: Large array and slow disk
- **WHEN** variable-length image or motor data would exceed the retained-byte limit while the writer is slow
- **THEN** admission rejects that item immediately and reports the gap without blocking tracking

### Requirement: Topic limits preserve room for control evidence

The recorder SHALL reserve 16 of its 256 slots and 1 MiB of its 64 MiB budget for protected control/state events and the final OFF pair. Ordinary entries SHALL have a default per-entry limit of 32 retained messages and 8 MiB. Images SHALL have limits of four messages and 32 MiB. Reserved and per-entry limits MUST NOT increase the global limits.

#### Scenario: Images saturate their allowance
- **WHEN** images occupy their per-entry limit but reserved capacity is available and OFF arrives
- **THEN** another image is rejected while OFF and its final invalid pair remain eligible for reserved admission

### Requirement: Recording completeness distinguishes deliberate omissions

Each session SHALL report per-entry offered, policy-skipped, byte-limited, oversized, queue-dropped, accepted, written, and timestamp-fallback counts. It SHALL report accepted-but-unwritten totals and any supported middleware-loss indication. Intentional policy omissions alone MUST NOT imply unexpected loss. Completeness SHALL describe recorder admission, not prove delivery of every publisher message.

#### Scenario: Successful sampled recording
- **WHEN** a session intentionally samples IMU, writes every accepted item, closes successfully, and has no detected unexpected gaps
- **THEN** its result is complete according to its frozen policy and reports the omitted samples

#### Scenario: Queue overflow
- **WHEN** an active session drops one ordinary message for lack of queue capacity
- **THEN** the result reports an incomplete session even if all remaining accepted messages are written

### Requirement: Session coverage and errors remain inspectable

A session manifest SHALL identify its source revision, session identity, start/end boundaries, frozen catalog and limits, timestamp rules, resolved names, delivery settings, per-entry activity, and terminal reason. Entries with no received messages SHALL be distinguishable from detected subscription errors. A manifest is the session's machine-readable summary and MUST NOT claim publisher-level loss counts that are unavailable.

#### Scenario: Quiet optional radar input
- **WHEN** no radar publisher supplies messages during an otherwise healthy session
- **THEN** its catalog entries remain visible as not observed rather than being silently omitted or reported as successful captures

### Requirement: File splitting does not delete evidence

The recorder SHALL split active bags at a configurable file-size target, default 256 MiB. Splitting SHALL continue the same session in another file while storage protection permits. The recorder MUST NOT delete existing recordings automatically. A split MUST NOT change message payloads, timestamps, or session identity.

#### Scenario: Multiple files within one session
- **WHEN** ON remains active after a bag file reaches its configured size target and storage remains available
- **THEN** later messages continue in another file belonging to the same session and earlier files remain intact

### Requirement: Storage protection ends recording independently

Bag storage SHALL default to a 5 GiB total owned-bag budget and a 5 GiB filesystem free-space threshold. The budget SHALL include existing complete and incomplete owned bags and their summaries. When a storage check detects either limit, the recorder SHALL seal the session, report storage_limit, and attempt to drain accepted work where storage permits. Other healthy functions SHALL continue.

#### Scenario: Space consumed by another writer
- **WHEN** camera video consumes space on the same filesystem and the bag writer observes free space below its threshold
- **THEN** bag admission stops with a storage_limit reason; no old evidence is removed and healthy tracking continues

### Requirement: Normal shutdown and abrupt loss have different guarantees

Normal session close SHALL attempt to write accepted messages and close the bag safely. Writer open, write, or close failures SHALL retain their original reason and stop that session's admission. Abrupt power loss SHALL have no guaranteed final-message retention interval. File presence alone MUST NOT be reported as proof of a complete or power-loss-safe recording.

#### Scenario: Mid-session write failure
- **WHEN** a bag write throws after accepting messages
- **THEN** the recorder stops admission, attempts safe close, reports accepted-but-unwritten work, and leaves healthy tracking and camera recording running

### Requirement: Production acceptance measures the combined workload

Acceptance SHALL compare recording disabled, the previous recorder, and the expanded recorder on one identified production HZ-RK3576_WuKong Board. Each performance run SHALL last at most 180 seconds. The comparison SHALL preserve camera, models, HDMI, diagnostics, and independent video settings. It SHALL report actual throughput, latency, CPU, memory, disk traffic, gaps, and bag readback results.

#### Scenario: Board comparison report
- **WHEN** the three configurations are measured with matched workload identities
- **THEN** the report gives absolute and relative changes, frozen policies, input message rates, queue peaks, and unrun checks without treating host or emulated results as production acceptance
