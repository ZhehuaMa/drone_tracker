# 独立验证：第 3 轮

本轮由新建子代理按 `openspec-verify-change` 核验 `adapt-jetson-target-observations`。
客户端日期为 2026-10-20。检查对象是指定工作树中的实际源码、测试、五份 delta specs
（描述本次新增或修改行为的规格）、proposal、design、tasks 和保留的验收证据。
本轮没有代码修复，也没有重新运行已通过的完整构建或硬件负载。

## 计分表

| 维度 | 独立检查结果 |
|---|---|
| 完整性 | 33/33 聚合任务完成；11 ADDED + 11 MODIFIED = 22/22 需求有实现；没有不可读的任务文件；删除与改名需求不适用 |
| 正确性 | 22/22 需求完成实现映射；72/72 场景有对应实现及受控测试或保留的板端证据；没有发现规格偏离或缺失场景 |
| 一致性 | 8/8 设计选择与实现一致；代码模式人工检查完成；自动 C++ lint 与语言服务器部分未验证：环境缺少可用或已配置的 clang-tidy/clangd |

初始问题：CRITICAL 0、WARNING 0、SUGGESTION 0。
最终问题：CRITICAL 0、WARNING 0、SUGGESTION 0。
这里的最终计数与初始计数相同，没有通过修改需求、取消场景或降低测试标准取得清洁结果。

## 需求和场景核对

下表路径相对工作树根目录。`node` 指 `src/drone_tracker_node.cpp`，`ros-test` 指
`src/tests/ros_video_test.cpp`。场景数按本轮读取的规格独立计数；总数为 72。
LOS 是目标相对于相机的视线方向。KF 是估计并预测目标框的卡尔曼滤波器。

