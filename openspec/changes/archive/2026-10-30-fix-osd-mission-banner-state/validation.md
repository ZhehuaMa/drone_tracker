# 任务状态横幅修复验证记录

## 验证对象与结论

本记录验证 `fix-osd-mission-banner-state`。修复后的 `ON MISSION` 同时要求收到任务开始事件，以及新鲜、已连接、已解锁且 `guided=true` 的飞行状态。`guided` 在本系统中表示飞控已进入 OFFBOARD 控制。任务结束、取消或飞行授权丢失会移除横幅；目标跟踪状态不再决定横幅寿命。

2026-10-28 在 `rk3576` 分支完成软件验证和生产板 HDMI 验证。基线提交为 `e4f6f92`。该提交已经同步并归档 RSSI 变更，本次实现保留其信号强度显示行为。当前变更尚未同步或归档，也没有发布为生产版本。任务遥测增量规格已补回 RSSI 变更刚同步的 `/mavros/rc/in` 输入和重映射场景，避免以后同步时丢失既有契约。

软件检查、ARM64 构建与模拟执行、真实设备输出分别列于下文。模拟显示后端的通过结果不作为硬件输出证据。

## 软件构建与测试

| 检查 | 环境 | 结果 |
|---|---|---|
| 独立 OSD 测试 | x86_64 开发主机；`cmake -S src/osd -B /tmp/mission-osd-native`，显式使用 `/usr/bin/python3` | 11/11 通过 |
| 完整仓库入口 | `./build.sh --test --artifacts-dir /tmp/tracker-mission-banner-validation` | 63/63 CTest 通过，耗时 167.50 秒 |
| 原生操作系统内存限制检查 | 同一默认容器中的 x86_64 测试程序 | 1/1 通过 |
| 开启可选低层反馈的 OSD 测试 | x86_64 容器；独立生成同架构 `quadrotor_msgs`，源码只读，输出在 `/tmp/mission-ros-native` | 13/13 通过 |
| 接收时间与锁边界补充检查 | 最后增加从飞行事件回调读取遥测快照的断言，随后单独重建 ROS 遥测测试 | ARM64/QEMU 1/1 通过；开启低层反馈的原生版本 1/1 通过 |
| 独立验证后的 TargetLost 组合回归 | 在两种呈现路径中切换为状态 `3`，确认已获授权的任务横幅继续显示；同时比较两种真实绘图路径的红色目标装饰 | 原生呈现与绘图测试 2/2 通过；ARM64/QEMU 对应测试 2/2 通过 |
| Git 空白检查 | 当前仓库 `git diff --check` | 通过 |
| OpenSpec 严格验证 | `openspec validate fix-osd-mission-banner-state --strict` | 通过 |

完整入口使用 `drone-tracker-rk3576:humble-amd64-rknn2.3.2`，镜像指纹为 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。编译器为 GCC 11.4.0，CMake 为 3.22.1。x86_64 编译器通过 ARM64 交叉工具链生成目标程序。QEMU 8.2.2 用于运行选定的 ARM64 测试，不模拟相机、NPU 或 HDMI 硬件。

最终节点为 ARM aarch64 ELF，SHA-256 为 `5179ca225955c03efaa1494fdf0f9cd1e1d0b9ff3ce9e99efee126b37825a186`。完整构建默认关闭可选低层反馈；独立原生测试补充验证开启该选项时的真实 ROS 消息接收。

首次独立主机测试自动选择的 Python 缺少 PyYAML，因此 YAML 检查失败。显式选择 ROS 兼容的 `/usr/bin/python3` 后，11 项全部通过。开启低层反馈的独立验证还修正了临时构建命令中的消息安装前缀；这些是测试环境配置修正，没有更改产品依赖或跳过测试。

