# 独立复验报告：持续 HDMI 相机预览

## 验证对象与方法

验证日期为 2026-10-13。变更为 `decouple-rk3576-hdmi-stream-from-tracker-trigger`，工作流为 `spec-driven`，源码位于 Git worktree `4a44`，基线为 `fde4f35`，实施修改尚未提交。本报告使用 `openspec-verify-change`，只核对实施，不修改实现或任务勾选，不同步主规格，不归档、不提交，也不连接或干预生产板。

已重新读取 CLI 返回的 proposal、design、tasks 和全部九份 delta specs。delta spec 表示相对主规格的新增或修改约束。CLI 返回的 OpenSpec 根目录为当前 worktree；任务聚合结果为 39/39，`unavailableTrackingFiles` 无缺失项。检索结果为 44 项 ADDED/MODIFIED 要求和 155 个场景，没有 REMOVED/RENAMED 要求。任务勾选只用于清单完整性；行为结论另以实际 diff、测试断言、原始生产日志和记录文件核对。

本次复验在问题修复完成后，由新 sub agent 独立执行。重新运行 `openspec list --json`、`status` 和 `instructions apply`，确认当前根目录、工件和聚合清单。重新执行 `openspec validate ... --strict` 和 `git diff --check`，两者通过。逐项复核当前 diff、原场景索引和修复代码，读取修复前失败、修复后通过及最终 ARM64 构建的原始日志。最终完整 CTest 为 45/45 通过；另由本复验在同一 ARM64 镜像独立执行 `osd_presenter_tests`，1/1 通过，耗时 0.75 秒。

当前源码和安装节点的指纹与修复阶段记录一致。修复后节点 SHA-256 为 `81c8c68e750e3e4bdc143858ee00d21ab7e4fdf36d0ebaae316620cd880e955d`。生产板证据仍对应修复前节点 `c493b99d3c08d8b6fc59bc3ded0f1767b8d3ca63cb3b54e5056c3783f448f9a3`；本次没有把它改称修复后程序的板端运行证据。新修复只调整模式切换期间的诊断前驱，不改变稳态像素处理、提交授权、算法或录像路径。

## 评分表

| 维度 | 结果 |
|---|---|
| 完整性 | 39/39 跟踪任务已完成；44/44 要求可定位实现或明确的验证/文档规则，没有缺失实现 |
| 正确性 | 要求实现映射 44/44，未发现实施偏离；155 个场景均有实现/测试/证据索引，不等于 155 次生产运行。场景覆盖部分未验证（Not verified）：实际屏幕撕裂、颜色与视觉质量缺少直接观测，修复后程序未作板端复验 |
| 一致性 | 八项设计决策均有对应实现，连续新画面间隔已符合第 8 项；代码模式检查未发现显著偏离，验收文档来源已纠正。静态工具检查未验证（Not verified）：镜像没有 clang-tidy/clangd，仓库没有配置其检查命令 |

软件提交完成时间不等于屏幕发光时间。实际屏幕视觉项的证据限制沿用 `validation.md:148`，不是额外要求必须使用光学设备，也不是以未测项推断画面已经有问题。该限制使本次场景覆盖不能被评分为全部通过。历史六轮回放、非持续预览五/十/三十分钟负载、Jetson 实机、目标质量和实飞不属于本变更新生命周期的重复验收，不计为本次跳过的必要检查。

## 当前问题计数

### CRITICAL

0 项。没有发现缺失实现、未完成清单或资源生命周期的关键缺陷。

### WARNING

0 项。原 W1 已通过代码审查和确定性回归消除。

### SUGGESTION

0 项。原 S1 的数据来源和脚本名称均已纠正。

## 原问题的修复核对

### W1 已消除：在途 OFF 不遗漏连续流的新画面间隔

位置为 `src/osd/presenter.cpp:387`，对应 `specs/rk3576-hdmi-osd/spec.md:53` 的“新画面间隔与显示替换统计可关联”、design 第 8 项和任务 6.3。控制版本表示显示模式的变更，用于拒绝尚未交给硬件的旧包。间隔代际标识一个连续视频段，在结束、重启或过期等待时变化。

