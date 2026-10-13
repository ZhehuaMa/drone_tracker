# W6 帧时间有效位修复记录

日期：2026-10-13。变更：`add-rk3576-tracking-diagnostic-process`。

**W6 的三处遗漏已修复，定向回归和最终 ARM64 容器 47/47 项测试通过。生产板两项受控测试通过。任务最终保持 39/40，7.3 未勾选。** 本报告是实施修复记录，不是新的独立校验。2026-10-12 的 `evidence/verification-report.md` 完整保留，其中 W6 和 C1 的历史结论未改写。

## 修改与契约

`FrameTimeKnown` 是事件的“帧时间已取得”有效位，数值为 `4U`。消费者应据它判断 `frame_time_us` 是否可解释。`frame_time_us` 保存帧源时间，单位为微秒；它与预测时间、执行耗时分开记录。

| 位置 | 修复 |
| --- | --- |
| `src/drone_tracker_node.cpp:822` | 视频结束后的 EOF 补取路径中，`FrameSkip` 已从真实帧取得时间，现在同时设置有效位。EOF 表示输入结束；补取用于处理仍在队列中的末帧。 |
| `src/drone_tracker_node.cpp:827` | 同一路径的 `FrameSelect` 同时设置有效位。 |
| `src/hybrid_tracker/hybrid_tracker.cpp:284` | 每条 `CatchupStep` 已从历史帧取得时间，现在同时设置有效位。补跟踪是用历史帧把跟踪位置推进到当前输入。单步成功和失败都保留已取得的时间。 |

已检索并检查全部九处诊断事件的 `frame_time_us` 赋值。正常取最新帧、调用入口/返回、发布记录和相机采集边界原有有效位保持一致。相机无帧和跟踪空输入仍不伪造已知时间。没有按时间值是否非零推造有效性。

实际业务修改只有三处位运算。模型处理、算法分支、补帧继续规则、ROS 话题/消息/时间、理由编号和协议布局未修改。`event.h`、`protocol.h` 指纹与修复前一致，仍为版本 1、256 字节事件。没有增加记录、循环、图像引用、内存分配或文件读写，既有热路径固定预算保持不变。

## 定向回归

- `src/tests/ros_video_test.cpp` 扩展受控帧源。等待超时并报告 EOF 后，非阻塞队列仍返回三帧。真实节点必须依次记录两条 `FrameSkip` 和一条 `FrameSelect`，每条时间逐项匹配帧源实际生成的值，且均有 `FrameTimeKnown`。同时核对只发布末帧。`basic` 和 `detail` 两种模式均运行。测试入口增加 `--diagnostic-boundaries`，仅运行这些节点边界检查，便于生产板短时验证。
- `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp` 使用真实公共跟踪器和受控模型返回值。既有部分补帧失败场景逐条检查 `CatchupStep` 的历史帧身份、对应时间和有效位，并确认两步失败及其余成功步骤都接受检查。既有汇总和整体恢复结果断言保持。
- 新增空帧输入回归。真实 `update()` 不消费输入；调用开始与返回的时间数值保持零，有效位均未设置。它说明缺少帧时仍报告未知。

上述测试直接经过生产实现并读取实际提交事件，没有复制事件生成逻辑作为待测实现。没有真实模型阈值调整。

## 构建、产物与证据对应

使用既有镜像 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 执行 `./build.sh --test`。第一次 47/47 通过，用时 37.42 秒。增加上述定向测试入口后执行最终构建，**47/47 通过，用时 36.77 秒**。最终结果包含完整 `ros_video_tests`、`diagnostic_tracker_tests`、协议/存储和三模式兼容检查。

原始日志、源文件快照和独立保留的二进制位于仓库 `.docker-artifacts/diagnostic-w6-fix-20261013/`。最终日志为 `raw/build-final.log`、`raw/test-console-final.log`。完整指纹见本目录 [artifact-manifest.json](artifact-manifest.json)。SHA-256 是核对文件内容的指纹。

