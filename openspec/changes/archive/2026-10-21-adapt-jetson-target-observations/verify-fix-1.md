# 独立验证与修复：第 1 轮

使用 change：`adapt-jetson-target-observations`。本轮由新建子代理独立执行，先核对全部规划工件、实现和证据，再进入修复阶段。客户端日期为 2026-10-20。

## 初始核验

| 维度 | 结果 |
|---|---|
| 完整性 | OpenSpec 聚合清单为 33/33；五份 delta specs 均可读，11 项新增、11 项修改需求可定位实现；无删除或改名需求 |
| 正确性 | 数学、消息、轮次、记录、诊断与历史比较路径均已核对；发现诊断配对完整性警告 W2 |
| 一致性 | 八项设计选择均有实现；发现 RK3576 新姿态规则泄漏到 Jetson 路径的警告 W1 |

初始 CRITICAL 为 0，WARNING 为 2，SUGGESTION 为 0。

### W1：RK3576 姿态回退策略改变 Jetson 行为

`src/hybrid_tracker/rotation_history.h:30` 将公共 `push()` 的倒序样本策略由忽略改为清空历史。Jetson 节点同样调用此函数。`src/drone_tracker_node.cpp:1497` 也将新原始消息记录顺序和非法四元数校验直接应用到 Jetson。设计要求新增 ROS 行为仅用于 RK3576，camera delta 要求 Jetson 保持原行为。

修复建议：将回退复位改为 RK3576 调用方显式选择，保留公共接口原有默认策略；仅在 RK3576 启用新回调校验和原始消息记录顺序。增加两种历史策略测试。

### W2：诊断缺少半对时仍可能被报告完整

`tests/tracking_regression/collect.py:183` 只检查存在的调用是否各自配对，没有验证每个观测身份同时拥有两种载荷和两话题调用。`metrics.py:250` 对缺失 LOS 载荷默认取 false，可能把缺证据算成已知无效 LOS；它也没有校验控制通知的两话题调用。新增正向测试仅创建 ControlBbox 起止而无 ControlLos，仍断言 `record_complete=true`。

修复建议：为版本 2 按 run、round、观测序号核对两份载荷及两个话题的完整起止，缺失、重复、时间不一致或控制身份错误应保留为不完整证据；禁止以无效 LOS 零值补齐缺失载荷。补充完整对、删整段调用、缺载荷、部分失败和控制半对反例。

## 证据边界

已阅读 `validation.md`、六次固定输入回归汇总、相机 OFF 专用复核以及最终服务恢复证据。固定输入均未产生有效目标；预测模式仅 10–11/900 帧采用姿态参考，其余回退采集参考。有效目标几何由受控数学与真实公共跟踪器测试验证，不能将这些录像运行说成角度精度验收。

生产板标识为 HZ-RK3576_WuKong、machine-id `1fe0a397f2ae4343896c7cf9d253a608`，本次实际内存约 7.9 GiB。相机 v3 的真实时间戳失败保留；v4 使用生产默认关闭 debug 图像完成 21 轮和 60 秒联合负载。四组矩阵原始日志未改写；OFF 复核从接受停止后的控制版本匹配首次无框显示，不误用 ON 后目标状态变化的延迟。

完整 Jetson SDK/硬件运行、真实标定与实飞精度不在可用证据中。环境没有配置或提供 clang-tidy/clangd。上述边界不能改称通过；不要求以额外物理精度验证替代本 change 的接口验收。

## 修复及复验

### 已完成的修复

- W1：`RotationHistory::push()` 的回退复位变为显式参数，默认保留原有忽略倒序行为。RK3576 的姿态回调显式选择复位；新记录顺序与非法四元数校验也限制在该平台。`target_los_tests` 同时检查 RK3576 复位和 Jetson 默认不复位。
- W2：`metrics.observation_issues()` 为 collector 和指标计算提供相同的版本 2 核验。每个观测必须有两份有效载荷和四个调用边界，且时间、轮次、观测序号、update 身份及控制标记一致。缺失证据令指标不完整，不能当成有效统计中的零值。控制话题的发布失败也单独记录。新增删除整个话题、删除载荷、重复载荷、错误时间/身份和部分失败反例；正向测试改为完整的六事件通知。