当前成功完成分支对持续模式使用视频流身份，对非持续模式仍使用控制版本；两者都要求间隔代际匹配且接收仍活跃。因此，硬件已授权的画面在 OFF 改变跟踪模式后安全完成时，仍更新同一流的 `last_display_`。未授权旧包继续由 `version == version_` 拒绝，没有放宽旧结果显示边界。`beginStream():70`、`endStream():91` 和过期处理 `:356` 仍重置前驱并改变间隔代际。旧 `beginRound()/endRound()` 的断链规则也保留。

`src/osd/tests/presenter_test.cpp:554` 的 `continuousInFlightInterval()` 在两条渲染路径都执行硬件授权后阻塞、OFF、继续完成。三张画面在虚拟单调时钟 10、20、40 ms 完成。断言先确认 10 ms 样本，再确认两个样本合计 30 ms、最大 20 ms，成功显示数为 3。测试同时确认旧跟踪包拒绝、OFF 后无算法结果和目标装饰、阻塞期间缓冲不复用、原帧/读视图均释放。它在原实现上报 `in-flight OFF omitted the 10 ms interval`，修复后通过。原停止/重启、过期等待、失败与所有权用例也包含在本复验通过的 presenter 目标中。

证据为 `.docker-artifacts/log/continuous-preview-finding-fixes/regression-before.log`、`regression-after.log` 及本次 `.docker-artifacts/log/continuous-preview-reverification/presenter-test.log`。新测试源与测试二进制指纹已核对。由代码与确定性测试可确认 W1 的软件行为修复；没有另称已取得修复后硬件运行证据。原生产稳态性能窗口排除了模式切换，其已测结果继续归属于原程序身份。

### S1 已消除：bag 与实时订阅的证据来源正确

`validation.md:128` 已明确：22 个 bag 共反序列化 17227 条消息，包含 `/fpv/gimbal`、`/fpv/odom` 和 `/fpv/target_bbox`，不包含 `/fpv/tracker_image` 或 `/fpv/tracker_state`，各 `states` 均为空。独立解析 `board/bag-readback.json` 得到相同结果。

状态值 0、1、2 改为归属于各窗口 `events.jsonl` 的实时订阅，并由 `node.log` 交叉支持。独立统计 cached-overlay 每个状态各 20 次，legacy 与 OSD 关闭每个状态各 1 次。该更正没有改变 bag 或 ROS 状态发布契约。`validation.md:142` 的计算脚本现为 `analyze.py`；证据目录存在该文件，不存在原误写的 `analysis.py`。

## 关键行为核对

### 视频、轮次与控制边界

`src/drone_tracker_main.cpp:12` 在构造成功后调用 `startService()`；`src/drone_tracker_node.cpp:43` 限定 RK3576 相机且 OSD 开启。`startService():183` 建立一条流，录像只选择控制能力，不请求会话；模型/初始化失败不会留下自动采集服务。主执行器每 100 ms 检查服务失败并返回非零。

`dispatchFrames():248` 是持续模式原相机队列的唯一消费者；`processFrames():981` 改为取单份待处理任务。忙时只替换尚未处理的任务，正在推理的 `FrameRef` 仍拥有只读图像。Running 就绪在分发边界设置 `sequence-1` 截止值，只有下一张新图进入跟踪；presenter 同时校验流、轮次、控制版本、包类型及截止序号。显示包只使用 `display_box`，ROS 框保留 `realtime_box` 的预测时间。

`stop():143` 先关闭输出、清空待处理任务、切 Idle 预览、封闭 bag 和原画面录像，再由常驻 worker 等待旧访问。快速 ON/OFF/ON 只改变最后期望状态，下一轮在 `finishRound()` 后重置。控制回调没有推理、录像准备或文件收尾等待。发布边界在 `processFrames():1034` 和 `:1165` 重新校验控制身份，OFF 后不启动旧结果的 ROS 发布；已交中间件的尾部消息仍允许延迟送达。

模拟覆盖两种路径与图像开关组合、阻塞 YOLO/NanoTrack、晚到结果、MISSION、failsafe/电池更新、异常跟踪、采集故障与重复 shutdown。其证据是 `src/tests/ros_video_test.cpp:973`、`:1026`、`:1080`。旧视频 EOF/OFF 冻结、OSD 关闭相机和 Jetson 配置测试保留。

### 录像资源与计数

