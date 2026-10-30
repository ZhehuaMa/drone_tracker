# Spec Delta

## MODIFIED Requirements

### Requirement: 单目标框采用参考角框和锁定装饰

The system SHALL draw reference-style corner boxes for the current valid target. A target label means text attached to a target box together with its background and border. The system MUST NOT draw target-label text, backgrounds, or borders, whether the input label is populated or empty.

Tracking means local tracking-state value `2`. In that state the system SHALL retain outer lock corners and the central diamond. The existing reference-style opacity pulse SHALL remain permitted. Target colors SHALL preserve the existing white/cyan/green/red mapping. Corner dimensions, line widths, and positions SHALL retain their existing rules. Display boxes SHALL use the existing paired frame result and video geometry. Decorations SHALL be clipped only to the valid video region. Missing confidence and target identifiers MUST NOT be replaced by invented percentages, identifiers, or a second simulated target.

The HUD is the flight, tracking, and warning overlay independent of target boxes. Previously specified coordinate, arming, trigger-placement, and RSSI changes SHALL remain applicable. Other HUD content, position, font, color, and transparency SHALL retain their existing rules except for the mission-banner lifetime defined by `osd-presentation`. `ON MISSION` SHALL require a received active-mission event and fresh connected armed OFFBOARD confirmation. Removing a target label SHALL reveal the original underlying video or HUD; the system MUST NOT cover its former region with black.

The existing `cached-overlay` and `legacy` HDMI rendering paths SHALL follow the same label-removal and mission-display rules. Without a valid box, or while the picture is waiting or frozen, the system SHALL clear all target decorations.

#### Scenario: 有效 Tracking 单目标
- **WHEN** a frame contains one valid box and Tracking state without confidence or a target identifier
- **THEN** the same HDMI update displays green corners, outer lock corners, and a central diamond
- **THEN** it contains no target-label text, background, border, or invented field

#### Scenario: 输入提供目标名称或空标签
- **WHEN** identical valid Tracking boxes carry `TRACKING`, another nonempty target name, or an empty label
- **THEN** all three target-decoration outputs are identical and contain no label panel or fallback state text

#### Scenario: 保留独立状态文字与其他 OSD 项目
- **WHEN** labels are removed with fixed video, boxes, telemetry, and draw time while the status bar reads `TRK TRACKING`
- **THEN** all unrelated HUD elements retain their specified appearance, including existing coordinate, arming, trigger-placement, and RSSI behavior
- **THEN** pixels outside the former label region remain unchanged when comparing label removal alone, and the former region reveals its original underlying content

#### Scenario: 任务文字覆盖继续有效
- **WHEN** a frame contains a valid Tracking box and mission state plus flight status satisfy the confirmation rule
- **THEN** the upper-right status bar reads `ON MISSION` while corners and lock styling retain their existing appearance and no label panel is drawn

#### Scenario: 两条路径输出一致且移动不留残影
- **WHEN** identical input is drawn through `legacy` and `cached-overlay` and the target subsequently moves
- **THEN** each path produces the same visible graphics and leaves no previous target decoration or label pixels

#### Scenario: 目标丢失或停止
- **WHEN** the next frame has no valid box or the display enters waiting, stopped, EOF, or input-failure state
- **THEN** old corners, outer corners, and diamonds are absent with no target-label residue

#### Scenario: Mission completion preserves a continuing target
- **WHEN** mission `false` is received while the current valid paired target remains Tracking=2
- **THEN** both rendering paths replace `ON MISSION` with `TRK TRACKING` without changing the paired target's geometry or styling

### Requirement: 底部解锁状态与限时触发提示具有独立位置

The system SHALL display `ARMED`, `DISARMED`, or `ARM --` at the bottom center. These texts SHALL use the same main font and black outline as `MAH`. Each text rectangle SHALL remain horizontally centered. Limited-duration `TRIG ON` and `TRIG OFF` hints SHALL occupy the existing left-column space below the subtitle and above altitude. Their existing two-second duration SHALL remain unchanged. Trigger, mission, tracking-phase, freeze, and warning text MUST NOT cover the arming indicator. Arming text, colors, and validity SHALL follow `osd-ros-telemetry`.

Receiving fresh connected `armed=false` SHALL remove a pending or active mission banner. The arming indicator SHALL remain independent of tracker start/stop. Both rendering paths SHALL apply the same disarm and mission-display rules.

#### Scenario: 已解锁且触发提示生效
- **WHEN** fresh connected flight status reports `armed=true` and `ON` is received
- **THEN** green `ARMED` remains at the bottom center with independent `TRIG ON` in the left column
- **THEN** trigger-hint expiry does not remove `ARMED`

#### Scenario: 未解锁且任务覆盖生效
- **WHEN** the upper-right status bar reads `ON MISSION` and fresh connected flight status reports `armed=false`
- **THEN** the bottom center displays white `DISARMED`
- **THEN** the mission banner is removed within 500 ms on a healthy display without waiting for a tracker-state publication

#### Scenario: 未知状态仍有固定占位
- **WHEN** flight status expires while a tracking-start hint is visible
- **THEN** the bottom center displays amber `ARM --` and the phase hint remains in its independent region
- **THEN** an old mission banner does not remain eligible after the flight-status timeout
