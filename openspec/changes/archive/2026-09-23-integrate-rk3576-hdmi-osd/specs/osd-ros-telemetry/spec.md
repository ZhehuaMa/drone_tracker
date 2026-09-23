# OSD 外部遥测规格

## Purpose

将飞行系统通过 ROS 2 提供的姿态、电池、位置和环境信息转为可读屏幕元素。为未收到、非法及过期数据建立明确显示规则，使冻结视频期间仍能查看最新遥测，同时不混淆外部雷达状态与本地跟踪状态。

## ADDED Requirements

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

系统 SHALL 复用现有 `/fpv/tracker_trigger` 的 ON/OFF 接收事件显示触发提示，不增加独立控制通路。低层反馈适配未编译时 SHALL 隐藏依赖该输入的显示，其他遥测仍可工作。

#### Scenario: 没有自定义消息包
- **WHEN** 构建未启用低层反馈适配，标准电池与姿态话题正常到达
- **THEN** 正常显示电池与姿态，低层反馈告警等级显示未知或隐藏，不阻止 OSD 启动

#### Scenario: 雷达正在跟踪而本地检测中
- **WHEN** 外部雷达状态为 2，本地提交包状态为 Detecting
- **THEN** 显示两者各自状态，不将本地框或跟踪状态改为 Tracking

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

电池剩余电量 SHALL 按 `BatteryState.charge` 的 Ah 含义处理。只有有效容量和合法剩余电量同时存在时，系统 SHALL 显示两者差值换算的已耗 mAh。单节电压 SHALL 仅在已配置节数或有效单节数据存在时显示。系统 MUST NOT 用整包电压猜测节数后当作确定值。

#### Scenario: 电池剩余量换算
- **WHEN** `capacity=13 Ah`、`charge=10 Ah` 且字段有效
- **THEN** 已耗电量显示为 3000 mAh，不将 10000 mAh 标为已耗电量

#### Scenario: 无有效容量
- **WHEN** 电压有效但 charge 或 capacity 未知
- **THEN** 电压正常显示，已耗 mAh 隐藏或标为未知

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

系统 SHALL 按连接丢失、失效保护、电池严重、低电量的优先级显示当前有效告警。来源未知或过期时 SHALL 标明未知，不继续断言旧告警是当前状态。冻结/等待提示 SHALL 有独立显示区域，不被遥测告警覆盖。遥测快照 SHALL 以 10 Hz 汇总；OFF 后只要显示后端健康就 SHALL 继续更新。

#### Scenario: 冻结时收到失效保护
- **WHEN** 视频因 OFF 冻结，而新鲜消息报告 failsafe=true 且连接正常
- **THEN** 屏幕同时可见冻结提示和失效保护告警，不恢复旧目标框