核心覆盖包括：两种输入顺序、等待期间的 ACRO、确认后的 OFFBOARD 丢失、解锁与连接丢失、飞行状态超时、从未收到飞行状态、重复消息、先结束后取消、取消早于延迟开始、结束边界后的重新授权，以及持续 60 秒的任务静默。ROS 测试验证同一 100 ms 汇总间隔内收到的 `true/false/true` 和短暂 OFFBOARD 退出不会被合并丢失。

显示测试覆盖两种绘制路径、没有新帧时的取消和过期、NanoTrack 阻塞时的真实节点回调、取消前已经准备的叠加层、已授权硬件工作的安全完成、有效视频包继续被接受，以及取消后继续预览。绘制测试比较真实 CPU 绘图结果，确认任务结束或解除锁定后没有残留文字，目标装饰、RSSI、解锁文字和提示位置保持原有规则。

本环境没有配置或提供 `clang-tidy`、`clangd` 检查命令，因此没有宣称执行这些静态检查。

## 生产板、相机和模型

板卡为 `HZ-RK3576_WuKong Board`，ADB 序列号 `a4649c048860c947`，主机名 `hzhy`。系统为 Ubuntu 22.04.5 ARM64，内核 `6.1.141-rt36`。这是真实生产板结果，不是开发板 1、开发板 2 或容器结果。

使用板端既有 `/opt/drone_tracker/config/camera.yaml`：SC233HGS，相机节点 `/dev/video22`，1920×1080 NV12，连续采集 60 fps；HDMI 为 720×480、约 60 Hz。采集前读取了 `v4l2-ctl --list-devices` 和 `media-ctl -p`，核对设备和拓扑。测试使用实时室内相机场景。既有模型在该场景产生了真实的状态序列 `0 → 1 → 2` 和绿色锁定框；没有注入假目标结果。

| 现有模型 | SHA-256 |
|---|---|
| YOLO：`balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| NanoTrack 模板 Backbone：`fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| NanoTrack 搜索 Backbone：`fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| NanoTrack Head：`fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

节点继续使用板端 RKNN Runtime 2.3.2 系统库。验证前后 `/usr/lib/librknnrt.so` 的 SHA-256 均为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。没有覆盖该库，也没有重新训练或转换模型。

## HDMI 输出证据与测量方法

真实节点临时复制为 `/tmp/mission-banner-node`，通过命令行分别选择 `cached-overlay` 和 `legacy`。测试只覆盖运行参数，关闭录像和诊断收集；生产配置和 `/opt/drone_tracker/current` 均未更改。合成任务和飞行状态通过 `ROS_DOMAIN_ID=183`、`ROS_LOCALHOST_ONLY=1` 发布，只作用于隔离测试节点，不向生产控制节点发送命令。

屏幕证据来自内核当前用于 HDMI 的真实扫描缓冲区。扫描缓冲区是显示控制器正在读取的图像存储。测试读取 DRM 的当前平面状态，并用板端 `kmsgrab` 读取活动的主平面 58 和叠加平面 103。PNG 按这两个真实图像平面的透明度关系合成；它们不是模拟后端或另行运行布局代码生成的截图。两个平面逐个读取，因此不能把 PNG 解释为同一次物理扫描的精确时间截面。

计时使用板端单调时钟。测试先记录消息发布时刻，再匹配 `OSD mission` 的叠加版本与 `OSD display` 成功提交记录。成功记录发生在硬件页面切换完成之后。下表采用从发布到成功显示的时间；发布早于接收，因此它是接收到成功显示时间的保守上界。过期一项从最后一次飞行状态发布开始计时。

| 终止原因 | cached-overlay | legacy | 上限 |
|---|---:|---:|---:|
| 任务 `false` | 22.341 ms | 34.216 ms | 500 ms |
| 解除锁定 | 18.788 ms | 18.528 ms | 500 ms |
| `DISMISS` | 24.681 ms | 30.937 ms | 500 ms |
| 已确认的 OFFBOARD 退出 | 16.781 ms | 37.975 ms | 500 ms |
| `OFF` | 16.819 ms | 49.186 ms | 500 ms |
| 飞行状态超时 | 2025.756 ms | 2045.015 ms | 2500 ms |