| 能力与需求 | 场景数 | 本轮检查的实现和覆盖证据 |
|---|---:|---|
| target-observations：目标框与视线消息具有共同身份 | 2 | `src/target_observation.h:26`、`node:1414`；消息构造测试与独立话题失败证据测试 |
| 框的浮点精度和有效性明确 | 3 | `src/hybrid_tracker/hybrid_tracker.cpp:866`；真实 KF 小数及越界输出测试、消息构造中缺姿态保留有效框 |
| 角速度补偿相机旋转并按时间平滑 | 3 | `src/hybrid_tracker/target_los.h:36`；非等比焦距、复合旋转、运动目标、tau=0/0.12、300 ms 间隙边界测试 |
| 参考时刻与相机姿态一致 | 3 | `src/hybrid_tracker/rotation_history.h:78`、`node:1182`；采集默认、姿态新鲜度 300000/300001 us、未来/早于帧/空历史测试 |
| 重复抑制与速度复位各自生效 | 3 | `src/target_observation.h:26`、`node:1270`；重复重锚定先复位、新轮较早时间、`ros-test:765` 的图像与状态独立性 |
| 姿态重复和回退不制造倒序观测 | 2 | `src/hybrid_tracker/rotation_history.h:30`、`node:1523`；回退代次、相同时间去重及构造时间守卫测试；旧 KF 对倒退帧也保留 RepeatedTime 诊断 |
| OFF 发送一次明确的失效观测 | 2 | `node:231`、`node:1476`；首次 ON 前 OFF、重复 OFF、阻塞 NanoTrack/YOLO、旧结果拒绝和持续预览测试 |
| 观测启动配置与实际像素一致 | 2 | `node:137`、`node:692`；三个模板、独立 fx/fy、实际尺寸、非法 tau 和明确要求重启的运行期拒绝断言 |
| ROS 消息队列和可靠性按观测用途配置 | 2 | `node:916`、`node:934`；`ros-test:719` 的实际端点 QoS 与仅 MAVROS 输入不生成有效 LOS 检查 |
| ros-video：时间与取最新帧保持一致 | 3 | 原帧源与 `node:1176`；慢消费者取最新帧、可停止等待、默认采集时间及原时间轴检查 |
| ROS 接口和结果有效性保持兼容 | 4 | `node:1213`、`node:1295`、`node:1376`；无目标、图像采集时间、状态重启、未 ON 不播放测试 |
| ROS bag 记录与关闭互斥 | 9 | `src/bag_session.h:70`、`:115`、`:150`；五话题回读、EOF/回调并发、打开/写入/关闭故障、消息与字节容量、无图像数据及元数据测试 |
| ros-camera：ON 和 OFF 控制完整运行轮次 | 13 | `node:166`、`node:427`；`cameraContracts()`、`cleanupAndFailures()`、`independentCameraRecording()`、持续模式和启动故障测试；保留的 21 轮相机证据 |
| 图像与预测消息保持原有语义 | 4 | `node:1274`、`node:1376`；采集/姿态时间、Detecting 新轮、只在 ON 发布独立 BGR8 图像及缺姿态不阻止显示测试 |
| continuous-hdmi-stream：新轮任务和输出与旧轮隔离 | 3 | 现有控制版本、唯一 dispatcher 与 `node:231`；阻塞推理后 OFF/ON、旧结果拒绝、新分发边界及健康预览测试 |
| 记录与 ROS 调试图像保持业务边界 | 3 | `node:534`、`node:1387`、`src/bag_session.h:158`；启动预览不记录、OFF 后停止业务消息、独立录像和 bag/图像四组合证据 |
| tracking-regression：测试入口与生产契约隔离 | 1 | 真实回归入口复用生产节点；顺序 replay 路径无本次代码差异，原 source/round/fixture 检查覆盖其边界 |
| 实际发布与预测时间分别统计 | 2 | `node:1455`、`tests/tracking_regression/metrics.py:453`；参考时间、真实调用完成时间、重复抑制与帧年龄指标检查 |
| 不同版本先检查可比条件 | 3 | `tests/tracking_regression/identity.py:75`；保留模型、区间和配置身份检查，增加观测协议、时间模式、tau、图像 QoS 和 bag 策略 |
| Version comparisons preserve measurement compatibility | 2 | `tests/tracking_regression/collect.py:26`、`:46`；版本 1/2 读取、未知版本拒绝、历史浮点框及 LOS 标为 unavailable 的测试 |
| 成对观测证据可按话题重算 | 2 | `src/diagnostics/event.h:153`、`tests/tracking_regression/metrics.py:170`；double 位复制、六条必要记录完整性、缺整话题/载荷、独立有效性测试 |
| OFF 通知与帧结果分开统计 | 1 | `node:1476`、`tests/tracking_regression/metrics.py:492`；独立控制身份及两话题调用，通知不进入帧级吞吐或有效框分母 |

针对八项设计选择，本轮分别核对公共数学边界、来源数学约定、参考时间与姿态配对、共同
构造与独立传输、停止顺序与锁边界、异步记录、显式像素配置、版本化诊断。锁顺序保持
control → bag 生命周期 → bag 队列；姿态快照结束后才接触构造器，磁盘线程不持控制锁。
推理和图像映射仍在控制锁外，ON/OFF 没有引入第二个取帧者或重开持续相机。

直接读取来源提交 `f176960cd6e1e98c5ea341b5fcac856b508b9865` 的 `target_los.h`，
确认方向、旋转补偿、差分和平滑公式保持来源语义。新增 ROS 逻辑受 RK3576 编译条件限制。
共享 `RotationHistory` 默认继续忽略倒序样本，只有 RK3576 显式选择清空历史。
模型、检测阈值、四个状态数值及既有仲裁决策没有被本次观测出口改写。

前两轮修复均有可核对的回归保护：Jetson 回退默认策略、缺失半对诊断记录的反例，以及
`publish_prediction`/`los.rate_filter_tau` 运行期拒绝必须包含 `restart` 的 ROS 断言。
本轮未发现新问题，无需进入代码修复阶段。

