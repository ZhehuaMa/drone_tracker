# HDMI OSD 实施与验证记录

> 2026-09-23 最新验收见 [剩余任务验收记录](final-validation.md)，包括产品独占检查、用户实际画面观察和最终节点验证。RKNN 同步修复证据见 [RKNN 同步修复记录](rknn-sync-validation.md)。下文完整保留此前失败与验收历史；其中进度和阻塞描述均属于历史状态，不代表当前结果。

本记录对应 `integrate-rk3576-hdmi-osd`。验收对象为生产 `HZ-RK3576_WuKong Board`，不是开发板 1 或 2。更新日期为 2026-09-23。任务进度为 **28/32**；剩余 4.6、6.2、6.3、6.4。尚未完成的任务保持未勾选，不同步或归档本变更。

后续隔离测试的具体证据与诊断结论见 [isolation-diagnosis.md](isolation-diagnosis.md)。下文初轮验收中的“根因未确认”保留为当时的记录，不覆盖后续发现。

## 已实施代码

- `src/osd/presentation.h`：完整包、拥有字符串与数值的遥测快照、可注入显示接口。
- `src/osd/presenter.*`：独立线程、单槽整包替换、轮次隔离、冻结、过期、失败收尾及统计。
- `src/osd/geometry.*`：按输入尺寸计算最终视频矩形，框使用同一映射并裁剪。
- `src/osd/layout.*` 与 `status_glyphs.h`：独立信息层和透明目标层、状态颜色、锁定标记、未知值与固定中文提示。
- `src/osd/ros_telemetry.*`：标准消息订阅、单调时钟有效期限、返航与气压参考、单位转换及告警。
- `src/osd/low_level_feedback.*`：单独的可选消息依赖边界，编译时校验电池等级常量。
- `src/platform/rk3576/osd/`：自有 DMA 缓冲、RGA 灰度/彩色处理、DRM 双层原子提交及原状态恢复。
- `src/drone_tracker_node.*`：仅在已消费结果路径提交 `display_box`；生命周期事件在原控制路径通知显示。未新增帧源消费者或本地结果 ROS 回订阅。

## 模拟与 ROS 自动化证据

本机使用约定的 `roc-rk3576-ros2-build:humble-arm64` 镜像和 `./build.sh --test`，没有修改或重建镜像。默认 OSD 关闭构建的完整测试已通过 **34/34**。日志位于本机 `.docker-artifacts/log/build-console.log` 和 `test-console.log`。

| 测试 | 覆盖要求 |
|---|---|
| `osd_presenter_tests` | A/B/C 整包替换、只读源帧、视图存活、硬件在途时不复用、提交前停止、在途停止、新轮隔离、可控单调时钟过期、EOF、输入/显示故障及重复退出 |
| `osd_geometry_tests` | 1920×1200 → `(0,14,720,450)`、中心 `(360,239)`、其他宽高比、贴边/越界/非法框 |
| `osd_layout_tests` | 四种跟踪状态、空框清除、冻结无框、告警与冻结区域隔离、新遥测更新、未知姿态、固定中文提示及 PNG 快照 |
| `osd_telemetry_tests` | 四元数归一化与 ENU 航向、非有限值、消息未知标记、GPS 单位、3000 mAh 示例、未知节数、返航点上升沿、飞行中气压参考、告警优先级和独立超时 |
| `ros_osd_telemetry_tests` | 实际 ROS 发布到模拟显示、话题重映射、QoS 匹配、OFF 后更新、过期组独立失效、雷达不覆盖本地跟踪状态 |
| `ros_video_tests` | OSD/ROS 图像开关组合、采集时间、状态仅变化发布、显示故障后业务继续、初始化回滚、原有重启/EOF/输入异常回归 |
| `ros_initialization_tests` | 未编译却请求启用的明确诊断；编译启用版本使用不存在的显示设备验证初始化失败 |

布局快照输出位于 `.docker-artifacts/build/drone_tracker/src/osd/osd-status-*.png`。已人工检查工具返回的故障提示快照，中文可辨识，冻结提示与告警没有重叠。此处是 CPU 画布检查，不是 HDMI 屏幕验收。

系统日历时间不参与有效期限计算。时间测试注入单调时钟；布局单独接收系统时间。没有把 ROS 消息自带时间戳当作接收新鲜度。

没有配置可运行的 clang-tidy/clangd 检查入口，约定容器也不提供这两个工具；该静态检查缺口保留。C++ 构建、CTest 和 `git diff --check` 已执行。Jetson 硬件/SDK 构建未执行；`jetson_config_tests` 仅测试配置策略。

## 生产板硬件前提和探针

详细能力检查见 [environment-check.md](environment-check.md)。本次 SSH 对象为 `firefly-production`，主机名 `hzhy`，内核 `6.1.141-rt36`。

