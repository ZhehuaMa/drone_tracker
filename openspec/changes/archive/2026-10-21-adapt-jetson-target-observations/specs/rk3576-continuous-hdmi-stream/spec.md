## MODIFIED Requirements

### Requirement: 新轮任务和输出与旧轮隔离

系统 SHALL 分别标识视频流会话和跟踪轮次。新跟踪轮次 SHALL 从新的分发边界接收图像，MUST NOT 使用关闭期间的旧任务。OFF 后的旧轮帧结果 MUST NOT 发起新的 ROS 发布或 HDMI 目标显示。系统 SHALL 允许按 target-observations 为该轮发送一次显式无效 bbox/LOS 控制通知；该通知不是旧推理结果，也不产生目标装饰。重复 OFF SHALL 去重。通知 SHALL 在 bag 封存前尝试提交；控制路径 MUST NOT 等待推理或磁盘收尾。旧设备访问结束前 MUST NOT 重置或释放其资源。已经交给 ROS 中间件的消息 SHALL 保留既有延迟交付语义。

#### Scenario: 旧请求完成于新轮之后
- **WHEN** OFF 使轮次 1 失效，系统等待其访问结束后启动轮次 2
- **THEN** 轮次 2 不消费轮次 1 的请求或结果，算法模型跨轮复用

#### Scenario: 从预览进入新轮
- **WHEN** 预览已显示流内序号 100，随后新轮在分发边界就绪
- **THEN** 新轮从边界之后取得新任务，不用旧启动结果使 HDMI 回到序号 99

#### Scenario: 失效通知与迟到推理竞争
- **WHEN** OFF 关闭一轮的正常发布接收并发出无效 bbox/LOS，旧推理随后返回
- **THEN** 没有后续旧轮有效观测或目标装饰；无效通知不会停止 HDMI 视频，也不会触发新的 ROS 图像

### Requirement: 记录与 ROS 调试图像保持业务边界

原画面录像 SHALL 仅由 `recording.enabled` 和 ON/OFF 会话控制。ROS bag SHALL 保持 `need_save` 的现有轮次规则，并按 rk3576-ros-video 记录姿态、里程计、浮点框、LOS 和 MAVROS IMU。所有 visualized_result 设置下，bag MUST NOT 包含 `/fpv/tracker_image` 消息或话题元数据。服务自动预览 MUST NOT 创建上述记录。`/fpv/tracker_image` SHALL 继续只在 `visualized_result=true` 且当前轮次实际消费图像时发布。预览 MUST NOT 产生该话题消息或其 bag 数据。

#### Scenario: 开启调试图像但未触发
- **WHEN** 持续预览配置 `visualized_result=true`、`need_save=true`、`recording.enabled=true`，服务启动后尚未收到 ON
- **THEN** HDMI 有视频，但没有跟踪图像消息、bag 会话或原画面录像会话

#### Scenario: 结束一轮后继续预览
- **WHEN** ON 后记录和调试图像正常，随后接收 OFF
- **THEN** 当前记录收尾且停止新的跟踪图像发布，HDMI 预览继续

#### Scenario: 调试图像不等于消息录像
- **WHEN** ON 有效轮次同时设置 visualized_result=true、need_save=true、recording.enabled=true
- **THEN** 可实时发布调试图像并独立录制原画面，bag 记录规定的非图像话题；OFF 后三者按各自会话边界收尾，HDMI 继续预览
