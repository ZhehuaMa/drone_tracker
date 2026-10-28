# OSD 外部遥测规格

## Purpose

将飞行系统通过 ROS 2 提供的姿态、电池、位置和环境信息转为可读屏幕元素。为未收到、非法及过期数据建立明确显示规则，使冻结视频期间仍能查看最新遥测，同时不混淆外部雷达状态与本地跟踪状态。

## Requirements

### Requirement: 外部遥测经已有消息类型输入

系统 SHALL 使用下表话题和消息类型接收外部数据，并支持 ROS 话题重映射。遥测输入 SHALL 不改变本地帧、框和跟踪状态的进程内数据通路。

| 话题 | 消息类型 | 用途 |
|---|---|---|
| `/mavros/imu/data` | `sensor_msgs/msg/Imu` | 航向、俯仰、横滚 |
| `/mavros/state` | `mavros_msgs/msg/State` | 连接、模式、解锁与计时 |
| `/mavros/extended_state` | `mavros_msgs/msg/ExtendedState` | 落地状态 |
| `/mavros/rc/in` | `mavros_msgs/msg/RCIn` | 生产遥控接收链路质量百分比 |
| `/mavros/battery` | `sensor_msgs/msg/BatteryState` | 电压、电流、电量 |
| `/mavros/gpsstatus/gpsraw` | `mavros_msgs/msg/GPSRAW` | 卫星、地速、航迹 |
| `/mavros/global_position/global` | `sensor_msgs/msg/NavSatFix` | 定位与返航计算 |
| `/fpv/static_pressure` | `sensor_msgs/msg/FluidPressure` | 气压高度 |
| `/fpv/temperature_baro` | `sensor_msgs/msg/Temperature` | 温度接收保留，不再绘制温度行 |
| `/fpv/failsafe` | `std_msgs/msg/Bool` | 失效保护告警 |
| `/fpv/radar_state` | `std_msgs/msg/Int8` | 独立雷达状态 |
| `/fpv/low_level_feedback` | `quadrotor_msgs/msg/LowLevelFeedback` | 可选电池告警等级 |

系统 SHALL 复用现有 `/fpv/tracker_trigger` 的 `ON`、`OFF` 接收事件显示限时触发提示。收到 `frame_id="MISSION"` 时，系统 SHALL 按 `osd-presentation` 的任务状态栏要求显示 `ON MISSION`。这三种触发值 SHALL 沿用现有 `std_msgs/msg/Header` 消息类型，不增加独立控制通路。低层反馈适配未编译时 SHALL 隐藏依赖该输入的显示，其他遥测仍可工作。

#### Scenario: 没有自定义消息包
- **WHEN** 构建未启用低层反馈适配，标准电池与姿态话题正常到达
- **THEN** 正常显示电池与姿态，低层反馈告警等级显示未知或隐藏，不阻止 OSD 启动

#### Scenario: 雷达正在跟踪而本地检测中
- **WHEN** 外部雷达状态为 2，本地提交包状态为 Detecting
- **THEN** 显示两者各自状态，不将本地框或跟踪状态改为 Tracking

#### Scenario: 收到任务触发
- **WHEN** `/fpv/tracker_trigger` 的 `frame_id` 为 `MISSION`
- **THEN** HUD 状态栏显示 `ON MISSION`，不创建新的 ROS 话题或消息类型


#### Scenario: 生产遥控输入允许丢包并支持重映射
- **WHEN** `/mavros/rc/in` 被重映射到隔离测试话题，该话题以 best-effort 发布 `mavros_msgs/msg/RCIn`
- **THEN** OSD 接收 RSSI，不改变本地跟踪状态或帧框配对

### Requirement: 未知和过期数据不能显示为有效零值

系统 SHALL 为每组遥测保留有效性与最近有效接收时间。接收时 SHALL 拒绝非有限值、无效姿态和消息定义的未知标记。新鲜度 SHALL 依据单调时钟计算，默认 `osd.telemetry_timeout_ms=2000`。未收到、非法或过期的数据 SHALL 显示 `--`、未知提示或隐藏相关图形，MUST NOT 默认为有效零测量。依赖多组输入的派生显示 SHALL 要求相关输入均有效且新鲜。

#### Scenario: 姿态过期
- **WHEN** 姿态最后有效接收时间距今超过 2000 ms，其他遥测仍更新
- **THEN** 水平仪停止呈现旧姿态并标为不可用，电池等新鲜数据显示不受影响

#### Scenario: 系统时钟跳变
- **WHEN** 系统日历时间被校正而某遥测实际只中断 100 ms
- **THEN** 不因日历时间跳变错误认定该遥测超时

### Requirement: 遥测转换符合输入字段语义

系统 SHALL 校验并归一化有效姿态四元数，再计算俯仰、横滚与以北为零度、顺时针递增的航向。GPS 地速与航迹 SHALL 按消息单位转换。有效定位 SHALL 用于经纬度显示及返航距离计算。

电池剩余电量 SHALL 按 `BatteryState.charge` 的 Ah 含义处理。只有有效容量和合法剩余电量同时存在时，系统 SHALL 显示两者差值换算的已耗 mAh。单节电压 SHALL 仅在已配置节数或有效单节数据存在时显示。系统 MUST NOT 用整包电压猜测节数后当作确定值。HUD 电流数值 SHALL 为有效 `BatteryState.current` 的绝对值。原始 ROS 消息 SHALL 保持不变；未知或非有限电流 SHALL 显示未知标记。

#### Scenario: 电池剩余量换算
- **WHEN** `capacity=13 Ah`、`charge=10 Ah` 且字段有效
- **THEN** 已耗电量显示为 3000 mAh，不将 10000 mAh 标为已耗电量

