# RK3576 session topic recording

[Project overview](../../README.md) · [Parameters](../../parameters/README.md) ·
[Deployment](../../deployment/README.md)

`need_save` enables ROS message recording inside the tracker process.
`RecordingManager` owns the fixed typed topic catalog, subscriptions, recording
policy and session handles. One dedicated ROS executor runs its callbacks. An
executor is the scheduler that invokes subscription callbacks. The manager uses
a callback group that the main tracker executor cannot attach automatically.
One `BagSession` worker exclusively opens, writes and closes each SQLite bag.
There is no additional bag-recorder service. Original-camera H.264/TS recording
still uses its independent `recording.*` settings and process.

## CPU placement and installation

The standard installed CPU policy allows `[2, 4, 5, 6, 7]` and selects
`roles.topic_recording: 2`. Both the subscription executor and each new bag
writer apply that role and confirm the actual mask before doing work. Every
ON/OFF replacement writer repeats this handshake. This is allowed execution on
CPU 2, not exclusive ownership of the core.

ROS subscriptions, callback groups, and executor resources are created and
destroyed through `ros_transport` (standard CPU 6). Independent camera-video
controllers and encoders keep `recording` (standard CPU 7). A topic-worker
binding failure is reported through recording failure handling while healthy
tracking continues. A globally invalid CPU policy still fails startup.

Explicit custom policies can select another topic core or `mode: inherit`.
Inherited mode imposes no per-role binding. A version-1 policy used directly at
runtime without `topic_recording` retains the old split: executor on
`ros_transport`, bag writer on `recording`. That compatibility path does not
promise CPU 2. A new recorder-enabled role-pinned release must name the topic
role explicitly; packaging and installer preflight reject omission with a
migration error. See the [deployment guide](../../deployment/README.md) for
custom policies, target availability checks, installation, and rollback.

## Operating intervals

Startup preview creates no bag. An established ON round opens one uniquely named
`drone_rosbag_` session below `save_path`. Repeated ON commands stay in that
session. OFF offers the original trigger and final invalid bbox/LOS pair before
sealing admission. A bbox/LOS pair consists of the two observations describing
the same target and timestamp. The tracker offers them together before ROS
publication, so a publication failure does not erase their recording evidence.

ON/OFF forwarding shares the tracker control lock with the decision to open or
stop a round. For example, if OFF arrives while a worker is opening an established
round, that round first receives its pending ON. OFF then enters the same session
before its final pair and sealing. The receipt time is captured when the ROS
callback begins, before it waits for that control decision.

Continuous preview can accept ON while the previous round is closing; that
original command belongs to the next established round. Finite-input mode keeps
its existing behavior of ignoring ON during cleanup. An ignored command cannot
be replayed into a later programmatic start. Failed starts clear their pending
recording commands, and session summaries retain the actual ending boundary.
A programmatic stop also cancels a pending start without fabricating a ROS OFF
Header. Final service shutdown freezes the control intent before stopping the
recording executor, so a worker already preparing cannot open another bag.

After sealing, an old callback handle cannot submit to the next session.
There is no post-OFF tail: for example, another module's response arriving after
OFF sealing is outside that bag. MISSION and DISMISS events do not independently
start or stop a recording interval. A programmatic start opens a session but
does not fabricate a ROS ON message. EOF, round-ending failure and normal service
exit close the active session. Normal close attempts to drain accepted work.
Abrupt power loss has no guaranteed final-message retention interval.

With `need_save=false`, there are no recording subscriptions, executor or writer
threads. Healthy tracking and independent video recording retain their own
switches. Invalid recording-only configuration reports the recorder unavailable
while tracking can continue. Recording settings are frozen at startup; changing
`bag.*` requires a restart.

## Fixed typed catalog

H selects `header.stamp`, S selects the Header message's own `stamp`, and R selects
ROS system-clock receipt time captured at callback entry. The generated message
types come from the real ROS packages; serialized offsets are never guessed.
No graph discovery or rescan is performed. A known publisher can start after ON
and connect to its already existing subscription.

