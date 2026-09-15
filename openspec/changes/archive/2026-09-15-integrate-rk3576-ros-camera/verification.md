# 实现核验报告：integrate-rk3576-ros-camera

核验日期：2026-09-15。范围为 `rk3576` 分支当前工作区，包括尚未提交的实现与工件。本报告只记录核验结果，不修改实现、任务勾选或归档状态。

## 结论

**未发现 CRITICAL（阻断级）问题；发现 1 项 WARNING（应处理的偏差）。** 现有任务及实机证据齐全，可以带此警告进入归档评估；建议先解决下述调度边界，再取得无警告的核验结论。

| 维度 | 核验结果 |
|---|---|
| 完整性 | 38/38 项任务已勾选；四份增量规格共 13 项需求均找到实现或验收证据 |
| 正确性 | 13 项需求均已核对；其中软件触发调度存在 1 个边界偏差；共检查 42 个规格场景 |
| 一致性 | 平台边界、线程清理、帧所有权、时间来源和 ROS 接入符合设计；调度迟到容差与设计未对齐 |
| 本次执行 | OpenSpec 严格校验通过；ARM64 构建通过；ROS/公共测试 12/12、独立相机/公共测试 14/14 通过；`git diff --check` 通过 |

## WARNING：迟到不足一周期时，仍可能短间隔追赶

位置：`src/platform/rk3576/camera_frame_source.cpp:13–18`、`:103–113`，以及 `src/platform/rk3576/tests/camera_frame_source_test.cpp:92–95`。

`skipLate()` 只有在迟到达到一个完整周期时才跳过命令。以 20 Hz 为例，正常周期为 50 ms。若原定 50 ms 的命令直到 99 ms 才得到执行机会，函数返回零，保留原定命令。触发循环会在 99 ms 发出该命令，并继续使用 100 ms 的下一计划时刻。驱动调用足够快时，两次命令可能只相隔约 1 ms。

