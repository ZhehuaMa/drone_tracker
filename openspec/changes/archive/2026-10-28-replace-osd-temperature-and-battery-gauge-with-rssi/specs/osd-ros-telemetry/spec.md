# Spec Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
