# adapt-jetson-target-observations 实施与验收记录

## 范围与证据规则

本记录对应工作树 `adapt-jetson-target-observations`，基线提交为
`7fbb0a2e006f12654a7db0f6d9d152244ec07e0d`。选择性移植来源为
`f176960cd6e1e98c5ea341b5fcac856b508b9865`。未改写 `jetson` 分支，未提交或部署生产版本。

LOS 是目标相对于相机的视线方向。KF 是用于估计并预测框的卡尔曼滤波器。
QoS 是 ROS 端点的可靠性和队列深度。下列检查分别报告可移植算法、容器内 ARM64
指令模拟和实际板卡证据；前两者不能替代 NPU、相机和 HDMI 的实机验收。

宿主证据根目录为 `/home/mzh/nickluo-validation-20261020-target-observations/apply/`。
板端隔离目录为 `/home/hzhy/ajo20/`。原始失败尝试与后续修复保留在这些目录中，不能把
单次失败尝试改称通过。客户端记录日期为 2026-10-20；板端系统时间明显早于客户端日期，
本次未修改板端时钟。原始日志保留其实际时间，运行时长使用单调时钟计算。

## 实现及需求映射

| 规格需求 | 实现入口 | 检查与证据 |
|---|---|---|
| 共同身份、浮点框与独立有效性 | `src/target_observation.h`、`TrackerOutput.observation_box` | `target_observation_tests`、`observation_output_tests`、ROS 配对检查 |
| 相机自转补偿、方向符号、平滑和长间隙 | `src/hybrid_tracker/target_los.h` | `target_los_tests`：复合旋转、运动、非等比焦距、tau=0、300 ms 边界和非法旋转 |
| 采集默认、合格姿态预测及回退 | `RotationHistory::reference()`、`processFrames()` | 数值边界 300000/300001 us、未来/早于帧/空历史；两种模式的固定输入实机回归 |
| 重复抑制、重锚定先复位、新轮时间基准 | `TargetObservationBuilder`、两个轮次启动路径 | 重复重锚定、丢失/恢复和较早新轮构造测试；重复参考时保留 8 张图像及状态 |
| 姿态重复、回退代次与发布时间守卫 | `RotationHistory`、节点姿态代次快照 | 相同时间去重、倒序清空历史；构造器守卫独立测试 |
| 一次 OFF 失效通知与旧结果隔离 | 控制锁、`notifyObservationOff()` | 普通与持续模式的阻塞推理/启停测试；重复 OFF 与首次 ON 前 OFF 检查 |
| 固定参数、实际像素和启动模板 | 节点参数保护、三个 RK3576 YAML | 非法 tau、运行期拒绝、三个模板加载、独立 fx/fy、真实帧尺寸及 Jetson 配置编译检查 |
| 各端点 QoS、MAVROS 不代替相机旋转 | RK3576 publisher/subscriber 初始化 | 检查实际端点 QoS；只有 MAVROS IMU 时有效框与无效 LOS 并存 |
| 视频取最新帧、图像时间和消息语义 | 原帧源、更新上下文和独立图像分支 | 原视频/相机/预览/回放回归测试；图像保留采集时间，不增加 ROS 预览入口 |
| ROS bag 关闭互斥和消息成对接纳 | `BagSession`、bag 生命周期锁 | 五话题重新读取、原始时间/载荷、256 条与 64 MiB 上限、仅余一位置、字节不足、seal 后拒绝 |
| bag 故障隔离和图像移除 | bag writer 独占线程、节点记录路径 | 打开/写入/关闭/溢出失败；第二条写盘失败保留 failed；图像开启也无图像元数据 |
| 持续模式旧轮隔离和预览边界 | 单一 dispatcher、原显示控制版本 | `ros_continuous_camera_tests`、生产相机启停记录；独立录像继续使用原控制边界 |
| 实际发布和参考时间分别统计 | 新观测/LOS 调用事件、collector/metrics | double 位复制往返、双话题同 update、重复抑制、独立失败、旧日志 unavailable |
| 版本比较与历史指标兼容 | 事件版本 1/2 reader、观测比较身份 | 未知版本拒绝、旧记录解析、模型/参数/协议不同比较；旧 baseline 未改写 |
| OFF 控制事件独立统计 | Control 生产者与控制发布事件 | 控制通知不伪造帧身份，不进入帧有效率/吞吐分母；dispatcher 使用新增独立生产者 |