- libdrm pkg-config 版本 2.4.124；librga pkg-config 版本 2.1.0，运行 API 报告 `1.10.5_[9]`。
- `HDMI-A-1`，720×480，实际 60 Hz；可启用原子接口并使用带透明通道的显示层。
- 系统 Runtime `/usr/lib/librknnrt.so` 指纹为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，驱动 0.9.8。没有覆盖系统 Runtime。
- 板端独立构建目录 `/tmp/drone-osd-20260923/build`。公共测试 **3/3** 通过。
- 首轮 `osd_hdmi_probe` 使用有行填充的 1920×1200 灰度渐变和彩色红蓝块及运动框，分别开启两次后端。两次均为 `submitted=60,replaced=0,displayed=60,refreshes=93`，退出码 0。初始化双层 TEST_ONLY 检查、实际原子提交、等待事件、退出恢复和再次打开均成功。
- 后续探针源码在视频像素内加入 `FRAME N`，目标框标签同步为 `N`，供实际输出配对观察。首次探针上述计数不冒充后续版本的视觉验收结果。

`displayed` 表示成功交付的新输入包；`refreshes` 表示成功画面提交次数，不是物理屏幕扫描次数。重复刷新同一帧不增加新帧计数。

**仍缺实际输出证据：**当前 Mac 的 AVFoundation 没有枚举出可读取的视频设备。因此尚未观察 HDMI 端的灰度中性、红蓝通道、运动框配对和冻结提示。提交成功不等于像素验收完成，任务 4.6 保持未完成。

## 完整板端构建环境处理

生产板原缺少 `vision_msgs`、`cv_bridge`。已新增 `ros-humble-vision-msgs` 4.1.1、`ros-humble-cv-bridge` 3.2.1，以及其 `libboost-python1.74.0`、`python3-opencv` 依赖；没有升级已有软件包。原下载代理 `127.0.0.1:18080` 未运行，本次 apt 命令临时使用 DIRECT；未修改代理配置。

找到生产板已有 `quadrotor_msgs` 1.0.0，安装前缀 `/home/hzhy/workspace/fpv_ws/install/quadrotor_msgs`。其 LowLevelFeedback 定义明确包含 BAT_INVALID=0、BAT_GOOD=1、BAT_LOW=2、BAT_CRITICAL=3。初次仅加载 `/opt/ros/humble` 时未找到该包；显式指定实际 CMake 路径后完成发现。不能将先前的环境未加载误报为包不存在。

完整节点源快照位于 `/tmp/drone-osd-node-20260923/src/drone_tracker`。构建头文件从本仓库既有 `.docker-artifacts/rknn/include/rknn_api.h` 复制到独立临时 SDK 目录，链接仍指定生产板系统库。该头文件不是本次安装或升级的系统 SDK。

源快照不含 Git 历史。尝试默认构建全部目标时，无关的 NanoTrack 基准程序在源码身份收集处报错；随后只构建本次节点及相关测试目标。默认 ARM64 容器中的完整回归仍覆盖既有基准测试，不将板端定向构建描述为全部目标通过。

## 已执行的板端构建与诊断

启用 HDMI 与 `quadrotor_msgs` 的定向构建已成功。初轮 CTest 的两项失败来自未加载安装前缀与可选消息动态库路径；安装节点并设置 `AMENT_PREFIX_PATH`、`LD_LIBRARY_PATH` 后，相关 **8/8** 测试通过。关闭可选消息适配重新构建后，ROS 遥测集成测试 **1/1** 通过。最终源码同步后发现部分旧目标文件被增量构建复用，已强制重新编译受影响源码。奇数灰度尺寸修复后的最终 OSD 开启、可选消息关闭版本相关测试 **8/8 通过**，耗时 14.94 秒，见 `rebuilt-final-test.log`。可选消息开启的最终源码复核 **1/1 通过**，耗时 0.78 秒，见 `final-test-lowlevel-on-confirmed.log`；实际发布 BAT_CRITICAL，并断言电池等级为 3、雷达状态不覆盖本地跟踪状态。该复核只重建测试目标，最终已安装节点仍保持可选消息关闭。

初次真实模型运行使用既有 INT8 NanoTrack 模型，当前后端明确拒绝其原生特征布局：要求 FP16 NC1HWC2，实际为 INT8 NC1HWC2。随后使用同批已批准 FP16 模型。没有更改模型契约、训练或转换模型。

合成测试输入为 FFmpeg `testsrc2` 生成的 640×480、20 帧/秒、660 秒 MJPEG AVI。示例内参为 `(800,810,320,240)`，只用于软件流程验证，不是相机标定。该画面没有捕获目标；有效框数量为零，不能据此评价跟踪质量或实际运动框配对。

