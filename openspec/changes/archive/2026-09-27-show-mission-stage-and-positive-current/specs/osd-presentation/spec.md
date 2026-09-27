# Spec Delta

## ADDED Requirements

### Requirement: 任务触发覆盖 HUD 跟踪状态文字

HUD 状态栏 SHALL 在收到 `/fpv/tracker_trigger` 的 `frame_id="MISSION"` 后显示 `ON MISSION`。此处的任务触发表示开始执行拦截任务，不是 `TrackerState` 的新数值。系统 SHALL 持续显示 `ON MISSION`，直到本节点下一次向 `/fpv/tracker_state` 发布新的跟踪状态值；随后 SHALL 显示该值对应的跟踪状态文字。状态值 `3` SHALL 继续表示 `TargetLost`，MUST NOT 解释为任务阶段。

任务触发 SHALL 只覆盖状态栏文字，不得修改与视频帧配对的跟踪状态、目标框颜色和锁定样式。收到 `ON`、`OFF` 或其他非 `MISSION` 触发值 SHALL 不启动任务文字覆盖。两条 HDMI 渲染路径 SHALL 显示相同的任务文字。

#### Scenario: 跟踪期间进入任务
- **WHEN** 状态栏显示 `TRK TRACKING`，随后收到 `frame_id="MISSION"`，且没有新的 `/fpv/tracker_state` 发布
- **THEN** 状态栏持续显示 `ON MISSION`，即使两秒触发提示结束或后续视频帧到达

#### Scenario: 新跟踪状态结束任务覆盖
- **WHEN** 状态栏显示 `ON MISSION`，随后本节点向 `/fpv/tracker_state` 发布 `3`
- **THEN** 状态栏显示 `TARGET LOST`，目标丢失仍按状态 `3` 的既有红色规则绘制

#### Scenario: 非任务触发不覆盖状态栏
- **WHEN** 已显示 `TRK TRACKING`，随后收到 `frame_id="ON"`、`frame_id="OFF"` 或其他字符串
- **THEN** 状态栏不因这些触发值显示 `ON MISSION`

### Requirement: HDMI 状态切换诊断日志

系统 SHALL 在接收触发消息、发布变化后的跟踪状态、置位或解除任务覆盖，以及请求结束一轮显示时记录事件日志。系统 SHALL 在 HDMI 成功提交的画面中，状态栏文字、跟踪框标签或显示状态发生变化时记录实际提交结果。日志 SHALL 包含轮次及足以关联画面的帧序号；停止后的日志 SHALL 指明画面是否保留最后视频底图、目标框是否清除。稳定视频帧 SHALL 不逐帧输出相同的诊断日志。

#### Scenario: 跟踪中收到任务触发
- **WHEN** 跟踪中收到 `frame_id="MISSION"`
- **THEN** 日志能区分触发到达、任务覆盖置位、状态发布解除覆盖和最终提交的右上角文字

#### Scenario: 跟踪中收到停止触发
- **WHEN** 跟踪中收到 `frame_id="OFF"`
- **THEN** 日志能关联停止请求、显示状态变为 `Stopped`，以及最后视频底图冻结和目标框标签清除后的提交结果
