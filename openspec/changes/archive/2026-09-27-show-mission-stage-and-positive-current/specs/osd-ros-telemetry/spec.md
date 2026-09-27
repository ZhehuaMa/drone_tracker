# Spec Delta

## MODIFIED Requirements

### Requirement: 外部遥测经已有消息类型输入

系统 SHALL 使用下表话题和消息类型接收外部数据，并支持 ROS 话题重映射。遥测输入 SHALL 不改变本地帧、框和跟踪状态的进程内数据通路。

| 话题 | 消息类型 | 用途 |
|---|---|---|
| `/mavros/imu/data` | `sensor_msgs/msg/Imu` | 航向、俯仰、横滚 |
| `/mavros/state` | `mavros_msgs/msg/State` | 连接、模式、解锁与计时 |
| `/mavros/extended_state` | `mavros_msgs/msg/ExtendedState` | 落地状态 |
| `/mavros/battery` | `sensor_msgs/msg/BatteryState` | 电压、电流、电量 |
| `/mavros/gpsstatus/gpsraw` | `mavros_msgs/msg/GPSRAW` | 卫星、地速、航迹 |
| `/mavros/global_position/global` | `sensor_msgs/msg/NavSatFix` | 定位与返航计算 |
| `/fpv/static_pressure` | `sensor_msgs/msg/FluidPressure` | 气压高度 |
| `/fpv/temperature_baro` | `sensor_msgs/msg/Temperature` | 温度 |
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
