# Spec Delta

## MODIFIED Requirements

### Requirement: 外部遥测经已有消息类型输入

The system SHALL consume the existing external messages listed below and support ROS topic remapping. A topic is a named message channel. These inputs MUST NOT replace the in-process path for local video frames, target boxes, or tracking state.

| Topic | Message type | Display use |
|---|---|---|
| `/mavros/imu/data` | `sensor_msgs/msg/Imu` | Heading, pitch, and roll |
| `/mavros/state` | `mavros_msgs/msg/State` | Connection, mode, arming, timing, and OFFBOARD confirmation through `guided` |
| `/mavros/extended_state` | `mavros_msgs/msg/ExtendedState` | Landed state |
| `/mavros/rc/in` | `mavros_msgs/msg/RCIn` | 生产遥控接收链路质量百分比 |
| `/mavros/battery` | `sensor_msgs/msg/BatteryState` | Voltage, current, and charge |
| `/mavros/gpsstatus/gpsraw` | `mavros_msgs/msg/GPSRAW` | Satellites, ground speed, and course |
| `/mavros/global_position/global` | `sensor_msgs/msg/NavSatFix` | Position and return-home calculation |
| `/fpv/static_pressure` | `sensor_msgs/msg/FluidPressure` | Barometric altitude |
| `/fpv/temperature_baro` | `sensor_msgs/msg/Temperature` | Existing temperature telemetry input |
| `/fpv/failsafe` | `std_msgs/msg/Bool` | Failsafe warning |
| `/fpv/radar_state` | `std_msgs/msg/Int8` | Independent radar state |
| `/fpv/low_level_feedback` | `quadrotor_msgs/msg/LowLevelFeedback` | Optional battery warning level |
| `/fpv/mission_state` | `std_msgs/msg/Bool` | Interceptor mission start and end events |

The system SHALL use the existing `/fpv/tracker_trigger` `ON` and `OFF` events for their limited-duration trigger hints. A `MISSION` trigger SHALL be treated as a request rather than OFFBOARD confirmation. Mission display SHALL follow `osd-presentation` using existing mission and flight messages. `OFF` and `DISMISS` SHALL also cancel mission display. Trigger messages SHALL retain `std_msgs/msg/Header`. The tracker MUST NOT add a control topic or change any external publisher to obtain display state. Without compiled low-level-feedback support, dependent fields SHALL remain hidden while other telemetry continues working.

#### Scenario: 没有自定义消息包
- **WHEN** low-level-feedback support is disabled at build time and standard telemetry is received
- **THEN** standard telemetry and mission confirmation work without requiring `quadrotor_msgs` solely for this change

#### Scenario: 雷达正在跟踪而本地检测中
- **WHEN** radar state is 2 while the local paired result is Detecting
- **THEN** both states remain distinct and the local box is not promoted to Tracking

#### Scenario: 收到任务触发
- **WHEN** `/fpv/tracker_trigger` carries `MISSION` without both an active-mission event and valid OFFBOARD confirmation
- **THEN** the status bar does not display `ON MISSION`
- **THEN** no new ROS topic or message type is created

#### Scenario: 生产遥控输入允许丢包并支持重映射
- **WHEN** `/mavros/rc/in` 被重映射到隔离测试话题，该话题以 best-effort 发布 `mavros_msgs/msg/RCIn`
- **THEN** OSD 接收 RSSI，不改变本地跟踪状态或帧框配对

#### Scenario: Existing OFFBOARD variant is reported
- **WHEN** mission `true` is received and fresh flight status reports `connected=true`, `armed=true`, `guided=true`, and mode `OFFBOARD|ANGLE`
- **THEN** mission display accepts the OFFBOARD confirmation without requiring exact mode-string equality

### Requirement: 未知和过期数据不能显示为有效零值

For periodically reported telemetry, the system SHALL retain validity and the most recent valid receipt time. It SHALL reject non-finite numbers, invalid attitudes, and message-defined unknown markers. Freshness SHALL use a monotonic clock, meaning elapsed time unaffected by calendar-clock corrections. The default `osd.telemetry_timeout_ms` SHALL remain 2000 ms. Missing, invalid, or expired measurements SHALL be hidden or represented as unknown instead of valid zero. Derived measurement displays SHALL require all their measurement inputs to be valid and fresh.

Mission status is a transition event, meaning that healthy missions can remain silent between start and end. The system SHALL remember a received mission Boolean locally without applying a telemetry-age timeout to it. Flight-status freshness SHALL still gate mission visibility. Repeating a telemetry snapshot MUST NOT refresh the receipt time of the underlying flight report. Before any mission event, mission state SHALL remain unknown and MUST NOT be inferred from OFFBOARD alone.

