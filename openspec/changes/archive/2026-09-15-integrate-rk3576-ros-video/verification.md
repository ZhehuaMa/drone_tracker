# integrate-rk3576-ros-video 核验报告

核验日期：2026-09-15。分支：`rk3576`。工作流：`spec-driven`。

## 结论

未发现阻止归档的实现缺项。34/34 项任务已勾选，13 项需求均找到实现或验收证据。初次核验发现的 1 项文档一致性警告已修复，当前无未解决警告。此次核验及后续修复仅涉及报告和运行文档，没有修改实现、任务勾选或同步、归档任何变更。

| 维度 | 结果 |
|---|---|
| 完整性 | 34/34 项任务；13/13 项需求有对应证据 |
| 正确性 | 36 个规格场景已逐项检查代码、测试或验收记录；Jetson 完整构建场景仅有配置检查和源码证据 |
| 一致性 | 8 项设计决策与实现总体一致；文档警告已修复 |

“有证据”不等于此次重新执行全部测试。以下分别标明本次执行与历史结果复核，不能据此宣称所有硬件场景重新通过。

## 问题清单

### CRITICAL：归档前必须修复

无。没有未勾选任务，也未发现缺失的需求实现。

### WARNING：建议修复

无未解决警告。2026-09-15 已修复“运行文档中的实机进度落后于最终证据”：`src/platform/README.md:320` 和 `RK3576_REFACTORING_PLAN.md:21` 已明确 ROS 视频接入完成规定的 RK3576 实机验收，并链接 `validation.md`。两处均保留模型质量、部署性能、NPU 设备内存和 Jetson 实机验证的限制，与任务 7.1 及最终验收证据一致。

### SUGGESTION：可选改进

无额外建议。

## 需求与证据映射

下表路径相对于仓库根目录。状态快照是只读取帧源状态的接口；轮次收尾是等待旧推理结束并清除旧请求和结果的操作。

| 需求 | 实现与检查证据 |
|---|---|
| 构建明确选择一个已实现平台 | `CMakeLists.txt:4` 默认 Jetson、提前拒绝未知值、按平台查找依赖；`src/platform/rk3576/dependencies.cmake:1` 明确目标版本；平台选择脚本本次通过 |
| RK3576 工厂提供视频并保持平台边界 | `src/platform/rk3576/platform_factory.cpp:22` 创建视频并拒绝相机；`backend.cmake` 由生产和独立入口共用；独立测试及已保存链接检查提供证据 |
| 公共帧源可查询终止原因 | `src/platform/platform_factory.h` 提供默认 Unknown；`src/platform/rk3576/video_frame_source.cpp:255` 映射受锁保护的诊断；视频测试覆盖并发查询、超时、尾帧与失败保留 |
| 复用模型时隔离不同运行轮次 | `src/hybrid_tracker/yolo_detector.cpp:96` 撤销排队请求、等待执行结束并清空结果；`preprocess_contract_test.cpp:179` 覆盖待执行、执行中、普通失败及重复帧号 |
| 正式入口使用本地实时 MJPEG 输入 | 节点 `setup()` 校验两项时钟；工厂与 MJPEG reader 校验输入；ROS 初始化测试、独立 reader 测试及基础实机结果覆盖 |
| 模型角色和路径必须明确 | 节点 `setup()` 校验共享/拆分角色并解析绝对及相对路径，装配层检查模型契约；`ros_video_test.cpp:182` 与 `jetson_config_test.cpp` 覆盖配置错误与旧路径策略 |
| 视频内参与启动配置显式提供 | 节点 `setup()` 校验有限值及正焦距，RK3576 跳过相机缩放；构造函数固定启动参数；专用 launch 仅覆盖视频路径；配置测试覆盖 |
| 时间与取最新帧保持一致 | 节点 `processFrames()` 使用公共取最新帧函数及帧时间，Running 超时继续等待；`ros_video_test.cpp:305` 覆盖真实十秒间隔期间 OFF；独立慢消费测试覆盖跳帧 |
| ROS 接口和结果有效性保持兼容 | `processFrames()` 保留预测/图像两种时间、有效性和状态变化发布；绘图生成 BGR 副本；`ros_video_test.cpp:208` 覆盖捕获、跟踪、丢失恢复和重启状态；另与 `jetson` 分支话题和 QoS 源码对照 |
| 多轮启停复用模型并完整收尾 | 节点 `run()`、`stop()`、`worker()` 串行控制并回收旧线程；帧源每次 start 重新打开路径；`video_frame_source_test.cpp:209` 覆盖首次 ON 前文件移走及恢复，实机失败/重试结果一致 |
| 结束原因与尾帧可诊断 | 节点 `processFrames()` 区分 Failed/Running/Eof，确认 EOF 后再次取尾帧；`ros_video_test.cpp:255` 覆盖尾帧竞争；实机损坏输入保留失败诊断 |
| ROS bag 记录与关闭互斥 | `openRecording()`、`closeRecording()`、`record()` 使用同一互斥锁；`ros_video_test.cpp:329` 覆盖姿态/里程计发送、关闭后读取、目录碰撞和打开/写入失败；实机 bag 与不可写目录结果一致 |
| 接入验收与模型质量分别报告 | `validation.md` 及 `fix1/basic-results.json`、`failure-results.json`、`fix2/reuse-results.json` 和 `resource-summary.json` 保存分层证据；旧回放任务 7.2、7.3 仍未勾选 |