## 容器及无设备检查

默认入口为 `./build.sh --test --jobs 12 --artifacts-dir <证据根>/build`。
后端为 amd64 原生工具交叉编译 ARM64，镜像 ID 为
`sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。
工具链为 `/opt/toolchains/aarch64-humble.cmake`，ARM64 程序由固定 QEMU 8.2.2 执行。

完整 60 项套件及必需的原生操作系统内存限制检查已通过。60 项套件耗时 164.77 秒；
其中包含真实 KF 输出、模板加载和重复参考场景。最终结果见
`build-final-60.log` 及 `build/backends/amd64/log/`。
Python SDK 分组运行 84 个不同用例，83 个原生用例和 1 个真实 ARM 消息库加载用例均通过。

额外使用原生 x86_64 镜像单独配置 `src/hybrid_tracker`，设置
`DRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON`。11/11 检查通过，证明公共算法测试不要求 ROS
或硬件 SDK。日志为 `native.log`。新数值测试的首版错误地要求整数 KF 初始化立即产生
小数宽度；修正为先执行真实浮点测量校正，再检查输出精度。未修改算法来满足这一断言。

环境未提供项目配置的 clang-tidy/clangd，因此未执行这两项工具检查。Jetson 配置策略
编译检查通过；完整 Jetson CUDA、TensorRT、VPI 和硬件运行未验证。

## 指定 adb 板卡和生产边界

实际设备为 adb 序列号 `5ceb49407502303d`，主机名 `hzhy`，机型
`HZ-RK3576_WuKong Board`，machine-id 为 `1fe0a397f2ae4343896c7cf9d253a608`，
内核为 `6.1.141-rt36`。本次设备实际可见内存约 7.9 GiB，与仓库说明的 3.8 GiB 不同；
不能将本记录的内存结果外推为 3.8 GiB 机型通过。

NPU 驱动为 0.9.8，系统 RKNN Runtime 为 2.3.2。系统库 SHA-256 为
`d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。
既有模型为气球 YOLO 和 FP16 NanoTrack Template Backbone、Search Backbone、Head。
完整路径及模型指纹保存在 `board/models.txt`，回归 identity 也记录模型内容。
没有重新转换模型，没有覆盖系统 Runtime。

生产服务基线为 `drone-tracker.service`、`drone-interceptor.service`、`apm-bridge.service`、
`rkaiq_3A.service` 均 active/running 且 enabled。前三项在隔离验收期间暂时停止，rkaiq 保持运行。
每次板端验收由 EXIT/INT/TERM 恢复处理保护；失败时也启动原服务并记录恢复状态。
恢复命令与原始基线保存在 `board/restore-services.sh`、`board/baseline.txt`。