固定 `SessionDispatcher` 通过逐帧共享引用覆盖 stage/publish/cancel/disposition；这些操作分别是候选暂存、最终接收、撤销和采集处置记录。`detach()` 与最终 publish 同步，OFF 后候选撤销；资源控制线程等待 `drain()` 再关闭元数据。像素复制不在控制锁内执行。关掉录像只取消其接收，Gray8 分流仍继续。

`sc233hgs_camera_frame_source.cpp:78` 串行准备/收尾并比较请求版本，取消后准备成功也不能接入旧轮。`Session::start():287`、`stop():331` 各有 3 秒父进程预算，旧进程待回收保护仍保留。prepare-delay、OFF 位于 stage/publish 之间、共享引用存活、同流两会话与旧进程隔离分别见 `session_test.cpp:78`、`:113`、`:165`。既有 SC233HGS 最终归还/期限/撤销用例和存储故障检查继续通过。

`metadata.h:65` 保存开始/结束快照和独立结束原因，camera Running 不改写为停止。`shared_slots.h:180` 记录首个接受身份；既有 storage PTS 算法仍以本会话首个接受帧为零。生产 20 个 cached 会话共享流 1、跟踪轮次 1–20，独立身份及 PTS=0；接受/编码/帧记录可对账，最后 OFF 后接受数保持不变。原尺寸 NV12 录像走采集边界，不使用 OSD 渲染输出。

### 故障、亲和性与清理

相机故障由 `dispatchFrames()` 通知 main，跟踪异常只结束轮次；显示故障保留隔离，不自动重连。shutdown 在禁止任务后停采集、回收分发/worker、等待模型访问、关闭记录及显示。重复执行安全。新增分发线程使用 `cpu_affinity::Role::Capture`，实际工作前的绑定/读回握手由原 `cpu_affinity::start()` 提供。`cpu_affinity_test.cpp` 在 `<cassert>` 前取消 `NDEBUG`，Release 构建的断言没有被移除。

生产策略仍为 Control/Tracking=4、YOLO=5、Display=6、Capture/Recording=7。原始启动日志与 `/proc` 掩码交叉支持职责确认；录像子进程的初始采样缺口通过独立短补测补齐，没有把初始数组缺子进程解释为没有创建进程。

## 生产与构建证据

- 修复后离线证据：`.docker-artifacts/log/continuous-preview-finding-fixes/`；完整构建及 45/45 CTest 通过。源码、安装节点及 presenter 测试的 SHA-256 与该目录 `artifacts.sha256` 一致。本次独立定向复验日志为 `.docker-artifacts/log/continuous-preview-reverification/presenter-test.log`，1/1 通过。
- 原完整离线证据：`.docker-artifacts/log/continuous-preview-offline-final/`；额外 session/storage 与部署检查通过。容器结果没有当作设备运行结果。
- 生产证据：`.docker-artifacts/log/continuous-preview-production/`；指定 `HZ-RK3576_WuKong Board`，系统 Runtime 2.3.2、驱动 0.9.8，库与原正式配置/策略指纹保持。节点安装 SHA-256 已重新核对为 `c493b99d3c08d8b6fc59bc3ded0f1767b8d3ca63cb3b54e5056c3783f448f9a3`。
- 三个核心窗口时长为 cached 150.120、legacy 39.781、OSD 关闭 92.726 秒，均少于 180 秒。cached 初始/最终预览超过十秒、同进程 20 轮、持续 Tracking 64.850 秒。模型使用静止室内输入，没有人工首框或阈值调整；真实 Tracking 工作量不代表有目标真值。
- cached 配对新视频及消费为 59.851 fps；基线 60.021 fps，下降 0.283%。成功新视频组 `render_total` 均值 15.301 ms、P95 桶上界 32.768 ms、`submit_to_display` 均值 19.055 ms，满足指定门槛。计时按样本数合并，未平均每秒百分位。
- 核心 22 个会话的 24 个 TS 段全段独立解码退出码为 0，H.264 原尺寸 1920×1200；22 个 bag 完整反序列化和 SQLite 完整性检查通过。bag 只记录 gimbal、odom、框，状态依据为实时订阅日志，见已解决的 S1 核对。
- 流只启动一次；OFF 清框软件完成最大 cached 29.748 ms、legacy 41.908 ms。短测的内存、线程、文件描述符保持有界；不能推广为长期稳定性。
- `restored-state.txt` 证明原生产服务 enabled、active/running，PID 47141，current 为原 `bundle-20260927T070059Z-2281`；正式配置、CPU 策略和系统 Runtime 指纹未变。验证阶段没有再次访问开发板。

