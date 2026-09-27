# Proposal

## Why

实机进入拦截任务后，HUD 仍显示 `TRK TRACKING`，操作人员无法从状态栏区分跟踪与执行任务。电池放电时电流显示为负数，也与参考 HUD 的正值读法不一致。当前节点只处理跟踪触发消息中的 `ON` 和 `OFF`，并直接显示电池消息中的有符号电流。

## What Changes

- 收到现有 `/fpv/tracker_trigger` 消息的 `frame_id="MISSION"` 时，HUD 状态栏显示 `ON MISSION`。该文字持续显示，直到节点产生下一条 `/fpv/tracker_state` 状态消息；之后恢复显示最新跟踪状态。
- 保留 `TrackerState` 的 `0` 至 `3` 数值及含义。`MISSION` 只改变 HUD 状态栏，不改目标框颜色、锁定样式或 ROS 状态发布。
- HUD 电流字段显示有效 `/mavros/battery` 电流的绝对值；未知和非有限值继续显示未知标记。
- 为事件顺序、重复触发、两条渲染路径和负电流增加定向验证，更新 HUD 说明。
- 为实机诊断记录触发接收、任务覆盖解除，以及 HDMI 已提交画面的状态栏文字、跟踪框标签和停止冻结状态；仅在事件或显示值变化时输出日志。

例如：跟踪状态为 `Tracking=2` 时显示 `TRK TRACKING`；收到 `frame_id="MISSION"` 后改为 `ON MISSION`；随后跟踪器发布 `TargetLost=3` 时改为 `TARGET LOST`。同一画面若电池消息电流为 `-3.0 A`，HUD 显示 `CUR 03.0A`。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `osd-presentation`：增加任务阶段覆盖文字及其由下一条本地跟踪状态事件结束的规则。
- `osd-ros-telemetry`：增加 `MISSION` 触发消息的显示用途和电流绝对值显示规则。

## Impact

- 影响节点触发回调、显示侧事件状态、HUD 文本生成、电池显示格式、相关测试和 `docs/RK3576_HDMI_OSD.md`。
- 沿用现有 ROS 话题和消息类型；不改变跟踪算法、状态数值或电池原始消息。
