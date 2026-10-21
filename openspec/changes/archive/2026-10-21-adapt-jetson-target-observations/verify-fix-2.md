# 独立验证与修复：第 2 轮

本轮由新建子代理使用 `openspec-verify-change` 独立核验 `adapt-jetson-target-observations`。
客户端日期为 2026-10-20。核验范围为该工作树中的五份 delta specs、proposal、design、tasks、
当前实现、测试及保留的验收证据；不以第 1 轮报告代替源码检查。

## 初始核验

| 维度 | 结果 |
|---|---|
| 完整性 | OpenSpec 聚合任务 33/33；11 项新增及 11 项修改需求均有实现；无删除或改名需求 |
| 正确性 | 22 项需求及其 72 个场景均已定位实现与测试/实机证据；发现参数拒绝说明的警告 W1 |
| 一致性 | 八项设计选择与当前实现一致；代码模式人工检查完成 |

初始 CRITICAL：0。WARNING：1。SUGGESTION：0。

本轮另按 delta 章节重新计数，确认是 11 项 ADDED 和 11 项 MODIFIED。第 1 轮报告曾把
其中两项新增回归需求误计为修改，现已纠正该统计笔误；总计 22 项需求和 72 个场景不变，
不将这项报告记账修正计为实现问题。

### W1：运行期参数拒绝未明确说明需要重启

`src/drone_tracker_node.cpp:143` 对 `publish_prediction` 和 `los.rate_filter_tau` 的运行期修改
返回失败，但说明仅为 `RK3576 startup configuration is fixed; system clock only`。
`target-observations` 的“非法或运行期参数”场景要求同时说明需要重启；运行说明也承诺返回
重启指示。`src/tests/ros_video_test.cpp:351` 原来仅检查失败，没有检查说明。

修复建议：在现有固定启动参数拒绝说明中明确要求重启，并对这两个参数核对返回原因。
该问题只涉及错误说明，不改变参数值、数学计算、设备访问或消息时间。

## 检查定位与证据边界

- 公共几何：直接对照来源 `f176960cd6e1e98c5ea341b5fcac856b508b9865` 的
  `target_los.h`；检查 `TargetLosEstimator`、`RotationHistory`、浮点输出和三组新 C++ 测试。
  方向、独立 fx/fy、旋转补偿、平滑、300 ms 边界、非法旋转与重锚定复位均有受控用例。
- 消息与时间：检查 `TargetObservationBuilder`、`processFrames()`、两种新轮启动路径和
  `notifyObservationOff()`；消息字段、独立有效性、时间选择、先复位后重复守卫、停止边界
  与图像/状态独立性均有对应实现。Jetson 默认倒序姿态策略仍为忽略，RK3576 显式选择复位。
- ROS 接口与记录：检查实际 QoS、仅记录的 MAVROS 回调、三个参数模板、geometry_msgs
  依赖及 `BagSession` 的两条额度/合计字节检查。现有测试覆盖五话题回读、无图像元数据、
  seal 后拒绝、第二条写盘失败、容量不足和健康预览/录像隔离。
- 持续相机：检查唯一 dispatcher、控制版本及待执行任务边界，核对阻塞 YOLO/NanoTrack、
  ON/OFF、启动失败和预热用例。预览不扩展为 ROS 图像，原顺序 replay 路径没有改写。
- 回归证据：检查版本 1/2 存储和 collector、double 位复制、两话题独立调用、控制身份与
  `observation_issues()`。七项专门观测测试在本轮重新通过；控制通知不进入帧级统计。
  比较身份包含协议、时间模式、tau、图像 QoS 和 bag 策略，历史缺失字段仍为 unavailable。
- 生产证据：读取保留的六次固定输入汇总、相机开关矩阵与停止专用复核、最终服务恢复及
  测试进程清理记录。六次固定输入没有有效目标，不能用于证明物理角度精度。生产默认
  关闭图像的单次 124.840 秒运行覆盖 21 轮及 60 秒联合负载；开启图像的历史时间戳失败
  仍保留，不改称通过。