既有录像截断/同步/进程故障证据允许复用。已核对归档 `2026-09-24-add-rk3576-camera-recording` 的 `final-validation.md`、`truncation-validation.md` 与对应实现：194 个副本截断边界及恢复限制有记录。本次没有把它们改称新会话实测，也没有要求断电测试。

## 结论与明确未测范围

已运行的检查中没有发现 CRITICAL、WARNING 或 SUGGESTION，三类当前问题均为 0 项。原 W1 和 S1 已分别通过确定性回归及原始来源核对消除。任务仍为 39/39；44 项要求均有映射，155 个场景名称均在索引中，没有漏列要求或场景。

场景覆盖部分未验证：实际屏幕撕裂、颜色和视觉质量缺少直接观测；软件完成事件、模拟像素和原录像抽帧不能替代这种证据。修复后节点尚未上生产板，本报告不将旧节点的板端性能和生命周期记录归属于新二进制。静态工具检查未验证：镜像没有 clang-tidy/clangd，仓库没有相应检查命令。代码模式一致性已人工审查，不能因此称静态工具已通过。由于这些证据限制，本报告不宣称全部检查通过或完整归档就绪。

目标准确率、运动/小目标/暗光画质、曝光时间精度、真实姿态补偿、长期运行、NPU/编码器设备内存及功耗保持未测。Jetson 只作配置兼容检查。没有执行断电或损坏性实验。这些是已声明的证据边界，不是新增加的归档前置条件。归档应使用其独立工作流和确认规则。

## 全部要求与场景的证据索引
以下路径以仓库 `src/` 为前缀，`node.cpp` 表示 `src/drone_tracker_node.cpp`。场景名称逐项保留，便于与原 delta spec 交叉核对。证据引用可由多个组件测试与代码路径共同支持；索引不声称每个条件都曾在生产板独立注入。非持续预览历史长测属于原模式条件，本次仅核对其规则仍保留。
### osd-presentation
**1. 图像与跟踪结果一次性配对提交**

实现：`node.cpp:248,1041；presentation.h:72；presenter.cpp:225`。测试/证据：presenter_test.cpp pairingAndOwnership/continuousPreview；ros_video_test.cpp osdCompatibility。

- 相邻两帧并发提交与显示
- 未消费帧
- 未触发的预览

**2. 无效框清除且状态含义不变**

实现：`layout.cpp:177,321,388；presenter.cpp:349`。测试/证据：layout_test.cpp:156；hud_style_test.cpp；continuousBlockedNanoAndTelemetry。

- 有框切换为无框
- 数据没有置信度
- 停止时保留任务文字

**3. OFF 冻结最后画面并继续显示遥测**

实现：`node.cpp:143；presenter.cpp:106,349`。测试/证据：continuousPreview；stopBeforeHandoff/stopInFlightAndRestart；生产 OFF 日志。

- 正常跟踪后停止
- 首帧之前停止
- 相机 OFF 后有新画面
- 预热中 OFF

**4. 新轮与等待状态不误用旧结果**

实现：`node.cpp:267,332；presenter.cpp:225,356`。测试/证据：continuousCameraLifecycle；timingIntervalsAndStale；旧视频 EOF 回归。

- 停止时推理尚未结束
- 合法长帧间隔
- 运行中重复 ON
- 相机新轮独立

**5. OSD 不改变既有业务输出**

实现：`node.cpp:1034,1165`。测试/证据：osdCompatibility；continuousCameraLifecycle 两图像开关；22 bag 回读。

- 关闭 ROS 调试图像
- 调试图像开启但仅预览

**6. HDMI 状态切换诊断日志**

实现：`node.cpp:1094；presenter.cpp:429`。测试/证据：missionLifetime；labelChangesDoNotLog；生产 OSD/control 日志。

- 跟踪中收到任务触发
- 跟踪中收到停止触发
- 输入含标签但画面没有标签
- 仅输入标签改变
- 持续预览停止日志

### osd-ros-telemetry
**1. 告警和冻结提示同时可辨识**

实现：`layout.cpp:321；node.cpp:143`。测试/证据：ros_osd_telemetry_tests；continuousBlockedNanoAndTelemetry；layout/hud 测试。