| 最终产物 | SHA-256 | 本轮范围 |
| --- | --- | --- |
| `drone_tracker_node` | `85131a03ff49e2711623e7edca8dda57298ea05aa5afe883df388de6cb16339b` | 容器构建与自动检查，未正式部署。 |
| `rk3576_diagnostic_worker` | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` | 与上一批位级一致。 |
| `rk3576_diagnostic_inspect` | `bc00627919b9e60ea3f95b9ab17a9aad11103f6348c8bffd22850543179a8970` | 容器与板端测试相同。 |
| `diagnostic_tracker_tests` | `7938bd24790de0cd5a75c48aead6d4a054bece2acd8fc7106a5fe8a70e012444` | 容器与板端测试相同。 |
| `ros_video_tests` | `5f718cccfe5aec0c6fef3cca7f44cb9a84838001f64ea5e1dcd2b030d6af7168` | 容器与板端测试相同，静态链接本轮节点与跟踪实现。 |

生产板通过 `firefly-production` 访问，为 HZ-RK3576_WuKong Board，Ubuntu 22.04.5 ARM64、内核 `6.1.141-rt36`。每条 SSH 命令先加载 ROS 2 Humble。系统 RKNN Runtime 2.3.2 的库指纹仍为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，驱动读回 `0.9.8`。

板端测试安装树为 `/home/hzhy/drone-diagnostic-w6-fix-20261013/bin/`，使用独立 `ROS_DOMAIN_ID=199` 和 `ROS_LOCALHOST_ONLY=1`。两项测试均由已有 `bounded_run.py` 提供外部截止，每项总预算 30 秒，包含 3 秒收尾。它通过 Linux 子收割者跟踪并回收实际后代进程。

| 板端受控测试 | 实际总秒数 | 结果与原件 |
| --- | ---: | --- |
| `diagnostic_tracker_tests` | 0.449367 | 通过；[tracker-window.json](tracker-window.json) 无超时、退出码 0、无残留、`resources_recovered=true`。 |
| `ros_video_tests --diagnostic-boundaries` | 6.345034 | 通过；[ros-window.json](ros-window.json) 同样确认有限退出与实际回收。 |

两项测试使用合成帧和受控模型，未采集相机，未执行 NPU 推理，未占用 HDMI。节点测试未运行独立日志服务，连接退化和有限收尾警告符合该测试安排；断言读取业务共享区实际提交的事件。板端原始控制台及动态库检查保存在 `raw/production/`，程序指纹与本地保留二进制逐项匹配。

09:38:25 与 09:40:07（生产板 +08:00）即时核查均为正式 PID `27312`、`active/running`，current 均指向 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。Runtime 指纹一致。结束核查未发现本轮测试残留。原件为 `raw/production-before.txt`、`raw/production-after.txt`。本轮未停正式服务、未切换 current、未修改系统 Runtime。

## 任务与未运行边界

本轮重新打开 3.3、3.5、4.4、4.5、7.2、7.7，按实际修复、回归和报告完成重新勾选，每项均重新查询 CLI。最终 **39/40**，7.3 保持未完成。`openspec validate --strict` 与 `git diff --check` 通过。

旧完整十组仍只有 7/10 满足全部门槛，C1 保持未解决。本轮未重复性能十组，未把旧二进制的性能结果解释为新节点的测量结果。旧十组中 9522 条时间有效位遗漏属于历史原件，未修补或覆盖原件。

本轮没有执行真实模型视频、相机/录像/bag/HDMI 全组合、细分性能、存储故障实机组合、正式升级或回退。ARM64 容器结果与板端受控测试分别报告，不能替代这些验收。既有 OSD 基线偶发失败本轮未定位，两次全通过不证明该问题消失。环境未提供配置可用的 clang-tidy/clangd，静态分析未运行。Jetson SDK 与硬件不可用，参考平台构建/实机回归未运行。长期稳定、实飞准确率、真实断电、强制重启和介质损坏未执行。

历史独立校验报告 SHA-256 仍为 `0dd5eba8892e3467a7effc90f4088058d5d9d7d385e4659cb1814ee53db7f953`。本报告提供 W6 已修复的证据，不擅自把历史独立校验改为通过。既有工作区修改及另外两个 change 保留；没有提交、同步主规格、归档或正式升级。