#### Scenario: 无有效容量
- **WHEN** 电压有效但 charge 或 capacity 未知
- **THEN** 电压正常显示，已耗 mAh 隐藏或标为未知

#### Scenario: 负电流显示为正值
- **WHEN** `/mavros/battery` 发布有效电流 `-3.0 A`
- **THEN** HUD 显示 `CUR 03.0A`，其他电池字段不因取绝对值改变

#### Scenario: 未知电流不冒充零
- **WHEN** `/mavros/battery` 电流为非有限值
- **THEN** HUD 显示 `CUR --A`，不显示 `CUR 00.0A`

### Requirement: 返航与气压高度使用有效参考

系统 SHALL 在连接和定位有效的解锁上升沿记录返航点。解除解锁后 SHALL 清除返航点。没有有效返航点时 SHALL 隐藏返航方向和距离。方向箭头 SHALL 表示返航方位相对当前机头航向的角度。

系统 SHALL 仅在未解锁、落地状态和气压均有效新鲜时更新地面气压参考。缺少参考时相对气压高度 SHALL 显示未知。解锁后 SHALL 保留地面参考而不随飞行中气压重新归零。

#### Scenario: 解锁时没有定位
- **WHEN** 解锁上升沿 GPS 不可用，之后本轮 GPS 恢复
- **THEN** 不将空坐标或后来位置冒充本轮返航点，返航元素保持不可用

#### Scenario: 飞行中气压变化
- **WHEN** 已建立地面参考并解锁，随后气压降低
- **THEN** 使用原地面参考计算相对高度，不通过更新参考把高度归零

### Requirement: 告警和冻结提示同时可辨识

系统 SHALL 按连接丢失、失效保护、电池严重、低电量的优先级显示当前有效告警。来源未知或过期时 SHALL 标明未知，不继续断言旧告警是当前状态。冻结/等待提示 SHALL 有独立显示区域，不被遥测告警覆盖。遥测快照 SHALL 以 10 Hz 汇总；OFF 后只要显示后端健康就 SHALL 继续更新。RK3576 正式 ROS 相机且 osd.enabled=true 的持续预览模式 SHALL 在 OFF 后保留实时视频，MUST NOT 附加输入停止冻结提示。跟踪关闭、启动或故障提示 SHALL 与告警和 MISSION 覆盖分别可辨认。ON/OFF 控制接收 MUST NOT 以推理或记录收尾等待阻塞遥测汇总。

#### Scenario: 冻结时收到失效保护
- **WHEN** 非持续预览模式视频因 OFF 冻结，而新鲜消息报告 failsafe=true 且连接正常
- **THEN** 屏幕同时可见冻结提示和失效保护告警，不恢复旧目标框

#### Scenario: 实时预览与告警并存
- **WHEN** 持续预览 OFF 后视频更新，新消息报告 failsafe=true 且连接正常
- **THEN** 显示新视频、跟踪关闭和失效保护告警，不显示输入停止，目标框保持清除

#### Scenario: 任务覆盖期间停止
- **WHEN** 持续预览已显示 ON MISSION，OFF 后继续收到电池数据但没有新的跟踪状态发布
- **THEN** 任务文字按原规则保留，独立提示区域说明跟踪关闭，电池更新，不把任务文字当作跟踪仍在运行

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

### Requirement: RSSI 使用生产桥接的百分比契约

RSSI 在此表示飞控提供的遥控接收链路质量，不是视频链路强度或 dBm 功率。系统 SHALL 使用 `/mavros/rc/in` 的 `mavros_msgs/msg/RCIn.rssi`。最新 `apm_bridge` 的 `rk3576` 源码将此字段定义为 0–100 百分数。系统 SHALL 原样采用此范围内的整数，包括 0。系统 MUST NOT 按 254 重新缩放或用电池比例代替。大于 100 的输入 SHALL 立即成为未知值。

#### Scenario: 正常百分比和边界
- **WHEN** 输入 `rssi` 依次为 0、85 和 100
- **THEN** 显示侧依次取得有效百分比 0、85 和 100，不将 85 转换为约 33

#### Scenario: 非法值清除旧读数
- **WHEN** 已收到 85，随后分别收到 101、254 或 255
- **THEN** 对每个非法输入，下一快照中的 RSSI 都为未知，不截断为 100，也不保留 85

### Requirement: RSSI 有效期与电池及图像更新独立

系统 SHALL 为 RSSI 单独记录有效接收时刻。有效期 SHALL 使用单调时钟和既有 `osd.telemetry_timeout_ms`，默认 2000 毫秒。尚未接收或距最后有效输入超过该期限时，RSSI SHALL 为未知。电池、温度或视频更新 MUST NOT 刷新 RSSI 有效期。既有 10 Hz 遥测快照 SHALL 在实时视频、无框预览及冻结底图上继续传递 RSSI。

#### Scenario: 刚好到期与超过到期
- **WHEN** 有效 RSSI 85 的接收时间为 0，且超时为 2000 毫秒
- **THEN** 2000 毫秒时仍有效，2001 毫秒时未知

#### Scenario: 电池更新不能保持旧 RSSI
- **WHEN** RSSI 停止更新超过超时，电池与温度仍持续到达
- **THEN** RSSI 为未知，电池与温度的新鲜数据仍可用

#### Scenario: 停止后接收和恢复
- **WHEN** 跟踪停止或底图冻结后，隔离重映射话题继续发布 RSSI，并经历有效、过期及恢复
- **THEN** 显示侧快照仍反映这些变化，不恢复旧框或改变停止状态