- 冻结时收到失效保护
- 实时预览与告警并存
- 任务覆盖期间停止

### production-binary-deployment
**1. 上电启动与核心限制**

实现：`main.cpp:12；deployment/run-camera；CPU 策略`。测试/证据：test_cpu_policy.py/test_package.py/test_prepare_display.py；生产启动/恢复日志。

- 启用服务
- 使用生产策略手动启动
- 服务已启动但跟踪关闭

**2. 持续预览升级和回退说明明确**

实现：`deployment/README.md 持续预览说明；生产模板注释`。测试/证据：上述部署检查；YAML 数据与基线一致；原配置终态哈希。

- 回退旧版启停行为
- 升级前仅预检

### rk3576-camera-recording
**1. 录像显式启用并随相机整轮运行**

实现：`sc233hgs_camera_frame_source.cpp:40,54,113；shared_slots.h:153`。测试/证据：liveCameraControl；independentCameraRecording；warmupLifecycle/recordingFinalConfirmation；生产首十秒。

- 整轮无目标
- 预热观察帧不进入视频
- 首帧之前取消或失败
- 重复触发
- 默认兼容
- 无效录像参数不阻止初始化
- 服务预览不启动录像
- 已预热相机开始录像

**2. 时间和采集身份可追溯**

实现：`metadata.h:65；storage.cpp:307；shared_slots.h:180`。测试/证据：dispatcherBoundary；sharedRecordingSlots；生产逐帧记录/各会话 PTS=0。

- 首个合格帧定义视频起点
- 中间一帧丢失
- 跨轮帧序号重复
- 同流两次录像
- 停止录像不停止相机

**3. 录像资源有界且故障隔离**

实现：`session.h:40；session.cpp:287,331`。测试/证据：dispatcherBoundary/liveCameraControl/lifecycle；storage_test.cpp failure；生产收尾/资源。

- 录像工作进程崩溃
- 旧存储操作不能退出
- OFF 位于暂存和发布之间
- 准备中取消

**4. 非断电验收明确性能与证据限制**

实现：`recording/storage/inspect 既有实现；validation.md`。测试/证据：新 24 段解码与生产检查；复用归档 final-validation/truncation-validation；历史五分钟本次不重跑。

- 所有模拟检查通过
- 实际录像影响正常使用
- 持续预览会话定向验收

**5. 活跃采集中的录像会话边界可确认**

实现：`session.h:40；sc233hgs_camera_frame_source.cpp:78`。测试/证据：dispatcherBoundary/liveCameraControl/lifecycle 旧进程隔离。

- 相邻会话不串帧

### rk3576-continuous-hdmi-stream
**1. 持续预览模式具有明确适用范围**

实现：`node.cpp:43,183`。测试/证据：continuousCameraLifecycle；osdCompatibility/cameraContracts；配置测试。

- 生产相机使用现有配置
- 文件输入保持原行为
- 相机关闭 OSD

**2. 服务生命周期控制视频流**

实现：`main.cpp:12；node.cpp:183`。测试/证据：continuousCameraLifecycle；SC233HGS warmup 测试；生产初始预览。

- 等待首次 ON
- 预热中收到 OFF

**3. OFF 后预览不等待算法或记录收尾**

实现：`node.cpp:143,332；session controller`。测试/证据：continuousBlockedNanoAndTelemetry；liveCameraControl prepare_delay；生产 OFF。

- 推理尚未返回
- 录像收尾较慢

**4. 唯一取帧者和有界分发**

实现：`node.cpp:248,295；presenter.cpp:225`。测试/证据：continuousBlockedNanoAndTelemetry 槽替换；pairingAndOwnership。

- 跟踪落后于相机

**5. 视频与算法结果采用明确的配对模式**

实现：`node.cpp:267,1041；presenter.cpp:225,356`。测试/证据：continuousPreview；timingIntervalsAndStale；cached/legacy 配对窗口。

- 采集六十帧而处理二十五帧
- 运行中暂无处理结果

**6. 新轮任务和输出与旧轮隔离**

实现：`node.cpp:267,332,1034,1165`。测试/证据：continuousCameraLifecycle；continuousBlockedNanoAndTelemetry；continuousPreview 截止序号。

- 旧请求完成于新轮之后
- 从预览进入新轮