## 本次实际执行

- `openspec status --change integrate-rk3576-ros-video --json`：规划工件齐全。
- `openspec instructions apply --change integrate-rk3576-ros-video --json`：34/34，无未完成任务。
- `openspec validate integrate-rk3576-ros-video --strict`：通过。
- `git diff --check`：通过。
- 在 macOS 执行 `src/hybrid_tracker/tests/platform_selection_test.cmake`，指定当前仓库和独立临时目录：通过。检查默认平台、未知平台拒绝和 RKNN 缺失诊断；不代表完整 Jetson 或 RK3576 构建。

已读取 proposal、design、tasks、两份 delta specs 和 validation，检查相关实现与测试，并对照 Jetson 参考分支的 ROS 话题、QoS（消息队列与可靠性设置）和消息时间代码。

## 历史证据复核

原始证据目录为仓库下 `.docker-artifacts/ros-board-20260915/`，这些是本地产物，不随源码提交。

- `fix1/retest.log`：ARM64 ROS/公共测试 12/12，独立入口 27/27 通过。包含公共替身模型、真实公共跟踪器和生成的 MJPEG 素材；不是 NPU 推理测试。
- `fix1/basic-results.json`：修复版真实 FP16 和 INT8 配置均完成基础集成、图像开关、记录及 EOF/重启。两组使用相同 YOLO 和各自三份 NanoTrack 文件。
- `fix1/failure-results.json`：首次启动文件缺失及恢复、损坏输入及恢复、运行中 SIGINT、记录目录失败及恢复、模型缺失退出均有结果。
- `fix2/resource-summary.json` 与逐轮记录：PID 5773 完成前置 20 轮，再额外完成 181 轮；有效播放统计为 1801.933221 秒，额外阶段墙钟时间 1918.450480 秒，观测到 53700 条对应处理帧的检测消息。四次模型身份日志、一次 YOLO 线程启动与跨轮复用相符。最终退出码为 0，记录中进程已不存在。
- 固定输入为 960×968、30 FPS、十秒 MJPEG；目标 Runtime 2.3.2、驱动 0.9.8。修复版节点、模型和视频的 SHA-256 文件指纹见 `validation.md`。

长负载统计已对各轮图像首尾时间差按视频跨度封顶。15 轮异常差值明确保留，未直接作为播放时长计入。进程驻留内存出现一次约 21 MiB 台阶增加，随后约 80 轮没有持续上升；线程保持 20、文件描述符保持 48。该证据支持本次时长内未观察到持续资源增长，不证明长期无泄漏或 NPU 设备内存稳定。

## 未执行与结论边界

本次没有重新构建 ARM64 产物或重新运行 ROS、NPU、实机长负载测试，而是检查对应源码、测试和已有原始结果。没有重复部署开发板。

完整 Jetson 编译和实机运行、NPU 设备内存测量、clang-tidy/clangd 静态检查仍缺少验证。真实检测质量和前序完整回放 7.2、7.3 不在本次通过结论中。旧回放 delta 的过时平台限制仍须在后续同步时协调，不能覆盖回已开放的视频能力。

最终评估：0 项 CRITICAL，0 项未解决 WARNING。可进入归档准备。文档修复后重新执行 OpenSpec 严格校验和 `git diff --check`；本报告不执行同步或归档。