## 验证执行与已有结果

本轮运行 `openspec list --json` 确认本地 root，读取 `status` 和 `instructions apply`
返回的全部工件与聚合任务。`openspec validate adapt-jetson-target-observations --strict`
和 `git diff --check` 通过。没有使用 CLI 的 all_done 状态替代实现检查。

完整构建和测试采用已经覆盖最新代码修改的第 2 轮结果，本轮没有重复运行：

- 默认 amd64 镜像交叉编译 ARM64，60/60 CTest 通过，耗时 164.25 秒；必需原生操作系统
  内存限制检查通过。镜像为 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。
  日志：`/home/mzh/nickluo-validation-20261020-target-observations/verify-fix-2-build.log`。
- 该构建中的 Python 分组为 86 项原生用例和 1 项真实 ARM 中间件用例，全部通过。
  独立七项观测证据测试以及六份保留板端事件的加强完整性检查也已有通过记录。
- 公共算法原生检查 11/11 通过；日志：
  `/home/mzh/nickluo-validation-20261020-target-observations/verify-fix-1-native-retry.log`。
  第 2 轮仅改变拒绝说明与 ROS 断言，没有改变公共算法。

保留的指定生产板证据位于
`/home/mzh/nickluo-validation-20261020-target-observations/apply/board/preserved/`。
本轮读取六次 fixed-input 汇总、相机日志复核口径、开关矩阵和最终恢复审计。两种时间模式
各有三次 qualified 运行，每次 900 对帧观测；qualified 表示输入与证据完整，不表示目标精度。
固定素材在既有模型下没有有效目标。目标几何由受控测试覆盖，不能据此证明物理标定。

生产默认图像关闭的相机运行持续 124.840 秒，包含 21 轮及至少 60 秒跟踪/录像/HDMI
联合负载。四组 bag/图像开关均有保留证据。原图像开启长负载的采集时间戳失败，以及
矩阵 harness 对清框时延取值过宽的原始非零结果均保留；只读 OFF 专用复核没有改写原日志。
这些数据不能证明图像开启的 60 秒稳定性或同条件性能改进。

## 未验证项目和生产恢复

自动 C++ lint 与语言服务器检查未验证，原因是没有可用或已配置的 clang-tidy/clangd。
代码模式的一致性已人工检查，但工具部分不计为通过。完整 Jetson SDK 构建/硬件运行、
真实角度标定、实飞目标质量，以及图像开启长负载稳定性也未验证；它们不由容器结果替代。

实际板卡为 adb `5ceb49407502303d` 的 HZ-RK3576_WuKong，内核 `6.1.141-rt36`，
可见内存约 7.9 GiB；不能外推为 3.8 GiB 机型内存验收。Runtime 2.3.2、驱动 0.9.8
和系统库指纹保持原证据。保留实机二进制早于前两轮修复；最新构建未再次在板端运行，
本报告不把最新构建的程序身份套用到早先的硬件结果。

父代理在本轮期间完成最终只读板端检查，保存为
`/home/mzh/nickluo-validation-20261020-target-observations/root-final-service-audit.txt`。
四项原生产服务均为 active/running 且 enabled：`drone-tracker.service`、
`drone-interceptor.service`、`apm-bridge.service`、`rkaiq_3A.service`。
原 current 软链接、相机配置 SHA-256、系统 Runtime SHA-256 均与基线一致，没有隔离测试
进程残留。本轮子代理未连接板端、停止服务或修改部署。

## 最终结论

已执行的检查未发现 CRITICAL 或 WARNING，SUGGESTION 也为 0。本轮为未产生修复的
独立清洁验证，可以结束用户要求的 verify-fix 循环。自动 C++ lint/语言服务器部分因工具
缺失未验证，因此不宣称所有工具检查通过或无条件归档就绪。前述硬件、标定和图像开启
长负载边界保持明确，没有提交、推送、同步主规格、归档或切换生产版本。