**7. 重复及快速启停不累积资源**

实现：`node.cpp:90,143,332`。测试/证据：continuousCameraLifecycle 快速启停及两图像开关。

- 停止中多次改变期望状态

**8. 记录与 ROS 调试图像保持业务边界**

实现：`node.cpp:395,1034,1165`。测试/证据：continuousCameraLifecycle；bagWithoutImagesAndDualRecording；生产零初始记录。

- 开启调试图像但未触发
- 结束一轮后继续预览

**9. 故障按资源边界隔离**

实现：`node.cpp:209,286,361；main.cpp:17`。测试/证据：continuousStartupFailures/continuousBlockedNanoAndTelemetry；显示失败/初始化测试。

- 未触发时相机失败
- 跟踪处理失败

**10. 定向验收具有统一时长和证据边界**

实现：`validation.md；生产 harness.py`。测试/证据：cached/legacy/osd-off events/resources/node.log；analysis.json；程序/模型指纹。

- 完成定向联合检查
- 路径与基线对照
- 生产证据尚缺

### rk3576-hdmi-osd
**1. 启动 OSD 前必须关闭桌面显示**

实现：`hdmi_backend.cpp:40–83；部署前置检查`。测试/证据：test_prepare_display.py；初始化/故障回归；生产独占 DRM 日志；本次不主动启动桌面。

- 桌面仍占用显示设备
- 控制权查询出现误判
- 关闭桌面后启动并退出
- 采集停止但 OSD 仍显示

**2. 验收区分模拟测试与生产板证据**

实现：`validation.md；独占 DRM 与节点联合运行`。测试/证据：生产三个定向窗口；继承历史模式要求，本次不重跑旧长测。

- 带真实模型的生产板运行
- 只有模拟测试通过
- 持续预览定向验收

**3. 新画面间隔与显示替换统计可关联**

实现：`presenter.cpp:387；timing.cpp:62`。测试/证据：continuousPreview/timingIntervalsAndStale；continuousInFlightInterval 两路径的虚拟 10/20 ms 样本及本次独立 ARM64 presenter 复验，原 W1 已解决。

- 一秒显示两帧
- 停止后重新开始
- OFF 仍连续显示

**4. 计时验证保持原显示语义并提供生产证据**

实现：`presenter.cpp:316；timing.*`。测试/证据：osd_timing_tests；presenter 虚拟时钟/替换/冻结/故障；生产计时窗口。

- 只有无硬件测试
- 持续预览生命周期计时

**5. 可选择且可回退的 OSD 渲染路径**

实现：`presentation.h；node.cpp:477；hdmi_backend renderer 选择`。测试/证据：osd_yaml/config_tests；osdCompatibility 两路径；生产 legacy。

- 默认启用优化路径
- 启动时选择旧路径
- 拒绝无效路径
- 拒绝运行期切换
- legacy 不是启停回退

**6. 优化路径保持显示语义**

实现：`hdmi_backend.cpp；layout.cpp:177,321`。测试/证据：layout_test.cpp 两路径像素比较；pipeline_state_tests；ROS/生产配对，实际屏幕颜色未直接观测。

- 新视频帧和遥测配对显示
- 仅遥测变化
- OSD 关闭后冻结
- 带填充步长的输入
- 持续预览关闭跟踪

**7. 优化路径满足生产板流畅度和延迟门槛**

实现：`timing.*；生产 analysis.json`。测试/证据：cached 成功新视频合并桶及 node_window 对比基线。

- 同板 A/B 性能验收
- 保护跟踪吞吐量
- 持续预览性能不以名义刷新替代

**8. 优化路径通过持续运行和切换验收**

实现：`node.cpp 持续轮次；presenter.cpp 显示边界`。测试/证据：生产 20 轮与资源；模拟所有权/失败；实际屏幕撕裂和视觉质量未直接观测。

- 重复开启与关闭
- 十分钟满负载运行
- 相机切换保留实时图像

**9. 动态文字沿用参考视觉但尊重现有数据语义**

实现：`layout.cpp:321,388；short_text.cpp；telemetry`。测试/证据：hud_style/layout/telemetry tests；continuousBlockedNanoAndTelemetry；生产原图序号。

- 电池数据不完整
- 冻结时仍有告警
- OFF 时实时告警