#### Scenario: 姿态过期
- **WHEN** no valid attitude arrives for more than 2000 ms while battery reports continue
- **THEN** attitude is shown as unavailable and fresh battery display continues

#### Scenario: 系统时钟跳变
- **WHEN** calendar time is corrected while a telemetry interruption lasts only 100 ms
- **THEN** freshness is based on the elapsed 100 ms rather than the calendar jump

#### Scenario: A healthy mission is silent
- **WHEN** one mission `true` is received, no later mission event arrives for 60 seconds, and connected armed OFFBOARD flight reports keep arriving within the timeout
- **THEN** the mission banner remains eligible for all 60 seconds
- **THEN** no mission heartbeat or periodic mission publication is required

#### Scenario: Old flight status is repeatedly summarized
- **WHEN** a flight report is received once and telemetry summaries continue for more than 2000 ms without another flight report
- **THEN** flight status expires according to its original receipt time and cannot keep `ON MISSION` eligible

### Requirement: 告警和冻结提示同时可辨识

The system SHALL prioritize connection loss, failsafe, critical battery, and low battery in that order. Unknown or expired sources SHALL not continue asserting an old warning as current. Frozen/waiting hints SHALL have their own readable region. Telemetry SHALL be summarized at 10 Hz and SHALL keep updating after `OFF` while the display backend remains healthy.

RK3576 production camera input with `osd.enabled=true` SHALL keep live preview after `OFF`. It MUST NOT add an input-stopped freeze message in this mode. Tracking-phase hints, warnings, and confirmed mission display SHALL remain independently readable. `OFF` SHALL clear mission display without waiting for a new tracker-state publication. ON/OFF handling and mission/flight display updates MUST NOT wait for inference or recording cleanup. Other input modes SHALL retain their existing freeze behavior.

#### Scenario: 冻结时收到失效保护
- **WHEN** non-continuous video is frozen after `OFF` and fresh connected telemetry reports failsafe
- **THEN** the freeze hint and failsafe warning remain readable without restoring old boxes or a cancelled mission banner

#### Scenario: 实时预览与告警并存
- **WHEN** continuous preview receives `OFF` and then fresh failsafe telemetry
- **THEN** new preview images, tracking-off indication, and the warning are displayed together with cleared target decorations and no input-stopped freeze hint

#### Scenario: 任务覆盖期间停止
- **WHEN** continuous preview shows `ON MISSION`, receives `OFF`, and then receives battery updates without a new tracker-state publication
- **THEN** mission text clears within 500 ms on a healthy display, tracking-off indication remains readable, and battery updates continue

## ADDED Requirements

### Requirement: Received mission and flight transitions remain effective

The display SHALL process each received mission Boolean and each received flight-authorization change before a later received event can supersede it. A received end or OFFBOARD exit MUST NOT disappear merely because another event arrives before the next telemetry summary or screen refresh. The tracker SHALL retain compatible reliable, volatile subscriptions to the existing publishers. It MUST NOT require publisher QoS changes.

#### Scenario: Mission end and new start occur between summaries
- **WHEN** mission `true`, `false`, and then `true` are all received before the next 10 Hz telemetry summary
- **THEN** the received `false` terminates the earlier attempt and the later `true` is considered as a new attempt requiring flight confirmation
- **THEN** there is no requirement to flash an intermediate banner for an attempt that never reached hardware

#### Scenario: Brief OFFBOARD exit is received
- **WHEN** a confirmed mission receives flight `guided=false` and then `guided=true` before the next display refresh
- **THEN** the OFFBOARD exit invalidates the old attempt and the later flight report alone does not restore its banner

### Requirement: Unobserved mission state stays unconfirmed

After tracker startup the system SHALL keep the banner hidden until it receives a mission `true` and valid flight confirmation. It MUST NOT infer a missed start from a trigger, target lock, mode text, or OFFBOARD status alone. Documentation SHALL state that missed mission transitions and interceptor death during continuing valid OFFBOARD cannot be independently detected without a different external status contract.

#### Scenario: Tracker restarts during an existing mission
- **WHEN** the tracker starts after the interceptor's last mission `true` and receives only armed OFFBOARD flight reports
- **THEN** the banner remains hidden until a mission start is actually received

#### Scenario: Interceptor stops without an end event
- **WHEN** the tracker has confirmed a mission, no mission `false` arrives, and the bridge continues publishing fresh connected armed OFFBOARD status
- **THEN** these inputs do not by themselves prove mission termination and the banner can remain visible
- **THEN** a later received OFFBOARD exit, disarm, disconnection, or flight-status expiry still clears it