上述覆盖包含 target-observations 的 22 个场景、ros-video 的 16 个场景、ros-camera 的
17 个场景、continuous-hdmi-stream 的 6 个场景和 tracking-regression 的 11 个场景。

下表的 `node` 指 `src/drone_tracker_node.cpp`，`ros-test` 指
`src/tests/ros_video_test.cpp`，其余路径均相对仓库根目录。行号按本轮最终源码记录。

| 需求 | 具体实现与场景检查 |
|---|---|
| 目标框与视线消息具有共同身份 | `src/target_observation.h:26`、`node:1414`；消息构造与独立发布失败测试 |
| 框的浮点精度和有效性明确 | `src/hybrid_tracker/hybrid_tracker.cpp:866`；`observation_output_tests` 与消息无姿态测试 |
| 角速度补偿相机旋转并按时间平滑 | `src/hybrid_tracker/target_los.h:36`；`target_los_tests` 的几何、复合旋转和间隙边界 |
| 参考时刻与相机姿态一致 | `src/hybrid_tracker/rotation_history.h:78`、`node:1176`；参考时间数值用例及六次板端日志 |
| 重复抑制与速度复位各自生效 | `src/target_observation.h:26`、`node:1269`；重复重锚定、新轮较早时间及 `ros-test:765` |
| 姿态重复和回退不制造倒序观测 | `src/hybrid_tracker/rotation_history.h:30`、`node:1493`；新旧回退策略及 builder 时间守卫用例 |
| OFF 发送一次明确的失效观测 | `node:231`、`node:1476`；`observationEndpoints()`、阻塞推理和连续停止用例 |
| 观测启动配置与实际像素一致 | `node:137`、`node:691`；`configuration()`、`observationTemplates()`，本轮 W1 修复 |
| ROS 消息队列和可靠性按观测用途配置 | `node:916`、`node:934`；实际端点 QoS 及 MAVROS-only 测试 |
| 时间与取最新帧保持一致 | `node:1106`、`node:1176`；原视频源的最新帧/停止等待和默认采集时间测试 |
| ROS 接口和结果有效性保持兼容 | `node:1213`、`node:1295`、`node:1376`；消息、状态重启、独立图像和未 ON 测试 |
| ROS bag 记录与关闭互斥 | `src/bag_session.h:70`、`:115`、`:150`；配对接纳、回读、容量及打开/写入/关闭故障测试 |
| ON 和 OFF 控制完整运行轮次 | `node:166`、`node:427`；相机启动/预热/失败/录像/重复启停用例及 21 轮实机证据 |
| 图像与预测消息保持原有语义 | `node:1274`、`node:1376`；两时间模式、图像开关及缺姿态独立显示测试 |
| 新轮任务和输出与旧轮隔离 | `node:231`、`node:427`；`continuousCameraLifecycle()`、`continuousBlockedNanoAndTelemetry()` |
| 记录与 ROS 调试图像保持业务边界 | `node:534`、`node:1387`、`src/bag_session.h:158`；无图像 bag、独立录像及四组实机开关 |
| 测试入口与生产契约隔离 | `tests/tracking_regression/main.cpp` 调用生产节点；既有 source/round/fixture 检查，replay 源码无差异 |
| 实际发布与预测时间分别统计 | `node:1455`、`tests/tracking_regression/metrics.py:453`；发布间隔、帧年龄、参考落后量及抑制计数测试 |
| 不同版本先检查可比条件 | `tests/tracking_regression/identity.py:75`；既有模型/区间/参数不同比较与历史组不匹配测试 |
| Version comparisons preserve measurement compatibility | `tests/tracking_regression/collect.py:26`、`:46`；旧格式可读、未知格式拒绝与 unavailable 测试 |
| 成对观测证据可按话题重算 | `node:1414`、`tests/tracking_regression/metrics.py:170`；double 往返、独立有效性与缺半对反例 |
| OFF 通知与帧结果分开统计 | `node:1476`、`tests/tracking_regression/metrics.py:492`；完整控制通知和帧 FPS 不加倍测试 |