这与 [设计决策 3](design.md#3-周期触发使用单调时钟落后不补发)及[软件触发节奏需求](specs/rk3576-camera-frame-source/spec.md)中的“落后跳过、不突发追赶”存在偏差。当前测试覆盖迟到到 120 ms 的整周期跳过，也允许 1 微秒的正常唤醒抖动；没有覆盖接近一周期的迟到。实机记录没有出现调度跳过，因此不能排除这一边界。

本次从当前头文件和源文件原样提取 `CameraTriggerPlan` 声明及两个方法，在本机编译纯时序探针，输出：

```text
now_us=99000 skipped=0 due_us=50000 next_due_us=100000 interval_us=1000
```

该探针验证计划计算，不代表执行了相机或目标板测试。原文件位于 `/tmp/camera_schedule_verify.cpp`，编译命令为 `clang++ -std=c++17 /tmp/camera_schedule_verify.cpp -o /tmp/camera_schedule_verify`。

**建议：** 明确定义可接受的唤醒迟到容差。超过容差时跳过过期计划并计数，保持原始周期基准。补充 49 ms、恰好 50 ms 迟到以及普通微小抖动的可控时钟测试，断言实际命令时刻及跳过计数。若实际意图是允许不足一周期的追赶，应在设计和规格中明确这一行为及短间隔边界。

## CRITICAL 与 SUGGESTION

- CRITICAL：无未勾选任务，未发现完全缺失的需求实现。
- SUGGESTION：无单独列出的改进项。

## 需求与场景映射

以下位置均相对于仓库根目录。场景数按四份增量规格的 `Scenario` 标题统计。找到测试或源码分支表示具有覆盖证据，不意味着每个场景都已在真实硬件执行。

| 规格／需求 | 场景数 | 实现与证据 |
|---|---:|---|
| platform-factory／公共帧源保持现有采集与录像行为 | 5 | `src/platform/platform_factory.h:32`、`src/drone_tracker_node.cpp:98`；`ros_video_test.cpp` 的 `recording()`、`realVideo()`、`cameraContracts()`；Jetson 相机录像与重启仅作适配源码及公共契约核对，未执行 Jetson 实机 |
| platform-factory／独立工厂与生产构建边界 | 5 | `src/platform/rk3576/platform_factory.cpp:22`、`production_factory.cpp:38`；`camera_frame_source_test.cpp` 的 `timeoutAndFactory()`；生产构建、安装树及动态依赖检查 |
| camera-frame-source／软件触发节奏可预测 | 2 | `camera_frame_source.cpp:10`、`:92`；`softwareLifecycle()` 覆盖落后周期和命令失败；不足一周期迟到见警告 |
| camera-frame-source／触发诊断不伪造帧关联 | 2 | `camera_device.cpp:470`、`camera_frame_source.cpp:111`；`camera_driver_support.cpp:144`、`camera_driver_test.cpp:66`；120 命令/60 帧及驱动时间来源记录 |
| camera-frame-source／软件触发新增实机证据独立验收 | 1 | `final-board-validation.md`；原始频率矩阵、20 轮启停、100 次销毁、601 秒慢消费及连续模式回归 |
| camera-frame-source／相机配置与拒绝条件 | 4 | `camera_device.cpp:190`、`camera_frame_source.cpp:285`；`camera_device_test.cpp:256`；原尺寸、录像拒绝、频率独立及模式冲突测试 |
| camera-frame-source／停止重启和失败清理 | 7 | `camera_device.cpp:280`、`:316`、`:528`；`camera_frame_source.cpp:212`、`:263`；设备失败矩阵、控制延迟确认、恢复重试及触发在途清理测试 |
| ros-camera／入口和固定配置 | 5 | `drone_tracker_node.cpp:30`、`:157`；相机 launch/YAML；`cameraContracts()`；模拟内参来源有明确标注 |
| ros-camera／ON/OFF 完整轮次 | 2 | `drone_tracker_node.cpp:58`、`:332`；公共 `HybridTracker::finishRound()`；启动失败重试、在途模型等待、旧结果隔离及实机 20 轮 |
| ros-camera／图像与预测消息语义 | 2 | `drone_tracker_node.cpp:293`、`:432`；`messagesAndRestart()`、`cameraContracts()`；实机各话题原始时间序列和新轮状态记录 |
| ros-camera／每轮故障与采集量诊断 | 1 | `drone_tracker_node.cpp:364`；命令、读取、入队、丢弃、消费分别计数；慢消费 CSV 与终态日志 |
| ros-camera／实机验收与模型质量边界 | 2 | `final-board-validation.md`；八组基础配置、四组故障恢复、同进程 20 轮及额外 1806 秒负载；真实无目标结果保留 |
| ros-video／本地实时 MJPEG 正式入口 | 4 | `drone_tracker_node.cpp:135`、`:159`；视频后端；`configuration()`、`realVideo()`、`ros_initialization_tests`；相机请求转至相机后端 |
| 合计 | 42 | 13 项需求均有对应证据 |

## 本次构建与证据复核

### 构建

使用 `AGENTS.local.md` 指定的 `roc-rk3576-ros2-build:humble-arm64` 镜像与 `docker run --rm` 临时容器。按原约定挂载仓库、build、install、log，并执行已有 `.docker-artifacts/ros-camera-build.sh`。缺少的 ROS 依赖只补装在本次容器中，未重建镜像。

本次日志为 `.docker-artifacts/log/ros-camera-verify-build.log`。两组测试全部通过，安装树存在相机 launch 与参数文件。脚本的 `readelf`、`ldd` 检查通过，未发现缺失库或 CUDA/TensorRT/VPI/Tegra 动态依赖。纯时序探针与上述 ARM64 构建结果分开记录。

重建后的节点与工具指纹仍能匹配既有开发板 2 `deployment.log`：

| 程序 | SHA-256 |
|---|---|
| drone_tracker_node | `131d4ce8d561788c1d31b580bbbfc2631bb67b42642e84ccb9c653dce66d29fa` |
| camera_source_driver | `828f07d7e6d7499b84e9b24d033f5e6081d66d015857ab39cd2dc353bda43884` |

### 既有开发板 2 原始记录

本次读取 `.docker-artifacts/ros-camera-final/`，没有重新运行硬件采集或模型负载。证据对应真实 1920×1200 GREY 相机、2048 字节行跨度、Runtime 2.3.2、驱动 0.9.8。同一 YOLO 搭配纯 FP16 或纯 INT8 NanoTrack，分别检查软件与连续模式。

- 直接读取各组 `rounds.csv`：无触发、单次、10/15/20/30/60 Hz 各三轮；20 Hz 每轮 120/120，30/60 Hz 每轮分别为 120/60、120/30。
- 软件启停 20 轮、新建销毁 100 次；连续模式 20 轮共 6000 帧；慢消费 601 秒，命令 12019、读取 12018、队列丢弃 6109。上述各组控制恢复成功，停止均小于 1 秒。
- `basic-merged-results.json` 包含八组基础配置，每组两轮并主动退出；四个模型文件各加载一次。`failures-results.json` 包含四组受控错误及恢复，不能把错误注入称为自然驱动故障。
- `load-results.json` 包含同进程 20 轮及额外 1806.044 秒运行。持续负载命令和读取均为 36124，消费 36123。`load-resources.jsonl` 有 360 个采样，运行线程与句柄固定为 23/43，退出后进程不存在。
- `continuous-isolated-results.json` 包含每轮新建测试订阅的 30 轮复核。原接收积压异常仍保留；不能把订阅端晚到的历史消息直接判为新轮算法读取旧帧。
- `final-postflight.log` 记录控制恢复为 0/1/60，测试设备链接和错误注入标记已移除。模型加载及退出结果与最终报告一致。

### 未执行与结论边界

Jetson 实机、曝光同步、镜头正式标定、真实模型识别质量和 NPU 设备内存均未由本次核验补齐。既有镜像不提供 clang-tidy/clangd，因此未运行这两项静态检查。模拟内参仍为 1000/1000/960/600，不能证明旋转补偿精度。前序回放任务 7.2、7.3 保持原结论。

以上是已声明的验证范围，不将其误记为本变更新增的未完成任务。归档时应保留这些限制，以及本报告的调度警告。