这些修改没有改变 RK3576 已批准的姿态回退和消息生成行为；它们恢复 Jetson 边界并加强离线证据检查。实机日志仍对应其保存的原始程序指纹，不将宿主复编译后的新指纹冒充原板端程序。

### 全部需求与场景的定位

下表覆盖 22 项需求及其 72 个场景。场景核验结合实现分支、受控测试和已保存实机证据，不把日志中的 qualified 当作真实目标精度通过。所有 delta specs 均为新增或修改，没有删除/改名检查。

| 能力与需求 | 场景数 | 实现与核验依据 |
|---|---:|---|
| target-observations：目标框与视线消息具有共同身份 | 2 | `target_observation.h`、`publishObservations()`；消息构造测试、每话题失败证据测试 |
| target-observations：框的浮点精度和有效性明确 | 3 | `HybridTrackerImp::update()` 的 Rect2d 裁剪；`observation_output_tests`、`target_observation_tests` |
| target-observations：角速度补偿相机旋转并按时间平滑 | 3 | `target_los.h`；`target_los_tests` 的几何、复合旋转、非等比内参与 0.3 秒边界 |
| target-observations：参考时刻与相机姿态一致 | 3 | `RotationHistory::reference()`；300000/300001、未来/早于帧/空历史测试，两种模式实机回归 |
| target-observations：重复抑制与速度复位各自生效 | 3 | builder 先复位后守卫；重复重锚定、新轮较早时间测试，`repeatedObservationKeepsImageAndState()` |
| target-observations：姿态重复和回退不制造倒序观测 | 2 | 历史代次与 builder 时间守卫相互独立；`references()` 和消息新轮测试，运行说明中的 OFF/ON 恢复步骤 |
| target-observations：OFF 发送一次明确的失效观测 | 2 | 控制锁中的 `notifyObservationOff()`，先通知后 seal；`observationEndpoints()`、普通与持续模式阻塞推理测试 |
| target-observations：观测启动配置与实际像素一致 | 2 | 启动参数保护、原像素内参和实际帧尺寸；`configuration()`、`observationTemplates()`、帧源尺寸检查 |
| target-observations：ROS 消息队列和可靠性按观测用途配置 | 2 | `setup()` 的实际 QoS 和仅记录 MAVROS 回调；`observationEndpoints()` 的端点与 MAVROS-only 测试 |
| rk3576-ros-video：时间与取最新帧保持一致 | 3 | 唯一帧消费者、原 timestampUs 与 20 ms 可停止等待；既有视频/回归源测试及新默认时间断言 |
| rk3576-ros-video：ROS 接口和结果有效性保持兼容 | 4 | 独立状态与 BGR8 图像路径；`messagesAndRestart()`、重复抑制与未 ON 检查 |
| rk3576-ros-video：ROS bag 记录与关闭互斥 | 9 | bag 生命周期锁、独占 writer 和成对容量检查；`recording()`、`bagQueueLimits()`、`pairedBagAdmission()`、故障隔离及实机开关矩阵 |
| rk3576-ros-camera：ON 和 OFF 控制完整运行轮次 | 13 | `run()/worker()` 与 `continuousWorker()`；`cameraContracts()`、`cleanupAndFailures()`、独立录像、持续启停及真实 21 轮证据 |
| rk3576-ros-camera：图像与预测消息保持原有语义 | 4 | `processFrames()` 的独立观测/图像/状态处理；消息、MAVROS-only、重复参考与持续预览测试 |
| rk3576-continuous-hdmi-stream：新轮任务和输出与旧轮隔离 | 3 | 原单 dispatcher、控制版本和清空 pending 边界；`continuousCameraLifecycle()`、`continuousBlockedNanoAndTelemetry()` |
| rk3576-continuous-hdmi-stream：记录与 ROS 调试图像保持业务边界 | 3 | need_save、recording、visualized_result 三条独立路径；无图像 bag 测试、四种实机开关及 OFF 后预览 |
| rk3576-tracking-regression：测试入口与生产契约隔离 | 1 | 回归节点调用正式生产节点；既有 node-round、fixture 与顺序回放检查，原 replay 实现未改动 |
| rk3576-tracking-regression：实际发布与预测时间分别统计 | 2 | 发布端保存 mono_ns/参考时间与选择计数，指标使用实际 PublishEnd；年龄/滞后测试和重复抑制计数 |
| rk3576-tracking-regression：不同版本先检查可比条件 | 3 | identity 增加 observations 条件；模型、区间、算法与缺失比较条件测试，旧 baseline 未修改 |
| rk3576-tracking-regression：Version comparisons preserve measurement compatibility | 2 | 版本 1/2 解码与旧字段 unavailable；旧日志及元数据兼容测试，未知格式拒绝 |
| rk3576-tracking-regression：成对观测证据可按话题重算 | 2 | double 位复制载荷与独立调用事件；本轮完整性反例、双话题计数与独立无效 LOS 测试 |
| rk3576-tracking-regression：OFF 通知与帧结果分开统计 | 1 | Control 生产者及专门事件，帧统计只选 PublishEnd；完整控制六事件正例、控制半对/失败反例 |