八项设计选择逐项检查：公共数学边界、来源几何、参考时间、成对构造、停止顺序、异步
记录、显式像素配置及版本化诊断。没有缺失或不可读的必要规划工件。

自动 C++ lint 和语言服务器检查未验证：环境没有可用或已配置的 clang-tidy/clangd。
完整 Jetson SDK/硬件运行、真实标定/实飞精度和开启 debug 图像的 60 秒稳定性不在已验证
证据内。生产板实测约 7.9 GiB，不能外推为 3.8 GiB 机型的内存验收。保留实机程序早于
第 1 轮修复；本轮不得用宿主新编译身份冒充其原始程序身份。

## 修复与复验

W1 已修复：固定启动参数的拒绝原因明确加入 `restart the node to apply it`，两项新参数的
ROS 配置测试同时检查失败结果及 `restart` 提示。没有改变实际参数策略或 RK3576 的观测
语义。第 1 轮报告的需求分类笔误也已更正，原始实机数据和历史基线保持原样。

本轮检查结果：

- 默认 `./build.sh --test --jobs 12` 成功交叉编译 ARM64，60/60 CTest 通过，套件耗时
  164.25 秒。镜像为 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。
  完整日志保存在 `/home/mzh/nickluo-validation-20261020-target-observations/verify-fix-2-build.log`。
- 必需的原生操作系统内存限制检查通过。Python 分组为 86 项原生用例及 1 项真实 ARM
  中间件用例，全部通过；没有跳过。新的重启原因断言随 `ros_video_tests` 通过，既有
  持续相机、Jetson 配置、观测数学和诊断检查也全部通过。
- 在 conda base Python 中单独重新运行七项 `test_observations.py` 用例，通过。对六份
  保留板端 `events.jsonl` 重新执行当前 `observation_issues()`，每份问题数均为零；每轮
  900 对帧观测，capture 模式采用姿态参考数为零，pose 模式依次为 10、11、11。
- `openspec validate adapt-jetson-target-observations --strict` 和 `git diff --check` 通过；
  最终 apply 聚合仍为 33/33，没有不可读的任务文件。

## 最终计分与结论

| 维度 | 修复后结果 |
|---|---|
| 完整性 | 33/33 任务；11 ADDED + 11 MODIFIED = 22/22 需求有实现；72 个场景均已核对；删除/改名检查不适用 |
| 正确性 | 22/22 需求映射与 72/72 场景有对应实现及受控测试/保留实机证据；W1 已修复，ROS 配置测试通过 |
| 一致性 | 8/8 设计选择一致；代码模式人工核对完成；自动 C++ lint/语言服务器检查未验证：环境缺少可用或已配置工具 |

最终 CRITICAL：0。WARNING：0。SUGGESTION：0。初始唯一 WARNING 已修复。

已执行的检查未发现严重或警告问题。代码模式检查中的自动 C++ lint 和语言服务器部分
未验证，原因是没有可用或已配置的 clang-tidy/clangd；不将这项缺口记为通过，也不据此
声称无条件归档就绪。完整 Jetson 硬件、真实标定/实飞、debug 图像开启的长时间稳定性
和 3.8 GiB 机型仍保留前述边界。

本轮没有连接板端、停止服务或修改生产部署/配置/Runtime。已读取的最终恢复证据显示
原四项服务 active/running 且 enabled，生产配置和 Runtime 指纹未变，没有隔离测试进程
残留。原实机程序身份保留；本轮宿主产物未再运行于板端。没有提交、推送、同步主规格
或归档。由于本轮初始存在 WARNING，下一轮独立 verify 必须由另一个新建子代理执行。
