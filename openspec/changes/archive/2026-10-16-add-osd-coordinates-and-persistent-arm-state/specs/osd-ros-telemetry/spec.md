# Spec Delta

## ADDED Requirements

### Requirement: 经纬度采用带符号的十进制度数

OSD（视频上的屏幕信息叠加）SHALL 使用 `/mavros/global_position/global` 的 `sensor_msgs/msg/NavSatFix` 位置显示纬度与经度。十进制度数指直接以度为单位表示坐标，不转换为度分秒。有效新鲜坐标 SHALL 分别显示为 `LAT <纬度>` 与 `LON <经度>`。数值 SHALL 显式包含正负号、英文小数点和六位小数。负号 SHALL 表示南纬或西经。显示精度 MUST NOT 被解释为定位精度。坐标显示 MUST NOT 依赖已解锁、返航点或单独的 GPSRAW 消息。

#### Scenario: 有效北纬东经
- **WHEN** 有效新鲜位置为纬度 `31.2304`、经度 `121.4737`
- **THEN** 两行分别显示 `LAT +31.230400` 与 `LON +121.473700`

#### Scenario: 有效南纬西经和极值
- **WHEN** 有效新鲜位置为纬度 `-90`、经度 `-180`
- **THEN** 两行完整显示 `LAT -90.000000` 与 `LON -180.000000`，不省略符号或小数

#### Scenario: 零坐标是实际有效数据
- **WHEN** 消息具有有效定位状态，纬度与经度均为 `0`
- **THEN** 显示 `LAT +0.000000` 与 `LON +0.000000`，不误判为缺失值

#### Scenario: 舍入后为零不显示负零
- **WHEN** 有效新鲜位置为纬度 `-0.0000001`、经度 `-0.0`
- **THEN** 两行显示 `LAT +0.000000` 与 `LON +0.000000`

#### Scenario: 未解锁且没有返航点
- **WHEN** 定位有效新鲜，飞行器未解锁，且尚未记录返航点或接收 GPSRAW 消息
- **THEN** 仍显示实际经纬度，不要求先建立返航点或接收卫星状态

### Requirement: 无效或过期位置替换为坐标占位文字

坐标 SHALL 沿用既有定位有效性和单调时钟新鲜度规则，默认期限为 `osd.telemetry_timeout_ms=2000` 毫秒。未收到定位、消息报告无定位、任一坐标非有限或越界、或位置过期时，两行 SHALL 显示 `LAT --` 与 `LON --`。纬度合法范围 SHALL 为 `[-90,90]` 度，经度合法范围 SHALL 为 `[-180,180]` 度。旧数字 SHALL 在下一次显示更新时清除。系统 MUST NOT 保留旧坐标或生成零坐标来填补缺失数据。

#### Scenario: 无定位或非法数值
- **WHEN** 已显示有效坐标，随后收到无定位状态、非有限数值、纬度 `91` 或经度 `181` 的位置消息
- **THEN** 下一次显示更新将两行替换为 `LAT --`、`LON --`，没有旧数字残影

#### Scenario: 定位过期而其他遥测正常
- **WHEN** 使用默认超时，位置已超过 2000 毫秒没有更新，但电池仍更新
- **THEN** 坐标显示占位文字，电池仍显示当前数值

#### Scenario: 定位恢复
- **WHEN** 占位文字已显示，随后收到有效新鲜位置 `(31.2304,121.4737)`
- **THEN** 下一次显示更新恢复两行实际坐标

### Requirement: 解锁状态按有效飞行状态持续显示

系统 SHALL 使用 `/mavros/state` 的 `mavros_msgs/msg/State.armed` 显示解锁状态。该消息新鲜且 `connected=true` 时，`armed=true` SHALL 持续显示绿色 `ARMED`，`armed=false` SHALL 持续显示白色 `DISARMED`。文字 MUST NOT 因解锁时长、闪烁周期或跟踪 `ON`、`OFF`、`MISSION` 触发而消失。解锁状态 SHALL 在实时跟踪、无框预览、等待及保留底图的冻结阶段继续按最新遥测更新。跟踪启停 MUST NOT 被解释为飞行器解锁或上锁。

#### Scenario: 解锁超过三秒且触发停止跟踪
- **WHEN** 新鲜连接状态持续报告 `armed=true`，解锁已过去 75 秒，并收到跟踪 `OFF`
- **THEN** 显示继续保持绿色 `ARMED`，不受触发提示或跟踪停止影响

#### Scenario: 未解锁持续可见
- **WHEN** 新鲜连接状态持续报告 `armed=false`，期间收到 `ON` 或 `MISSION`
- **THEN** 显示持续保持白色 `DISARMED`，不因跟踪或任务状态被改为 `ARMED`

#### Scenario: 冻结画面上发生上锁
- **WHEN** 显示保持冻结底图，新的有效连接状态从 `armed=true` 改为 `armed=false`
- **THEN** 底部状态由 `ARMED` 更新为 `DISARMED`，底图继续冻结，旧目标框不恢复

### Requirement: 未知解锁状态不能冒充已上锁或已解锁

尚未收到飞行状态、状态过期或新鲜消息报告 `connected=false` 时，底部状态 SHALL 持续显示琥珀色 `ARM --`。`ARM --` 表示当前无法确认解锁状态。系统 MUST NOT 将未知数据默认显示为 `DISARMED`，也 MUST NOT 把失联前的 `ARMED` 作为当前确定状态。状态恢复后，显示 SHALL 按新的有效连接消息更新。此显示规则 MUST NOT 修改飞行控制状态、解锁计时或返航点建立逻辑。

#### Scenario: 启动时尚无飞行状态
- **WHEN** OSD 已启动且尚未收到 `/mavros/state`
- **THEN** 底部显示 `ARM --`，不显示默认的 `DISARMED`

#### Scenario: 状态过期或断连
- **WHEN** 已显示 `ARMED` 或 `DISARMED`，随后状态过期或收到 `connected=false`
- **THEN** 下一次显示更新改为 `ARM --`，现有连接告警仍按原规则显示

#### Scenario: 连接状态恢复
- **WHEN** 当前显示 `ARM --`，随后收到新鲜 `connected=true, armed=false` 消息
- **THEN** 下一次显示更新改为白色 `DISARMED`
