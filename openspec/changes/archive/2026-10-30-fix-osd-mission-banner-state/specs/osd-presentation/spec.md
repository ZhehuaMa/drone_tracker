# Spec Delta

## MODIFIED Requirements

### Requirement: 无效框清除且状态含义不变

The system SHALL clear the previous target box when the current packet has no valid display box. It SHALL display only the existing single target. It MUST NOT invent confidence or target identifiers. It SHALL preserve Detecting=0, TargetFound=1, Tracking=2, and TargetLost=3. It SHALL use white, cyan, green, and red for those states respectively. A valid Tracking box SHALL retain its recognizable lock styling.

Stopping, waiting, and input failure MUST NOT be rewritten as algorithm TargetLost. Preview SHALL explicitly represent the absence of an algorithm result. It MUST NOT use a default zero to impersonate Detecting. Continuous-preview tracking phases SHALL use `TRK OFF`, `TRK STARTING`, or `TRK ERROR` without adding ROS tracking-state values. Tracking-phase hints SHALL remain independently readable when a confirmed mission occupies the status bar. `OFF` SHALL clear the mission banner independently of tracker-state publication.

#### Scenario: 有框切换为无框
- **WHEN** the previous packet has a target box and the next consumed packet has no valid display box
- **THEN** the next display contains no previous box and uses the tracking state belonging to the next packet

#### Scenario: 数据没有置信度
- **WHEN** the tracking result provides a box and state without confidence or a target identifier
- **THEN** the display contains neither invented confidence nor an invented identifier

#### Scenario: 停止时保留任务文字
- **WHEN** continuous preview displays a Tracking box and `ON MISSION`, then receives `OFF`
- **THEN** target decorations are cleared and the independent phase hint reports tracking stopped
- **THEN** `ON MISSION` is removed within 500 ms on a healthy display without waiting for a tracker-state publication

### Requirement: 任务触发覆盖 HUD 跟踪状态文字

The HUD, meaning the text and graphics over the video, SHALL show `ON MISSION` only for a locally observed active interceptor mission with fresh, connected, armed OFFBOARD confirmation. The active-mission event SHALL come from `/fpv/mission_state=true`. Flight confirmation SHALL come from `/mavros/state` with `connected=true`, `armed=true`, and `guided=true`. In this deployment `guided` reports actual OFFBOARD status, including the `OFFBOARD|ANGLE` mode. A `MISSION` trigger alone MUST NOT activate the banner.

After an active-mission event, fresh connected and armed status with `guided=false` SHALL leave the display pending until OFFBOARD is first confirmed. Pending means that the normal tracking text remains visible. Once OFFBOARD has been confirmed for that attempt, a received `guided=false` SHALL end that display attempt. A received mission `false`, disarming, disconnection, flight-status expiry, `OFF`, or `DISMISS` SHALL end a pending or active display attempt. Missing flight status before any report SHALL keep a new attempt pending without asserting OFFBOARD.

Tracking-state publication MUST NOT determine the mission banner lifetime. Mission display SHALL change only the status-bar text. It MUST NOT change paired tracking state, target-box color, or lock styling. State value `3` SHALL retain its meaning of TargetLost. `ON` and unrecognized trigger strings MUST NOT create a mission attempt. `DISMISS` SHALL leave target tracking running. After removal, the status bar SHALL use the current tracking result or the existing tracking-phase text. Both HDMI rendering paths SHALL follow the same mission rules.

#### Scenario: 跟踪期间进入任务
- **WHEN** the status bar reads `TRK TRACKING`, a mission `true` is received, and flight status confirms fresh connected armed OFFBOARD
- **THEN** the status bar displays `ON MISSION`
- **THEN** additional video frames and expiry of the two-second trigger hint do not terminate the banner

#### Scenario: 新跟踪状态结束任务覆盖
- **WHEN** the status bar reads `ON MISSION`, flight confirmation remains valid, and the tracker publishes state `3` while the interceptor still considers its mission active
- **THEN** `ON MISSION` remains visible and target rendering follows the existing red TargetLost rules

#### Scenario: 非任务触发不覆盖状态栏
- **WHEN** the display reads `TRK TRACKING` and receives `ON`, `OFF`, or an unrecognized string without an active-mission event
- **THEN** no mission banner is started

#### Scenario: Request alone is not confirmation
- **WHEN** `MISSION` is received while flight status is armed `ACRO`, without a mission `true`
- **THEN** the status bar continues to show the existing tracking text

#### Scenario: Initial ACRO report does not cancel a pending start
- **WHEN** mission `true` is received with fresh connected armed `guided=false`, followed by fresh connected armed `guided=true`
- **THEN** the display first remains on tracking text and then shows `ON MISSION`

#### Scenario: Mission finishes while its target remains tracked
- **WHEN** `ON MISSION` is visible with Tracking=2 and mission `false` is received without any changed tracker state
- **THEN** the display returns to `TRK TRACKING` within 500 ms on a healthy display

#### Scenario: Confirmed OFFBOARD is lost
- **WHEN** `ON MISSION` is visible and a fresh flight report changes `guided` from true to false while mission `false` has not arrived
- **THEN** the banner is cleared and the previous attempt cannot resume merely because flight mode later returns to OFFBOARD

#### Scenario: Immediate mission completion during entry
- **WHEN** mission `true` and then mission `false` arrive 30 ms apart before any OFFBOARD confirmation
- **THEN** the attempt ends without displaying an unconfirmed mission banner

