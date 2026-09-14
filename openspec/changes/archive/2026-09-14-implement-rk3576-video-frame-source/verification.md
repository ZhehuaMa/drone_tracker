# implement-rk3576-video-frame-source 核验报告

核验日期：2026-09-14。对象为 `rk3576` 工作区中的实现，按 `spec-driven` 流程对照 proposal、design、tasks 和两份增量规格。本次只新增报告，没有修改实现、勾选任务、同步主规格或归档。

## 结论与完整性

在本变更约定的范围内，未发现需要修复的实现偏差，可以进入归档流程。规格同步时必须按 design 第 8 节协调旧变更的冲突条款。

| 维度 | 结果 |
| --- | --- |
| 完整性 | 26/26 任务完成；9/9 需求找到实现依据 |
| 正确性 | 25 个场景完成代码与证据对照；其中 3 个沿用 Jetson/ROS 的场景仅作源码保持性检查 |
| 一致性 | 符合设计中的软件解码、时间分离、单队列、从头重启及平台边界决策 |
| 本次构建与回归 | 约定 ARM64 临时容器构建成功；CTest 27/27 通过，耗时 9.92 秒 |
| OpenSpec 严格校验 | `openspec validate implement-rk3576-video-frame-source --strict` 通过 |

## 需求与场景对照

下表源码路径相对于仓库根目录。PTS 指视频帧在原时间轴上的显示时刻；EOF 指读取器报告输入结束。

| 需求 | 实现依据 | 场景与验证依据 |
| --- | --- | --- |
| 本地 MJPEG 输出原尺寸灰度 | `src/platform/rk3576/mjpeg_video_reader.cpp:42`、`:151` | 首帧保留、641×479 边缘、灰度和三种彩色采样的亮度逐像素对照，见 `mjpeg_video_reader_test.cpp`。非法文件和非 MJPEG 编码被拒绝；实际测试素材为伪装 PNG 编码，H.264 由同一编码拒绝条件覆盖，未另运行 H.264 素材。设备和管道由普通文件检查拒绝。 |
| 帧时间保留显示时间 | `src/platform/rk3576/video_timing.cpp:7`、`:37`、`:49` | `video_timing_test.cpp` 覆盖 Artist 大小写、歧义、非法基准、缺失/负值/重复 PTS、换算及加法溢出。真实 offset.mov 和帧源测试验证非零首帧偏移。 |
| 供帧按视频时间且等待可停止 | `src/platform/rk3576/video_timing.cpp:62`；帧源工作循环 | 注入时钟测试验证系统校时不改播放节奏，解码落后不额外等待。容器测试验证 10 秒等待可打断；真实慢消费和板上停止耗时见 validation.md。 |
| 单生产者、单消费者队列 | `src/platform/rk3576/video_frame_source.cpp:26`、`:244`；`src/platform/frame_source_ops.h:25` | `queueAndOwnership()` 验证只丢新帧、旧帧顺序、满队列统计和失败清空输出。公共消费者排空积压取最新帧。实机暂停消费试验取得 31 个旧帧，59 个新帧被丢弃。 |
| 每次重启从首帧开始 | `src/platform/rk3576/video_frame_source.cpp:176`、`:221` | 帧源测试覆盖中途停止、EOF、读取失败、线程启动失败和尺寸变化后的重试，以及实时/历史基准和清除旧队列。实机记录两种模式各 20 轮重启。 |
| 结束及资源释放安全可诊断 | `src/platform/rk3576/mjpeg_video_reader.cpp:103`；帧源工作循环、析构和 stop | 坏 JPEG、尺寸变化、读取错误、时间错误均有相应处理。测试覆盖 EOF 后析构、尾帧、诊断回调异常及帧/视图跨销毁存活。stop 先等待线程退出再释放读取器，不与消费者竞争排空。 |
| 独立回放及验证边界 | `src/platform/rk3576/tests/video_source_driver.cpp:11`；原 `src/replay/` 实现 | ReplayReader、原回放命令和配置未改动；原回放测试及退出码测试通过。新驱动经真实工厂和公共取帧接口运行，未加载模型。 |
| 公共帧源保持采集和录像行为 | `src/platform/rk3576/video_frame_source.cpp:252`；`src/platform/jetson/platform_factory.cpp:41`；`src/drone_tracker_node.cpp:196` | RK3576 视频录像配置为空操作；工厂测试验证提交录像配置后正常播放。RK3576 重启由上述测试覆盖。沿用的“相机启动录像”“视频文件启动”“采集停止后再次启动”三个场景仅核对未修改的 Jetson/ROS 路径，未声明实机通过。 |
| 独立工厂视频支持及生产边界 | `src/platform/rk3576/platform_factory.cpp:21`；`src/platform/rk3576/CMakeLists.txt:53`；`CMakeLists.txt:7` | 工厂测试覆盖未启动创建、实际尺寸、相机配置忽略、相机拒绝和路径诊断。平台选择测试继续拒绝根目录 RK3576 构建。视频软件依赖没有反向加入公共算法目标；既有动态链接检查未发现 Jetson 或 RKNN Runtime 依赖。 |