原生产 current 为 `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`。
原相机配置 SHA-256 为 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`。
本次没有修改生产软链接、配置、模型或服务文件。测试二进制、配置和录像均放在隔离目录。
模拟相机内参与模拟姿态只支持接口和资源检查，不能证明物理角度精度、目标准确率或实飞质量。

## 实机检查进度

固定输入使用既有 `wlcb-104206-020-035`，900 帧、1920×1200、15 秒，像素缓存逐帧校验。
原始 fixture 与历史 baseline 均未改写；两种观测模式各自建立新的算法配置副本和独立参考。
单次外部总预算为 115 秒，包含准备、15 秒回放及清理，低于 180 秒上限。

采集时间模式三次运行均 qualified，独立 reference 已保存于
`/home/hzhy/ajo20/regression2/capture/baseline.json/baseline.json`。姿态时间模式同样 3/3 qualified，
独立参考位于 `/home/hzhy/ajo20/regression2/pose/baseline.json/baseline.json`。六次运行均无
资格拒绝原因，原始输入像素保持不变。生产相机矩阵结果将在服务恢复并复核后补入下文。

固定输入的六次完整结果：

| 模式与轮次 | 外部总时长（秒） | 帧 bbox / LOS 调用 | 合格姿态参考数 | 有效 bbox / LOS |
|---|---:|---:|---:|---:|
| capture 1 | 73.266 | 900 / 900 | 0 | 0 / 0 |
| capture 2 | 73.542 | 900 / 900 | 0 | 0 / 0 |
| capture 3 | 73.150 | 900 / 900 | 0 | 0 / 0 |
| publish_prediction=true 1 | 71.377 | 900 / 900 | 10 | 0 / 0 |
| publish_prediction=true 2 | 72.331 | 900 / 900 | 11 | 0 / 0 |
| publish_prediction=true 3 | 71.564 | 900 / 900 | 11 | 0 / 0 |

这段固定输入在所用气球模型及阈值下没有有效目标；不能把 qualified 解释为目标准确率或
真实有效 LOS 精度通过。qualified 表示输入、生命周期、诊断与清理证据完整。
预测开启时，其余帧按要求回退采集参考。每轮另外存在一次控制失效通知，表中帧统计已排除它。
有效目标几何、复合旋转和小数精度由受控数值及真实公共跟踪器测试覆盖。

宿主保存的回归汇总为 `board/preserved/regression-summary.json`，完整原始事件、配置、
identity、完成与截止预算记录已从板端复制至 `board/preserved/regression2/`。
输入 sidecar 保存在 `board/preserved/fixtures2/`；大体积 clip 保留在原板端位置并由指纹关联。

## 保留的失败尝试

- 第一次板端回归在加载模型前因 PATH 缺少 ffprobe 失败。之后使用既有隔离 FFmpeg 工具，
  未安装系统包。外部 harness 曾因重复创建运行汇总文件失败，修正为仅更新自身汇总并继续
  保留已经 qualified 的运行目录。新增指标版本也暴露了 baseline reader 的版本表遗漏，
  已修复并完成两组独立参考冻结；未改写历史 baseline。
- 初始相机 harness 因板端缺少 `vision_msgs` Python 绑定未启动采集。随后只在
  `/home/hzhy/ajo20/python/` 放入匹配 SDK 的真实 ARM Python 包及 generator_py 动态库，
  测试端类型支持加载通过。生产环境与系统 Python 安装未修改。
- `endurance-v2` 的测试配置错误地令 `recording.min_free_bytes=0`，因此独立录像被原有
  参数保护拒绝。测试端还因回调消费过慢形成接收积压。该尝试不计为录像或停止预览验收通过。
  后续采用正的磁盘空余下限，并在保持 50 Hz 遥测输入的同时连续处理 ROS 回调。
- `endurance-v3` 使用有效录像配置、debug 图像开启，在约 34 秒、15 次完成 OFF 后遇到
  真实采集故障：驱动序列 2051 的时间戳被严格检查报告为重复或倒退。节点退出码为 1。
  该失败完整保留，未放宽相机时间契约，也未伪造递增时间。原服务随后恢复。
  后续 endurance 使用生产默认 `visualized_result=false`；独立矩阵仍分别覆盖图像开关。

## 最终生产相机与开关矩阵

`endurance-v4` 使用生产默认的 `visualized_result=false`，在单次 124.840 秒运行中完成：

- 首次 ON 前 10 秒预览，20 次短 ON/OFF，再进行第 21 轮至少 60 秒真实跟踪、独立录像和 HDMI 负载。
- 最终 OFF 后 10 秒预览。相机服务只启动一次，没有因切轮重开；节点最终退出码为 0。
- 收到 4818 条 bbox 与 4818 条 LOS，接收配对的 header 时间一致。ROS 图像为 0。
- 21 个 bag 和 21 个独立录像 session 可核对。bag 均有五个规定话题，没有图像元数据或数据；
  接受与写入数量相同，记录丢弃为 0。原始 MAVROS IMU 的识别字段和 header 时间保持不变。
- 逐条显式 OFF 到首次无框预览的同板日志差值最大 42.751 ms；对应显示器内部稳态时钟计时也保留。
  这些时延小于 500 ms。此处没有将稍后发生的目标状态变化误作为 OFF 延迟。

四个开关组合均使用 12 秒计划 ON 负载、同样的 50 Hz 测试遥测、同一相机和模型。
下表的 bbox/LOS 为接收端实际计数，不能推断全部发布调用都被接收。完整运行时长采用单调时钟。

| need_save | visualized_result | 总时长（秒） | 接收 bbox / LOS | 接收图像 | bag / 独立录像 | OFF 清框最大值（ms） |
|---|---|---:|---:|---:|---:|---:|
| false | false | 16.575 | 721 / 721 | 0 | 0 / 1 | 23.851 |
| false | true | 16.574 | 289 / 289 | 35 | 0 / 1 | 49.628 |
| true | false | 18.013 | 721 / 721 | 0 | 1 / 1 | 31.397 |
| true | true | 17.779 | 280 / 280 | 21 | 1 / 1 | 16.099 |

矩阵所有节点退出码为 0，相机各启动一次，OFF 排空后没有 ROS 图像或正常观测继续到达。
bag 开启的两组重新读取成功；五话题载荷、header 与存储时间、配对及计数检查通过。
图像开启也没有图像 bag 话题。独立录像不依赖 need_save，四组均有录像 session。
所有性能运行均低于 180 秒，未改变模型、阈值、生产部署、Runtime 或驱动。

原始矩阵 harness 对两组图像开启情形返回非零，因为它错误地将所有
`control_to_display_us` 状态记录取最大值。目标在 ON 后约 1 秒进入 Found/Tracking 时，
该字段仍相对较早的控制版本计时；这不是 OFF 清框延迟。原始非零日志和 summary 原封保留。
修正后的只读评估器 `reassess_camera.py` 从每条 `Tracking OFF accepted` 定位其 idle 控制版本，
再查找该版本或后继版本的首次无框显示。它检查全部显式 OFF 均不超过 500 ms，并保存匹配原始日志行。
`camera-reassessment.json` 保存这一复核、原始文件 SHA-256 与计时口径；没有通过删除失败记录取得结论。

跨版本 supersede 的 OFF 总延迟采用同一板端节点时钟日志差值；显示事件自带的
`control_to_display_us` 是独立的 steady-clock 计时。两者均保存在证据中。运行时长和帧率窗口使用
单调时钟，未将客户端日期与板端时钟混合。没有进行板端校时或用改写时间绕过采集检查。

节点原始性能窗口包含 ON 及其收尾/预览，不能冒充纯 ON 内的帧率。窗口实际值如下：

| need_save / 图像 | 监测窗口（秒） | 消费帧 | HDMI 显示 | 消费帧/监测窗口（Hz） |
|---|---:|---:|---:|---:|
| false / false | 13.832078 | 720 | 821 | 52.053 |
| false / true | 13.826426 | 288 | 390 | 20.830 |
| true / false | 13.829792 | 720 | 822 | 52.062 |
| true / true | 13.830681 | 280 | 385 | 20.245 |

这组短期对照未设置性能通过阈值。开启 debug 图像时消费量明显下降；不能将它归因于 bag，
也不能据此宣称性能达标或改进。60 秒长负载采用生产默认关闭图像。
`endurance-v3` 的真实采集时间失败仍未归因，因此不宣称 debug 图像开启下已通过 60 秒稳定性验收。

## 最终恢复与证据位置

板端服务在所有运行完成后已恢复为原来的 active/running、enabled 状态。`final-restoration.txt`
再次核对原 ExecStart、current 软链接、生产配置指纹、Runtime 指纹和驱动 0.9.8，均与基线一致。
`final-process-cleanup.json` 确认没有隔离目录下的测试节点、worker 或验收脚本残留。

后续核查不必重复硬件测试：

- 宿主 `board/preserved/camera/`：成功及失败尝试的配置、节点日志、接收事件、summary，
  以及录像和 bag 的小型元数据。真实时间戳失败保留在 `endurance-v3/node.log`。
- 宿主 `board/preserved/camera-reassessment.json`：四组矩阵及 endurance 的只读复核。
- 宿主 `board/final-restoration.txt`、`board/final-process-cleanup.json`：最后恢复和进程清理。
- 宿主 `board/preserved/raw-camera-files.sha256`：仍位于板端隔离目录的 `.ts` 和 `.db3` 原始文件指纹。
- 板端 `/home/hzhy/ajo20/camera/`：原始 bag 与录像。未将它们或模型二进制加入 Git。

本记录说明任务实施及验收证据。用户要求的独立 OpenSpec verify/fix 循环仍由新的子代理执行，
本次不归档、不同步主规格、不提交、不推送，也不切换生产版本。

## 独立验证第 1 轮

独立子代理按完整的 OpenSpec verify 流程核对 33 项任务、22 项需求和 72 个场景。
初始发现 0 项 CRITICAL、2 项 WARNING。已恢复 Jetson 原有姿态回退边界，并加强版本 2
观测证据的两份载荷、两个话题调用的完整性校验。修复后 60/60 ARM64 套件、必需原生内存
限制检查、11/11 公共算法原生检查均通过。七项观测证据测试通过；保存的六份板端事件也通过
加强后的只读校验。剩余 CRITICAL 和 WARNING 均为 0。

详情、初始发现、全部需求映射及未验证工具边界见 [verify-fix-1.md](verify-fix-1.md)。
原始实机程序身份保持原记录；本轮宿主复编译产物未再运行于板端，不能把其新指纹冒充原实机
验收身份。本轮没有操作板端或生产服务。用户要求的后续独立验证由新子代理继续执行。

## 独立验证第 2 轮

新子代理独立核对 33 项任务、22 项需求及 72 个场景，初始发现 0 项 CRITICAL、1 项
WARNING：运行期拒绝修改两项观测参数时，没有明确要求重启。现已补充重启说明并增加
ROS 配置断言；默认 ARM64 构建、60/60 CTest、必需原生内存限制检查及 87 项 Python
用例均通过。最终 CRITICAL 和 WARNING 均为 0。自动 C++ lint/clangd 仍因工具缺失
未验证；本轮未连接板端或操作生产服务。

第 2 轮同时纠正第 1 轮报告的需求分类笔误：实际为 11 ADDED、11 MODIFIED，总数不变。
详情见 [verify-fix-2.md](verify-fix-2.md)。本轮有修复，后续仍由新的子代理独立验证；
没有同步、归档或部署。

## 独立验证第 3 轮

新建子代理重新核对五份规格、33 项任务、22 项需求、72 个场景、八项设计选择及当前源码。
初始和最终均为 CRITICAL 0、WARNING 0、SUGGESTION 0；本轮无需代码修复，verify-fix
循环取得独立清洁结果。最新代码的第 2 轮 60/60 构建结果保持适用，本轮未重复负载测试。
自动 C++ lint/语言服务器仍因工具缺失未验证，实机程序与最新宿主产物的身份边界保持不变。

详情见 [verify-fix-3.md](verify-fix-3.md)。最终只读服务审计确认四项原服务均已恢复，生产
软链接及配置/Runtime 指纹未变，没有隔离测试进程残留。未提交、推送、同步、归档或部署。