预检查在同一进程完成 20 轮 ON/OFF，跟踪输出正常停止并能重新开始。但中途显示提交出现 `ENOSPC`，显示按设计停用、跟踪继续。该次结果不计为健康显示持续负载通过。后续带系统调用记录的 30 秒复现未出现同一错误，观察到每次提交后的完成事件读取；尚不能据此判定首次故障已解决。原始记录在 `.docker-artifacts/hdmi-osd-20260923/` 及板端独立验收目录保留。

奇数尺寸边界输入还暴露了 RGA Y400 对偶数宽高的要求。641×479 灰度帧原先触发明确的 RGA 参数错误；已修复为只在显示线程复制阶段将奇数尺寸灰度扩展为等值 BGR，再由 RGA 缩放。保留原图尺寸、行跨度与坐标，不裁切边缘或修改源帧。修复后真实生产板两轮 EOF 各消费 90 帧；显示计数达到 `submitted=180,displayed=180`，没有尺寸错误。输入文件移走后明确报告启动失败；恢复后新轮消费 2 帧，计数达到 `submitted=182,displayed=182`，正常退出。该结果证明提交与生命周期路径，不替代 HDMI 像素观察。

## 可回退部署与文件身份

生产板独立目录为 `/home/hzhy/drone-tracker-osd-20260923/`。`install-off/` 保留默认关闭构建，`install-on/` 为最终启用 HDMI、关闭可选消息适配的版本。两者安装入口均为 `lib/drone_tracker/drone_tracker_node`。安装只执行根包的安装脚本，不把未构建的无关基准程序描述成已安装。

两个副本均完成 `ldd -r` 检查，无缺库或未解析符号。最终启用副本不依赖 `quadrotor_msgs` 动态库。未切换系统服务使用的正式程序，也没有覆盖系统 RKNN Runtime。

| 文件 | SHA-256 |
|---|---|
| OSD 开启节点 | `6846910feddde3cb6da12b01d753e3c7363d8a1ba728fd67ddffae68ebadbb02` |
| OSD 关闭回退节点 | `a90ca1bb0d55e1c928d24557b4a94b0d3954d1aa450b902b5b7154a3821ec014` |
| 合成视频配置 `synthetic.yaml` | `0a25315b9ab1a81d403dc5431343fc780d1b92c75bf0ce42480d7e7a64464fde` |
| 640×480、20 FPS、660 秒合成视频 | `3e344dd4afab97a1b0b3b9c43ad47f8e480c8f4d58ca8e0b7d39f9417de6c1f9` |
| 641×479、90 帧边界视频 | `7d9c2e0961678812126fca978b74da2125449a9e90b61f4d57573aff34ae0a2b` |
| 系统 RKNN Runtime，部署前后相同 | `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` |
| YOLO | `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3` |
| FP16 Template Backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| FP16 Search Backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| FP16 Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

模型来自既有批准的 RKNN 文件，没有训练或转换；未加入 Git。验收脚本、详细日志和资源采样在本地 `.docker-artifacts/hdmi-osd-20260923/` 保留。板端构建日志位于 `/tmp/drone-osd-node-20260923/`，最终源码副本位于其 `src/drone_tracker/`。板端临时目录不是长期部署依赖。

## 未完成的生产验收与实际阻塞

2026-09-23，用户明确决定将任务 4.6 的 HDMI 实际画面观察留待后续单独完成。该决定只推迟视觉验收，不代表其他生产检查通过。

| 检查 | 实际结果 | 结论 |
|---|---|---|
| 最终节点 EOF、输入缺失、恢复 | 两次 EOF 各 90 帧；恢复新轮 2 帧；显示计数同步增加；退出码 0 | 自动化路径通过；屏幕冻结提示尚未观察 |
| 同进程 20 轮启停 | 全部 20 轮有输出并可停止；观察到的停止耗时 0.255–0.340 秒 | 中途显示故障，不能勾选健康显示多轮验收 |
| 最终版本显示故障 | `OSD paired plane commit: No space left on device`，即 DRM 原子提交返回 ENOSPC | 已重复复现，根因未确认；不是磁盘空间诊断结论 |
| 故障后的业务 | 显示停用后跟踪和 ROS 输出继续；正常停止可退出 | 证明故障隔离，不能替代持续显示通过 |
| 主动注入 EIO | 准备了第 100 次提交失败的工具，但该轮在注入点之前先遇到真实 ENOSPC | 不计为预期故障注入成功 |
| 至少 10 分钟持续负载 | 20 轮后进入持续阶段，但显示已失效，主动 SIGINT 停止测试 | 未完成，不把剩余仅跟踪运行计为显示负载 |
| DRM 调用审计 | 100 次提交时已读取 99 次完成事件；尚未到下一个百次记录点即遇到 RKNN 致命等待失败 | 不能据此断言 ENOSPC 来自事件队列泄漏 |
| RKNN 诊断期失败 | `search_backbone.rknn: RKNN wait failed; device access cannot be excluded` | 当前后端的保护逻辑终止进程；未证明与 OSD 的因果关系，未修改模型后端 |
| OSD 开关性能对照 | 由于前述阻塞未执行最终对照 | 未完成 |

