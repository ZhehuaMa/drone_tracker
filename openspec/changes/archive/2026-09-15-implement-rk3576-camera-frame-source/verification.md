# 实现核验报告：implement-rk3576-camera-frame-source

核验日期：2026-09-15。工作流：`spec-driven`，即按提案、设计、规格和任务清单核对实现。

## 结论

未发现 CRITICAL（归档前必须修复的严重问题）。发现 1 项 WARNING（应处理的验证警告）：长时采集的计时包含启动开销，现有记录不能严格证明有效采集持续至少 600 秒。按核验技能的分级规则，可带此警告归档；建议先补足时长证据。

本次只增加核验报告，没有修改实现、任务勾选或既有实机证据，也没有执行归档。

| 维度 | 结果 |
|---|---|
| 完整性 | 29/29 项任务已勾选；8/8 项要求找到实现 |
| 正确性 | 20/20 个命名场景找到代码及测试或历史记录支持；长时验收时长有 1 项警告 |
| 一致性 | 设备层、帧源层、独立帧所有权、固定时钟偏移和工厂能力隔离符合设计 |

命名场景覆盖不等于所有硬件故障均已实测。设备不存在、格式错误及失败清理由系统调用替身覆盖；替身会模拟设备响应，但执行实际设备层代码。

## 本次执行的检查

- `openspec status` 与 `openspec instructions apply`：确认全部规划工件存在，任务为 29/29。
- `openspec validate implement-rk3576-camera-frame-source --strict`：通过。
- 按 `AGENTS.local.md` 使用既有 `roc-rk3576-ros2-build:humble-arm64` 临时容器，带 `--rm`，重新配置并构建完整独立 RK3576 入口：通过。
- `ctest --test-dir build/camera-regression --output-on-failure`：33/33 通过，总测试时间 10.10 秒。覆盖相机设备、时间、帧源、报告工具、生产工厂默认拒绝相机，以及已有视频、模型替身和公共算法回归。
- `git diff --check`：通过。
- 使用 conda base Python 只读复核本地开发板 2 原始 CSV：四组文件均存在，合计 43132 帧、122 轮。核对帧数与报告一致、时间为正且每轮递增、尺寸与行跨度正确、读取数等于入队数加队列丢帧数。全部停止不超过 1 秒；停止及销毁资源样本恢复 1 个线程、9 个文件描述符。
- 本地实机工具 SHA-256 与既有实机记录一致：`8f7f477e1790b98102cd1e8a44b7d21346f499a89b7bca12208a3f42a6c1c499`。

容器中使用合成灰度输入、生成的视频及模拟模型接口，没有真实相机或 NPU 推理。构建目录为 `.docker-artifacts/build/camera-regression/`；CTest 原始日志为其下 `Testing/Temporary/LastTest.log`。

## 要求与场景对应

下列路径相对于仓库根目录，测试文件位于 `src/platform/rk3576/tests/`。

| 要求 | 实现证据 | 场景验证 |
|---|---|---|
| 独立工厂提供视频并隔离生产相机 | `src/platform/rk3576/platform_factory.cpp`、`production_factory.cpp` | `camera_frame_source_test.cpp::timeoutAndFactory`、`tracker_assembly_test.cpp::frames` 和视频回归覆盖 5 个场景 |
| 显式配置与拒绝不支持行为 | `src/platform/rk3576/camera_device.cpp::validateCameraConfig`、`camera_frame_source.cpp::configureRecording` | 配置测试拒绝缩放；帧源测试拒绝录像，覆盖 2 个场景 |
| 实际设备格式符合请求 | `camera_device.cpp::configureSensor`、`prepare`、`checkFormat` | 初始化失败测试覆盖打开失败、协商不符；创建探测不申请缓冲或开流，覆盖 2 个场景 |
| 像素及帧所有权独立 | `camera_device.cpp::next`、`src/platform/rk3576/host_frame.cpp` | `camera_device_test.cpp::pixelsAndMetadata` 覆盖填充、保留视图后销毁、非法缓冲，合计 3 个场景 |
| 时间可诊断、不伪造曝光时刻 | `src/platform/rk3576/camera_timing.cpp`、`camera_device.cpp::next` | 时间测试覆盖驱动单调时间、未知时间回退及重复时间，合计 3 个场景；设备测试另覆盖系统校时 |
| 单消费者及慢消费行为 | `camera_frame_source.cpp::run`、`waitFrame`、`tryFrame` | `camera_frame_source_test.cpp::queueAndRestart` 覆盖消费者暂停；检查丢新帧、保留旧队列内容与驱动缺帧独立计数 |
| 停止、重启及失败清理 | `camera_device.cpp::wake`、`finish`，`camera_frame_source.cpp::start`、`stop` | 等待停止、失败后重启、部分映射失败 3 个场景均覆盖；另有真实 Linux eventfd 唤醒测试 |
| 独立验收工具与证据边界 | `tests/camera_driver_support.cpp`、相机构建入口及 `validation.md` | 工具测试覆盖报告、快照与禁止覆盖；相机未连接场景由首次实施记录保留，随后实机记录独立补齐 |

场景数量按两份 delta spec 的 `#### Scenario:` 标题统计。delta spec 表示本次变更新增或修改的规格。

## 问题清单

### CRITICAL：无

没有未勾选任务，也没有完全缺失的要求实现。

### WARNING：1 项

**W1：长时验收把启动开销算入采集时长。**

- 位置：`src/platform/rk3576/tests/camera_driver_support.cpp:166`，在 `source.start()` 前设置计时起点；第 172 行按该起点判断 `--seconds` 是否达到。第 199 行的 `elapsed_us` 还包括停止耗时。
- 规格依据：`specs/rk3576-camera-frame-source/spec.md:95` 要求至少 10 分钟负载；任务 6.6 明确要求至少 10 分钟采集。
- 证据：`board2-validation.md:39` 及原始 CSV 确认总轮次为 600.063018 秒，首末驱动帧跨度为 599.866182 秒。这个差距不说明资源不稳定，但总轮次时长不能直接作为有效采集时长证明。
- 建议：在启动完成或首帧到达后开始计算采集截止时间，单独保留总轮次耗时；补充启动延迟的替身测试。实机补测时留出余量，例如采集 610 秒，并用原始帧时间确认跨度至少 600 秒后更新任务 6.6 的证据。

### SUGGESTION：无额外建议

## 已有实机证据与本次未执行项

本次复核了 `.docker-artifacts/log/camera-board2-20260915/`，没有重新连接或运行开发板 2：

- 短采集 300 帧全部通过同帧像素对照。
- 20 轮启停、100 次新建销毁的原始记录齐全；122 轮最大停止耗时为 60.961 ms。
- 长时阶段 598 个运行样本的驻留内存均为 110944256 字节，线程和描述符数量稳定。驻留内存表示进程当时实际占用的主机内存；稳定样本不能替代专门的内存泄漏检测。

本次没有重跑根目录 ROS 构建与测试、相机专用构建入口或硬件采集；这些检查沿用 `validation.md` 与 `board2-validation.md` 的既有记录。未执行 Jetson 实机、真实模型质量、ROS 相机接入、曝光同步及部署性能验收。既有 SDK 镜像缺少 clang-tidy 和 clangd，本次未运行这两项静态分析。完整性、正确性与设计一致性三个维度均已核验，未跳过任一维度。