设计的八项选择均已逐项核对：公共数学/平台边界、来源几何、参考时间、配对构造、停止顺序、异步记录、显式像素配置、版本化诊断。代码模式由人工检查与构建验证；自动 C++ lint 和语言服务器检查未验证，原因是环境未配置或提供 clang-tidy/clangd。完整 Jetson SDK/硬件运行未验证，不作为本 change 要求的 Jetson 配置检查的替代结论。

### 本轮检查结果

证据根目录为 `/home/mzh/nickluo-validation-20261020-target-observations/`。

- `verify-fix-1-build.log`：默认 amd64 交叉编译 ARM64 成功，60/60 CTest 通过，套件耗时 164.86 秒。必需的原生操作系统内存限制检查也通过。Python 分组共 87 个不同用例，86 个原生用例和 1 个真实 ARM 中间件用例均通过，无跳过。
- `verify-fix-1-native-retry.log`：独立于 ROS/硬件 SDK 的公共算法原生构建及 11/11 CTest 通过。首次重用 root 所有的既有缓存时因用户写权限失败，原始失败保留在 `verify-fix-1-native.log`；按该缓存原有容器身份重试成功，未修改源码权限或算法。
- 七项专门观测证据测试通过；删除整段 LOS 调用、删除载荷及控制半对的反例均得到不完整结果。控制 LOS 失败不会抹去 bbox 调用成功证据。
- `verify-fix-1-evidence.json`：对六份原始板端回归事件重新执行加强后的完整性核验，问题数均为零。另核对五组相机日志和 summary 的十个指纹，均与只读复核记录一致。未修改原始输入、事件、参考或历史基线。
- `openspec validate adapt-jetson-target-observations --strict` 通过；最终 apply 聚合仍为 33/33。`git diff --check` 通过。

### 最终计分与结论

| 维度 | 修复后结果 |
|---|---|
| 完整性 | 33/33 任务；22/22 新增或修改需求有实现；72 个场景均已定位；删除/改名检查不适用 |
| 正确性 | 22 项需求的实现映射及 72 个场景已核验；W2 已修复，完整性反例与既有实机日志均通过相应预期 |
| 一致性 | 8/8 设计选择一致；W1 已修复；代码模式人工核对通过。自动 C++ lint/clangd 检查未验证：环境无可用或配置工具 |

CRITICAL：0。WARNING：0。SUGGESTION：0。初始两项 WARNING 均已修复。

已执行的检查未发现严重或警告问题。自动 C++ lint 和语言服务器检查未验证，原因是工具不可用；因此不以本报告声称所有自动检查完整通过，也不据此宣布无条件归档就绪。完整 Jetson SDK/硬件运行、真实标定/实飞精度，以及 debug 图像开启的 60 秒相机稳定性仍保留前述证据边界，不将其包装为本轮已验证结果。

本轮没有连接或操作板端，没有停止服务，没有更改生产部署、配置、Runtime 或模型。原服务最终恢复为 active/running、enabled 的证据保持不变。后续由新的子代理独立执行下一轮 verify；本轮未提交、推送、同步主规格或归档。