#### Scenario: Dismissal preserves tracking
- **WHEN** `DISMISS` is received during a confirmed mission with a valid tracked target
- **THEN** the banner clears while target tracking and its normal paired display continue

### Requirement: HDMI 状态切换诊断日志

The system SHALL log received triggers, changed tracker-state publications, mission-display transitions, and requests to end a tracking round or video stream. Mission-display logs SHALL distinguish a pending start, confirmed activation, explicit mission end, OFFBOARD loss, disarm, disconnection, flight-status expiry, and trigger cancellation. The system SHALL log changes to the text or display status of frames successfully submitted to HDMI. Receipt and policy decisions MUST NOT be represented as successful screen updates.

Visible target labels remain removed. Display logs SHALL retain `target_label=""`. Input target names or fallback tracker-state labels MUST NOT be logged as visible labels. Changing only an input label MUST NOT create an additional display-transition log.

Logs SHALL identify the tracking round and frame sequence where available. Continuous preview SHALL also identify the video stream, original source sequence, and packet kind. Stop logs SHALL distinguish preview continuing after tracking stops from an input-stop freeze. They SHALL report target clearing. Stable frames and repeated unchanged status messages MUST NOT produce repeated transition logs. Mission-display revisions and monotonic transition times SHALL allow a cancellation decision to be correlated with the later screen update.

#### Scenario: 跟踪中收到任务触发
- **WHEN** `MISSION` is received during tracking, followed by mission `true` and OFFBOARD confirmation
- **THEN** logs distinguish trigger receipt, pending mission, confirmed activation, and the successful `ON MISSION` screen update
- **THEN** display logs retain an empty `target_label`

#### Scenario: 跟踪中收到停止触发
- **WHEN** non-continuous video receives `OFF` during tracking
- **THEN** logs associate the stop request, mission cancellation, `Stopped` display state, frozen background, and cleared target decorations
- **THEN** subsequent display logs retain an empty `target_label`

#### Scenario: 输入含标签但画面没有标签
- **WHEN** successful Tracking displays receive either a nonempty target name or an empty input label
- **THEN** the status bar follows confirmed mission or tracking state and display logs always contain `target_label=""`

#### Scenario: 仅输入标签改变
- **WHEN** video continues with unchanged status text, display state, geometry, and other transition conditions, but the input label changes
- **THEN** there is no label-only display-transition log and no change to visible target decoration

#### Scenario: 持续预览停止日志
- **WHEN** `OFF` is received and subsequent preview images are displayed
- **THEN** logs distinguish cancellation receipt, mission-banner removal, target clearing, and cleanup completion while preserving stream identity and increasing source sequence

#### Scenario: Mission completion is not confused with target loss
- **WHEN** a mission `false` clears the banner while Tracking=2 remains unchanged
- **THEN** logs identify `mission_false` as the cause and do not report a synthetic tracker-state publication

## ADDED Requirements

### Requirement: Ended mission attempts require new authorization

A display attempt is one locally observed mission start and its subsequent termination. The system SHALL invalidate an attempt on cancellation or confirmed loss of its flight authorization. Repeated mission `true`, a later `MISSION` trigger, or flight-mode recovery MUST NOT revive an invalidated attempt. After invalidation the system SHALL require a received mission `false` and a later `true` to establish another attempt.

#### Scenario: Old true repeats after OFFBOARD loss
- **WHEN** a confirmed attempt loses OFFBOARD and then receives another mission `true` and fresh armed OFFBOARD
- **THEN** `ON MISSION` remains hidden
- **THEN** receipt of mission `false` followed by a later `true` permits a new attempt subject to flight confirmation

#### Scenario: Cancellation arrives before a delayed start event
- **WHEN** `DISMISS` invalidates an attempt before its delayed mission `true` arrives
- **THEN** that `true` does not reactivate the banner
- **THEN** the display waits for a subsequent mission `false` followed by `true`

### Requirement: Mission visibility updates independently of video

On a healthy display the system SHALL remove `ON MISSION` within 500 ms of receiving a terminating event. Flight-status expiry SHALL remove it no later than `osd.telemetry_timeout_ms + 500 ms` after the last received flight report. Updates MUST NOT wait for a new frame, tracker-state publication, inference completion, or recording cleanup. An obsolete prepared overlay MUST NOT begin hardware submission after cancellation. Already-authorized hardware work SHALL finish safely.

#### Scenario: No new tracking results
- **WHEN** inference is blocked after an `ON MISSION` frame has been displayed and mission `false` arrives
- **THEN** the banner is removed over the retained video within 500 ms without waiting for inference

#### Scenario: Flight reports stop
- **WHEN** a confirmed mission receives no further flight report with the default timeout of 2000 ms
- **THEN** the banner is absent no later than 2500 ms after the last flight-report receipt

#### Scenario: Cancellation races with a prepared overlay
- **WHEN** an overlay containing `ON MISSION` is prepared, cancellation is accepted, and that overlay has not yet been authorized for hardware
- **THEN** the obsolete overlay is rejected and the next accepted overlay omits the banner

#### Scenario: Banner removal does not invalidate video
- **WHEN** a valid current-round video packet is pending and only mission-display state changes
- **THEN** the video packet remains eligible under the existing frame/round rules and can be displayed with the latest overlay