20 轮空闲点 RSS 从 126996 KiB 到 127712 KiB；线程从 17 降到 16，文件描述符从 49 降到 39。后两项下降对应显示故障后的资源回收。这不是健康显示长期无泄漏的证明。停止耗时是收到 OFF 后直到测试端观察到停止消息的时间，不是屏幕冻结延迟。

`endurance-result.json` 的 `RuntimeError('node exited 0')` 来自主动停止无效长测，不是节点崩溃。`audit-result.json` 的 `KeyError('VmRSS')` 是采样器读取已退出进程时的次生错误；真正的进程终止原因以 `audit.log` 中 RKNN 致命等待失败为准。两个失败记录均未删除或改成通过。

验收进程已退出。RKNN 异常终止绕过了析构，显示模式暂留在 720×480。已依据测试前 `modetest.txt` 记录，校验原帧缓冲 213 仍为 1280×720 后，恢复原连接器 177、CRTC 75 的 1280×720@60 模式和该帧缓冲。`drm-postflight-restored.txt` 记录恢复结果。上述编号仅用于本次环境收尾，不写入产品的设备选择逻辑。未停止桌面服务、调频或绑核。

未测项仍包括实际 HDMI 灰度与颜色、运动框配对、500 ms 屏幕冻结期限、显示端到端延迟、提交耗时分布、健康显示下持续 10 分钟资源趋势和 OSD 开关性能对照。合成画面无目标捕获，因此不宣称目标跟踪质量或相机标定通过。任务 4.6、6.2–6.4 保持未完成，不同步或归档为已验收。

部署与两种回退方法见 [RK3576_HDMI_OSD.md](../../../docs/RK3576_HDMI_OSD.md)。下一步先定位 DRM ENOSPC 和 RKNN 等待失败，再重跑健康显示多轮及持续负载；实际画面验收依用户决定后续单独完成。

## 任务与证据索引

| 任务 | 实现与证据 | 当前边界 |
|---|---|---|
| 1.1–1.3 | `presentation.h`、`presenter.*`、`osd_presenter_tests` | 公共显示控制已验证 |
| 1.4–1.5 | `src/osd/tests/presenter_test.cpp` | 使用可阻塞后端和可控时钟，不能替代实机延迟测量 |
| 2.1 | `geometry.*`、`osd_geometry_tests` | 包含规格中的坐标实例 |
| 2.2–2.4 | `layout.*`、`status_glyphs.h`、`osd_layout_tests`、PNG 快照 | 已检查 CPU 画布，HDMI 输出另验 |
| 3.1–3.2、3.4 | `ros_telemetry.*`、`osd_telemetry_tests` | 标准消息契约已验证 |
| 3.3 | `low_level_feedback.*`、真实 LowLevelFeedback 定义 | 启用构建与真实消息测试结果单列；关闭构建不依赖该包 |
| 3.5 | `src/tests/ros_osd_telemetry_test.cpp` | 标准遥测到模拟显示已通过 |
| 4.1 | `environment-check.md` | 生产板能力查询已完成 |
| 4.2–4.5 | `src/platform/rk3576/osd/`、生产板探针 | 已有真实提交/完成/重开证据；尚非完整像素验收 |
| 4.6 | 带帧号的 `osd_hdmi_probe` | 用户明确推迟实际输出观察，保持未完成 |
| 5.1–5.3 | `drone_tracker_node.*`、两个 RK3576 参数文件 | 复用原消费和控制路径，默认关闭 |
| 5.4 | 根及 OSD CMake 目标 | 默认关闭不查找显示或自定义消息 SDK |
| 5.5 | `ros_video_tests`、`ros_initialization_tests` | 受控模型业务兼容性已验证 |
| 5.6 | ARM64 全量回归、板端定向构建 | 最终 OSD 开启相关 8/8；两种可选消息配置均验证 |
| 6.1 | 独立安装目录、产物指纹 | 两个安装副本、完整指纹及动态依赖记录已保存 |
| 6.2–6.4 | 真实模型集成、20 轮及持续负载 | 真实模型路径部分通过；ENOSPC 与 RKNN 等待失败阻塞验收 |
| 6.5 | `docs/RK3576_HDMI_OSD.md` | 已给出依赖、参数、部署及两种回退方式 |
| 6.6 | 本记录及 `tasks.md` | 按证据维护完成状态；不归档 |