| Entry | Default topic | Generated type | Time | Maximum Hz |
|---|---|---|---|---:|
| body_imu | /mavros/imu/data | sensor_msgs/Imu | H | 100 |
| gimbal | /fpv/gimbal | sensor_msgs/Imu | H | 100 |
| odom | /fpv/odom | nav_msgs/Odometry | H | 50 |
| rc_in | /mavros/rc/in | mavros_msgs/RCIn | H | 25 |
| flight_state | /mavros/state | mavros_msgs/State | H | 0 |
| extended_state | /mavros/extended_state | mavros_msgs/ExtendedState | H | 0 |
| battery | /mavros/battery | sensor_msgs/BatteryState | H | 0 |
| pressure | /fpv/static_pressure | sensor_msgs/FluidPressure | H | 25 |
| temperature | /fpv/temperature_baro | sensor_msgs/Temperature | H | 25 |
| low_level_feedback | /fpv/low_level_feedback | quadrotor_msgs/LowLevelFeedback | H | 0 |
| gimbal_mode | /fpv/gimbal_mode | std_msgs/String | R | 0 |
| trigger | /fpv/tracker_trigger | std_msgs/Header | S | 0 |
| altitude | /fpv/altitude | std_msgs/Float64 | R | 25 |
| gps_raw | /mavros/gpsstatus/gpsraw | mavros_msgs/GPSRAW | H | 0 |
| gps_fix | /mavros/global_position/global | sensor_msgs/NavSatFix | H | 0 |
| failsafe | /fpv/failsafe | std_msgs/Bool | R | 0 |
| motor_rpm | /fpv/motor_rpm | std_msgs/Float32MultiArray | R | 25 |
| control | /fpv/control_command | quadrotor_msgs/ControlCommand | H | 0 |
| control_raw | /fpv/control_command_raw | quadrotor_msgs/ControlCommand | H | 0 |
| arm | /fpv/arm | std_msgs/Bool | R | 0 |
| offboard | /fpv/offboard | std_msgs/Bool | R | 0 |
| tracker_state | /fpv/tracker_state | std_msgs/Int8 | R | 0 |
| radar_state | /fpv/radar_state | std_msgs/Int8 | R | 0 |
| mission_state | /fpv/mission_state | std_msgs/Bool | R | 0 |
| mission_start | /fpv/mission_start | std_msgs/Bool | R | 0 |
| target_bbox | /fpv/target_bbox | vision_msgs/Detection2D | H | 0 |
| target_los | /fpv/target_los | geometry_msgs/TwistStamped | H | 0 |
| tracker_image | /fpv/tracker_image | sensor_msgs/Image | H | 2 |
| status_text | /mavros/statustext/send | mavros_msgs/StatusText | H | 0 |
| radar_target | /fpv/radar/target | nav_msgs/Odometry | H | 0 |
| radar_interceptor | /fpv/radar/interceptor | nav_msgs/Odometry | H | 0 |

`bag.bench_profile=true` adds only `bench_status` at `/bench_pid_tuner/status`
(`std_msgs/String`, receipt time, zero maximum Hz). It does not start the bench tool.
Framework topics and unrelated domain topics are excluded. Trigger and the
bbox/LOS pair use direct tracker interfaces; every other entry has exactly one
manager-owned subscription. The business gimbal callback continues to update
algorithm pose history independently.

Use `bag.topics.<entry>.name` for startup names. ROS remappings and the node
namespace also apply. Conflicting resolved names are rejected. Direct names must
match the actual business endpoints after remapping. The packaged interceptor
uses `/fpv/mission_start`; its bare constructor instead uses `/fpv/mission_launch`.
Select the name matching the actual launch configuration. Bridge private control
topics must likewise use the selected `/fpv/*` launch remappings.

## Source time and selection policy

Source time describes when a measurement or command originated. Receipt time
describes when the recorder callback received it. Monotonic time measures elapsed
time without system-clock corrections and controls rate selection only. Writer
time is never substituted for valid source time. For example, a command with
header 10.000 s, expected execution 10.100 s and receipt 10.020 s is indexed at
10.000 s. The complete original payload, including all three fields, is preserved.

Zero source stamps, negative seconds, or nanoseconds outside `[0,1000000000)` use
captured receipt time for bag indexing and increment separate fallback reasons.
Headers are never rewritten. Old, future and out-of-order valid stamps remain
valid recording timestamps.

`bag.topics.body_imu.max_hz: 100.0` selects the first sample immediately, then
requires at least 10 ms of monotonic spacing. A regular 200 Hz input therefore
retains at most every other sample. Irregular bursts can lose more samples even
when their long-term average is low. Discarded samples are never saved for a
catch-up burst. Zero Hz means no frequency sampling; it does not disable a topic.
Reception and decoding still consume resources before selection.

Use `bag.observations.max_hz` for joint bbox/LOS selection. Separate member-rate
overrides are rejected. The OFF pair bypasses sampling. Trigger, arm, offboard,
mission start/state, tracker/radar state, failsafe, gimbal mode and status text
are protected events and cannot be frequency sampled. Control commands default
to full rate but permit an explicit ordinary rate override.