## 实机证据核对

本次复核已有实机证据，没有重新执行十分钟板上负载。使用 `shasum -a 256 -c .docker-artifacts/video-board-20260914/source-sha256.txt`，清单中的 16 个文件全部与当前工作区一致。清单覆盖生产视频实现、工厂、主机帧、主要测试、验收驱动及 CMake；它不是全仓库清单。

源码清单、原始板上日志和资源样本 JSON 的 SHA-256（文件内容指纹）均与 validation.md 记录一致。原始日志末尾包含 `BOARD_VALIDATION_PASS`，记录 318 轮负载、600.187 秒。资源汇总记录 100 次创建销毁，每轮停止后线程数为 1、文件句柄数为 5；最终驻留内存 31208 kB。停止最大耗时 33.1238 ms，专门的长等待停止为 1.00869 ms。该证据支持任务 5.1–5.4 的完成状态。

本次测试输入由仓库生成器生成，包含灰度、4:2:0、4:2:2、4:4:4、非整块尺寸、时间偏移、慢消费及错误素材。视频测试没有加载 YOLO 或 NanoTrack 模型。

## 问题优先级

- **CRITICAL（归档前必须修复）：0。** 没有未完成任务或缺失的需求实现。
- **WARNING（应修复）：0。** 未发现本次范围内未解释的规格或设计偏差。
- **SUGGESTION（可选改善）：0。** 没有需要单独报告的代码模式问题。

旧变更冲突是已记录的同步步骤，不是新增实现缺陷：先保留 `assemble-rk3576-tracker-replay` 的非冲突模型装配与顺序回放条款，再应用本变更的视频支持。最终条款必须为相机仍拒绝、视频按新规格提供、根目录仍拒绝 RK3576。旧变更后续归档不得恢复“拒绝视频”，其 7.2、7.3 质量验收继续保持未完成。

## 未执行检查与证据边界

本次未运行 Jetson 构建、Jetson/ROS 启停、真实相机、模型质量或跨平台算法等价性验证。当前本地约定没有 Jetson 环境，且这些硬件验收不属于本次视频能力范围。已阅读 Jetson 分支视频实现作语义对照，不能据此宣称实机等价。

本次未重跑地址/泄漏/未定义行为检测及严格编译警告检查；validation.md 和本地日志保留了先前 4/4 内存检测与 11 个编译单元警告检查通过的记录。未执行专用线程竞态检测器；已运行的并发测试不等于穷尽所有线程交错。

本次重跑使用 `AGENTS.local.md` 约定的 `roc-rk3576-ros2-build:humble-arm64` 镜像及 `docker run --rm`。容器内采用 validation.md 的 Release 配置、`cmake --build build/video-source -j4` 和 `ctest --test-dir build/video-source --output-on-failure`。没有重建镜像。最新逐项测试日志保存在 `.docker-artifacts/build/video-source/Testing/Temporary/LastTest.log`。