这些时间是本次健康设备运行的测量结果，不是所有负载条件下的最坏时间保证。没有用回调日志冒充已经显示的画面。

两种路径都验证了以下情形：

1. 仅有 OFFBOARD 飞行状态时，任务横幅保持隐藏。
2. 开始事件与 ACRO 形成等待状态；在 OFFBOARD 确认之前收到任务结束，不显示未确认横幅。
3. 确认后显示横幅。任务 `false` 到达时，真实目标仍为 `Tracking=2`，绿色锁定框保留，横幅恢复为 `TRK TRACKING`。
4. 解除锁定、`DISMISS`、`OFF`、收到 OFFBOARD 退出、飞行状态超时均移除横幅。`DISMISS` 后仍有实际跟踪图像和锁定框；`OFF` 后新预览图像继续，目标装饰清除。
5. 收到 OFFBOARD 退出后，即使飞行模式恢复，也不复活旧任务。先收到 `false` 再收到 `true` 后，新的任务尝试可以重新获得确认。
6. 停止测试节点，在没有订阅者时先发布任务 `true`，再重启节点，只持续发布有效 OFFBOARD 飞行状态。重启后的扫描缓冲区没有任务横幅，日志中 `attempt=0`、`mission=0`，同时预览序号持续增加。

这里没有外置摄像机拍摄监视器，也没有 HDMI 线缆分析仪。证据覆盖真实板卡的活动 HDMI 扫描图像及完成的硬件提交，不声称测量显示器面板的光学响应时间。没有进行飞行验收、相机标定、长时间热负载验收或 Jetson 验证。

## 可审阅证据

完整输入、输出转换脚本和日志保存在忽略目录 `.docker-artifacts/mission-banner-20261028/`。本变更额外保留以下可提交证据，便于不依赖临时目录进行审阅：

- [cached-overlay 确认后画面](evidence/cached-active.png) 与 [结束后画面](evidence/cached-ended.png)。两张图均保留真实绿色跟踪框。
- [legacy 确认后画面](evidence/legacy-active.png) 与 [结束后画面](evidence/legacy-ended.png)。
- [cached-overlay 错过开始后的重启画面](evidence/cached-restart-hidden.png) 与 [legacy 对应画面](evidence/legacy-restart-hidden.png)。
- `evidence/*-banner.png`：等待、进入失败、解除锁定、取消、退出和超时后的实际状态栏区域。
- [逐事件时间与对应显示日志](evidence/board-timing.json)，以及两个路径的 `*-events.json`、`*-transitions.log` 和重启记录。
- [证据文件指纹](evidence/SHA256SUMS)。

## 服务状态与交付边界

用户授权临时停止板端服务，并要求所有工作结束后恢复。停止前确认 `drone-tracker.service`、`drone-tracker-diagnostics.service`、`drone-interceptor.service`、`apm-bridge.service` 均为 active/running/enabled；`lightdm.service` 原本 inactive。初始状态、停止结果和恢复脚本保存在 `.docker-artifacts/mission-banner-20261028/`。

在板端测试向后续任务交接时，临时节点和发布器均已退出，DRM 客户端列表为空。交接时四个应用服务保持临时停止状态，以便独立验证继续使用板卡；这描述交接时刻，不代表全部工作结束后的最终状态。所有用户指定工作完成后，由父任务执行 `restore-services.sh` 恢复原来的四个活动服务，并将恢复核对结果追加到忽略目录中的服务记录。没有修改服务启用状态或单元文件。

本次差异仅涉及 `drone_tracker` 中的任务显示策略、ROS 遥测传递、节点连接、显示交接、测试、说明文档和本变更证据。没有修改相邻仓库、控制算法、ROS 消息定义、生产配置、模型文件或父仓库子模块指针。任务显示变更仍保持独立活动状态，等待后续验证流程。
