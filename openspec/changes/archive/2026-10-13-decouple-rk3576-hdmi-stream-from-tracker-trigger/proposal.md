# Proposal

## Why

生产相机服务启动后，操作者需要一直看到带 OSD（叠加在视频上的状态和遥测信息）的 HDMI 画面。当前服务必须收到 `ON` 才出图，收到 `OFF` 后又冻结画面，导致停止检测跟踪也失去实时观察能力。

例如，修改前“启动服务 → 等待 ON → 跟踪并显示 → OFF 后冻结”；修改后“启动服务 → 自动显示预览 → ON 后显示配对的跟踪结果并录制 → OFF 后停止跟踪和录像，但实时预览继续”。

## What Changes

- **BREAKING**：仅在 RK3576 正式 ROS 相机入口且 `osd.enabled=true` 时，采集与 HDMI 输出随节点服务生命周期运行。该模式称为“持续预览模式”。`OFF` 不再停止相机或冻结健康视频。
- 将视频流会话、跟踪轮次和录像会话分开。服务退出关闭视频流；`ON/OFF` 继续控制检测跟踪、显式启用的原画面录像及既有 ROS bag 消息记录。
- 由常驻帧分发线程独占读取原相机队列。跟踪关闭、启动及收尾阶段显示无框预览；正常跟踪时显示同次处理的图像、显示框和算法状态。
- 保留目标框与图像的同帧关系。正常跟踪期间的新画面速率仍受跟踪处理速度约束；本变更不引入“旧框覆盖最新图像”或推理阻塞时的自动视频回退策略。
- `OFF` 立即使旧跟踪结果失效并清除目标装饰。等待在途推理和录像收尾不得阻塞预览取帧。重复消息和停止期间的快速启停不得累积线程或会话。
- 将 SC233HGS 原画面录像控制从帧源启停中拆出。保持 `recording.enabled`、原尺寸无业务叠加、分段格式、持久化和资源预算的既有规则。服务启动本身不开始录像。
- 保留 `/fpv/tracker_image` 行为：只有图像开关启用且当前跟踪轮次实际消费图像时，才按原规则发布；持续 HDMI 预览不产生该话题消息。检测框与状态话题的类型、QoS（队列与可靠性设置）、时间语义和数值不变。
- 保留 `MISSION` 任务文字及限时 `TRIG ON/OFF` 提示。跟踪关闭状态须能独立辨认，不能把任务文字当作仍在跟踪。
- 未启用 OSD 的相机入口、Jetson、有限视频文件回放和独立相机工具保持既有行为。`legacy` 与 `cached-overlay` 两条显示路径遵守相同的新生命周期。
- 统一持续预览模式的定向验收要求。已有实现的历史验收记录保留，本次每次性能或负载运行不超过 180 秒，不要求重复无关的历史长测。

## Capabilities

### New Capabilities

- `rk3576-continuous-hdmi-stream`：定义持续预览的适用条件、视频与跟踪生命周期、唯一帧分发者、旧轮隔离、安全退出及定向验收。

### Modified Capabilities

- `osd-presentation`：允许无跟踪结果的预览包；区分视频流与跟踪轮次；持续预览模式下 OFF 清框但不冻结，并保留其他模式的冻结契约。
- `rk3576-hdmi-osd`：两条渲染路径、画面间隔统计、冻结提示和切换验收适配独立生命周期。
- `rk3576-ros-camera`：持续预览相机自动开流；ON/OFF 只控制跟踪及记录会话；更新预热、故障、采集统计和 ROS 输出兼容场景。
- `rk3576-camera-recording`：保持 ON/OFF 录像控制，允许同一视频流中安全切换录像会话，并分别记录流身份和会话统计。
- `rk3576-sc233hgs-camera-frame-source`：保持独立帧源行为，明确持续预览期间 OFF 不结束相机预热或采集，并支持安全切换录像接收对象。
- `osd-ros-telemetry`：OFF 后遥测继续更新，持续预览模式不附加输入停止提示；保留 MISSION 覆盖规则。
- `rk3576-thread-affinity`：增加常驻帧分发入口的职责映射和确认，区分服务创建与 ON 创建的资源。
- `production-binary-deployment`：区分启用开机服务与实际启动节点，说明启动后立即采集、OFF 后设备仍占用，以及完整版本回退方式。

## Impact

- 编排层：`src/drone_tracker_node.h/.cpp`、`src/drone_tracker_main.cpp`。现有 `run()/stop()`、`worker()/processFrames()` 及统计需要分开服务与轮次职责。
- 显示层：`src/osd/presentation.h`、`src/osd/presenter.h/.cpp`、`src/osd/layout.cpp`。优先复用 `src/platform/rk3576/osd/hdmi_backend.cpp` 的像素转换和原子显示实现。
- 采集与录像边界：`src/platform/platform_factory.h` 或独立平台控制接口、RK3576 相机帧源、`src/platform/rk3576/recording/`。公共接口不得暴露 RKNN、RGA 或相机 SDK 类型。
- 测试：ROS 相机集成、OSD 提交与布局、SC233HGS 采集/录像、线程亲和性及故障注入。视频文件测试须保留原 OFF 冻结行为，不能批量替换为相机预览断言。
- 配置和部署说明：生产相机模板、`deployment/README.md` 与 `src/platform/README.md`。保留现有模型、算法、记录开关、显示模式、CPU 策略和系统 Runtime；无需增加启动参数。
- 不新增硬件 SDK、模型转换或训练依赖。目标兼容基线仍为系统 RKNN Runtime 2.3.2 和 RKNPU 驱动 0.9.8。
- 本提案只创建规划工件。主规格同步、代码修改、构建、部署和实机运行均属于后续实施，不在本次执行。
