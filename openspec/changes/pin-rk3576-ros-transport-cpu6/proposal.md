# Proposal

## Why

生产板接收任务开始信号后，OSD（叠加在视频上的状态文字）曾继续显示 `TRK TRACKING` 数秒，甚至在任务结束后才显示 `ON MISSION`。跟踪进程的 ROS 消息传输线程与较高优先级的跟踪工作共同限制在 CPU 4；临时把传输相关线程移到 CPU 6 后，三次台架尝试中的任务状态消息交付降到毫秒级，需要将这一分配固化到启动流程。

## What Changes

- 为 CPU 策略增加 `ros_transport` 职责。该职责负责创建 ROS 通信资源，使 DDS（ROS 使用的消息分发中间件）的接收、发现与事件线程在创建时继承指定核心。生产配置显式使用 CPU 6。
- 保持控制线程和 ROS 回调执行器在 CPU 4。保持跟踪 4、YOLO 检测 5、显示 6、采集与录像 7 的既有职责分配。
- 在 ROS 初始化、节点基类构造及应用发布者、订阅者创建前应用传输职责。拆分通信资源创建与模型初始化，随后明确恢复控制职责，不依赖一次性初始化标志恢复核心。
- 保持策略 `version: 1`。旧策略未提供 `ros_transport` 时沿用 `control`，无策略和 `inherit` 模式保持继承行为。新增字段无效时报告错误。
- 增加创建阶段、恢复阶段、错误清理和真实中间件线程继承的定向验证。禁止依赖固定线程编号、线程名称或事后扫描进行生产绑定。
- 例如：过去主线程先绑定 CPU 4，再创建通信线程；变更后先在 CPU 6 创建通信资源，再回到 CPU 4 初始化模型和处理回调，通信线程继续使用 CPU 6。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `rk3576-thread-affinity`：扩展 CPU 策略、区分通信资源与业务工作创建阶段，并规定传输职责的兼容、恢复和定向验收行为。

## Impact

- 修改 `src/platform/cpu_affinity.*`、`src/drone_tracker_main.cpp`、`src/drone_tracker_node.*` 的职责解析和启动组织。
- 修改 `deployment/cpu-affinity.yaml`、`deployment/cpu-policy.py`、对应测试及部署说明。保持系统 Fast DDS、RKNN Runtime 2.3.2 和 RKNPU 0.9.8，不升级依赖。
- 通过现有 ARM64 构建入口和生产板的隔离通信检查验证。测试可以临时停止板端服务；完成后恢复原先运行的服务。
- ROS 话题、消息类型、队列语义、算法、跟踪状态值和录像协议保持不变。飞行模式仍显示飞控确认的状态；`ON MISSION` 的确认条件保持不变。
- 不修改拦截控制进程。临时试验仍观测到该进程在桥接器接受任务后约 0.80–1.30 秒才记录任务启动，本变更不承诺消除这段上游延迟。