Published debug images default to 2 Hz, with `bag.image.bytes_per_second` and
`bag.image.burst_bytes` both 16777216. The byte allowance replenishes with elapsed
time; only admitted images consume it. An image exceeding the burst capacity is
oversized and an unexpected gap. Byte-budget omissions are intentional. Recording
does not enable `visualized_result` or produce preview images. An image-free
session contains no image topic metadata. Production keeps publication disabled.

## Ownership, capacity and delivery

Pending and in-flight message references remain charged until writing releases
them. The global bounds are 256 message slots and 64 MiB of accounted payload.
Ordinary work leaves 16 slots and 1 MiB for protected events and the final pair.
Each ordinary entry has 32 slots and 8 MiB; images have four slots and 32 MiB.
`bag.topics.<entry>.max_messages` and `.max_bytes` may lower those defaults.
Pairs consume two slots and are admitted together. Admission never waits for
storage. Scheduling allows at most eight consecutive control items while ordinary
work is available, and preserves ordering within each entry.

Accounted payload includes object storage and dynamic string/vector capacities,
including image pixels, array layouts and nested detections. This is not a total
process-memory guarantee. ROS middleware buffers, typed decoding, allocator
overhead, serialization scratch space and the separately bounded 64 KiB idle
gimbal-mode cache consume additional memory. A known cached mode is offered once
at session start with snapshot origin and its original receipt time recorded.

Body IMU uses best effort/depth 100; gimbal uses best effort/depth 10; images use
best effort/depth 1. Odometry, RC, pressure, temperature, altitude, GPS, motor RPM
and command streams use best effort/depth 10. Flight, extended, battery and
low-level feedback states use reliable/depth 10.
Gimbal mode uses reliable retained delivery/depth 1. Remaining events and optional
radar use reliable/depth 10. The catalog records these delivery settings. Separate
best-effort recorder and algorithm subscriptions can receive different samples.
The bag therefore describes recorder receipt, not exact algorithm consumption.

## Storage and summaries

`bag.max_file_bytes` defaults to 256 MiB. Additional SQLite files continue the
same session; read the complete bag, not one arbitrarily selected file. A target
file size can be crossed by one message and database overhead. `bag.max_total_bytes`
defaults to 5 GiB across owned `drone_rosbag_` directories, including incomplete
bags and summaries. Existing evidence is never deleted automatically. Symlinks
are not followed while accounting for owned directories.

For example, 256 MiB splitting permits many files in a 2 GiB session while the
5 GiB total quota also counts 3 GiB of older sessions. A split does not reset the
quota. `bag.min_free_bytes` independently defaults to 5 GiB on the actual
filesystem. Original-camera video has its separate 20 GiB quota in the production
template, but both writers can consume the same disk. Free space is observed,
not atomically reserved across writers. Storage protection stops bag admission
without stopping otherwise healthy tracking, HDMI, video or diagnostics.

Each newly owned session directory receives versioned `session.json` metadata.
It identifies software/configuration, session boundaries, frozen catalog,
delivery/limits, mode snapshot origin, queue peaks and per-entry counters. Quiet
catalog entries remain visible as not observed. The counters distinguish offered,
policy-skipped, byte-limited, oversized, queue-dropped, accepted, written,
timestamp fallbacks and accepted-but-unwritten work. Supported middleware loss
and incompatible-delivery diagnostics do not prove unknown publisher-level loss.

`complete` means every admitted message was written and closed without detected
unexpected gaps under the selected policy. Policy sampling alone does not make a
session incomplete. Queue loss is `incomplete`; open/write/close/summary errors
are `failed`; a detected storage guard is `storage_limited`. A mid-write failure
can interrupt a pair after its first member reaches disk. Atomic admission is
not a two-message disk transaction. File presence alone proves neither complete
recording nor power-loss durability. Summary failures remain visible in node logs.

## Build and validation inputs

RK3576 recording always needs the real target-architecture `quadrotor_msgs`, even
with `DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK=OFF`. Build it through the supported
workflow, for example:

```bash
./build.sh --test --quadrotor-msgs /absolute/quadrotor_msgs
```

The default input is the sibling `../quadrotor_msgs`. Native generators remain
separate from ARM64 generated libraries. The manager is linked only on RK3576;
Jetson retains its existing recorder. Interface baseline identities are tracker
`e17c43e`, bridge `31b00e8`, interceptor `73c605c`, and messages `bc8bf6c`. They are
source-review identities, not current hardware acceptance. Retain actual source,
configuration, model/input, Runtime and board identities for each run. Preserve
required binaries and raw evidence outside disposable build caches.