### rk3576-ros-camera
**1. 相机入口和配置显式且固定**

实现：`node.cpp setup/参数 guard/continuous_preview_`。测试/证据：configuration/cameraContracts/independentCameraRecording；Jetson 配置；AR0234 复用分发与已有软件采集组件。

- 合法软件模式
- 多位设备号
- 标定前使用模拟内参
- 记录请求
- 运行期改频率
- 合法 SC233HGS 连续配置
- SC233HGS 配置软件触发被拒绝
- 运行期改传感器型号
- 独立录像配置
- 运行中修改录像周期
- 其他输入启用新录像
- Jetson 输入行为保留
- AR0234 持续预览

**2. ON 和 OFF 控制完整运行轮次**

实现：`node.cpp:90,143,332；SC233HGS 控制`。测试/证据：cameraContracts/continuousCameraLifecycle；liveCameraControl；warmupLifecycle。

- 首次启动失败后重试
- 检测中停止再启动
- 没有目标仍录制
- 预热中 OFF
- 预热失败后新轮
- 录像准备后相机预热失败
- 录像故障后下一轮重试
- 录像收尾超时
- 录像参数无效但跟踪可运行
- 持续预览反复启停
- 持续预览预热中 OFF
- 持续预览相机故障

**3. 图像与预测消息保持原有语义**

实现：`node.cpp:1005,1074,1165`。测试/证据：messagesAndRestart/cameraContracts；continuousCameraLifecycle；实际订阅/回读。

- 帧时间与当前时间不同
- 无目标新轮
- 预览不扩展 ROS 图像

**4. 每轮故障和采集量可诊断**

实现：`node.cpp:856；metadata.h:65`。测试/证据：组件采集计数；Stream counters/生产窗口差值；显示间隔的原 W1 已解决，见 continuousInFlightInterval。

- 推理慢于拍摄
- 正常启动丢弃观察帧
- 一条流包含多轮跟踪

**5. ROS 实机验收不替代模型质量**

实现：`validation.md 限制与定向计划`。测试/证据：真实 Tracking 时长/窗口；可控模型/HybridTracker tests；历史板 2/Jetson/模型质量未冒充新验收。

- 输入稳定但没有识别目标
- 只做短测
- 生产持续预览短测

### rk3576-sc233hgs-camera-frame-source
**1. 启动时间戳预热有界且不交付观察帧**

实现：`SC233HGS device/warmup 既有逻辑；node.cpp:143`。测试/证据：warmupLifecycle/warmupDeadlineAndSequence/warmupFailurePaths；生产只预热一次。

- 初始重复后恢复
- 第 120 帧达到条件
- 重复耗尽检查量
- 到期帧不得挽救预热
- 预热中停止优先
- 预览预热不被跟踪停止取消

**2. 停止重启和失败清理可靠**

实现：`ar0234_camera_frame_source.cpp；node.cpp:209`。测试/证据：copiesYPlaneAndRestarts/reportsCaptureOpenFailure/资源失败与等待停止；生产退出释放。

- 等待中停止
- 失败后重启
- 部分资源取得失败
- 间歇出队不能续期
- 多轮跟踪保持一个开流

**3. 活跃采集的录像接收切换不影响灰度输出**

实现：`session.h:40；ar0234_camera_frame_source.cpp:174`。测试/证据：dispatcherBoundary/liveCameraControl；recordingFinalConfirmation；后续 Gray8。

- 录像关闭而采集继续

### rk3576-thread-affinity
**1. 工作前绑定并确认**

实现：`node.cpp:200；cpu_affinity::start`。测试/证据：cpu_affinity_test.cpp 启动前读回/拒绝执行；continuousStartupFailures；实际掩码。

- 显示线程继承控制核心
- 系统拒绝绑定
- 常驻分发者启动

**2. 轮次重建与录像继承**

实现：`node.cpp:200；worker 进程职责启动`。测试/证据：cpu_affinity_tests；生产常驻 Capture/分发和逐轮 Recording；子进程补测。

- 同进程再次开启
- 录像创建辅助线程

**3. 复用证据并执行定向短回归**

实现：`validation.md 定向 CPU 负载`。测试/证据：production node.log/resources.jsonl；worker command-path 补测；本次不重复旧模式长测。

- 正式实现短测通过
- 测试未形成有效负载
- 新模式职责验证
