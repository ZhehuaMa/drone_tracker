# Spec Delta

## MODIFIED Requirements

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
