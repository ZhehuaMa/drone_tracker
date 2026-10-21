# Proposal

## Why

RK3576 当前只发布整数目标框，下游无法同时获得与该框对应的目标方向和角速度。目标框还使用处理开始时的系统时间，容易与图像采集时刻混淆；需要选择性移植 Jetson 的观测改进，同时保留生产相机现有的 HDMI 视频与 OSD（叠加状态信息）预览。

来源是 `jetson` 提交 `f176960cd6e1e98c5ea341b5fcac856b508b9865`。实施基线是 `rk3576` 提交 `7fbb0a2e006f12654a7db0f6d9d152244ec07e0d`。本变更只移植已选行为，不直接重放整个来源提交。

## What Changes

- 新增 LOS（line of sight，目标相对于相机的视线方向）及其旋转补偿角速度，输出到 `/fpv/target_los`，消息类型为 `geometry_msgs/msg/TwistStamped`。
- 成对构造目标框与 LOS，使用相同时间戳和明确的有效性标记。目标框保留浮点坐标，整数框继续供显示使用。
- **BREAKING**：新增 `publish_prediction=false`，默认以采集时间描述观测。开启时只预测到满足时序及 300 ms 新鲜度条件的最新云台姿态时间；不再使用处理开始时的系统时间作为默认预测目标。
- 新增启动参数 `los.rate_filter_tau=0.12` 秒，控制角速度平滑；零表示关闭平滑。定义重复时间抑制、重锚定复位和 OFF 失效通知。
- **BREAKING**：将 `/fpv/tracker_image` 改为 best-effort、depth 1；将 `/fpv/gimbal` 接收队列改为 best-effort、depth 10。QoS 是 ROS 的消息队列及可靠性设置，图像订阅端需要兼容 best-effort。
- 新增 best-effort、depth 100 的 `/mavros/imu/data` 订阅，只用于记录原始 IMU（惯性测量）消息，不用于替代相机姿态。
- **BREAKING**：`need_save=true` 时异步记录原有姿态、里程计和目标框，并新增 LOS 与 MAVROS IMU。所有配置下均移除 bag 中的 `/fpv/tracker_image` 数据及话题元数据；保留跟踪期间的 ROS 图像发布和独立 `recording.*` 原画面录像。
- 使用已有显式 `camera.fx/fy/cx/cy` 像素内参，供卡尔曼滤波与 LOS 共用；按实际帧尺寸裁剪，不把 Jetson 的尺寸缩放公式重复套到 RK3576。
- 更新配置、构建、数值测试、ROS 集成测试和回归证据，使重复观测抑制与浮点输出可解释。

例如，图像采集于 `10.000 s`，最新云台姿态是 `10.030 s`，处理开始于 `10.050 s`。修改前发布整数框并标为 `10.050 s`；修改后默认发布同为 `10.000 s` 的浮点框与 LOS。开启预测时，两条消息改用合格姿态的 `10.030 s`，而不是假定该姿态属于 `10.050 s`。

范围边界：不引入 `enable_preview`、ROS 原图预览线程或 ON/OFF 重开相机行为。HDMI/OSD 仍使用唯一取帧者与已有分发结构。ROS 调试图像仍仅在开启 `visualized_result` 的有效跟踪轮次发布。模型、阈值、检测仲裁及四个状态数值保持既有语义。`jetson` 分支不改写；共享结构可扩展，但本次新增 ROS 行为限定于 RK3576 入口。

## Capabilities

### New Capabilities

- `target-observations`：定义浮点目标框、旋转补偿 LOS、共同参考时刻、有效性、配置、复位与停止通知。

### Modified Capabilities

- `rk3576-ros-video`：调整观测时间、消息 QoS 与 bag 内容，保留原视频输入和取最新帧规则。
- `rk3576-ros-camera`：接入成对观测与新 QoS，明确新增观测不改变相机生命周期或曝光时间证据边界。
- `rk3576-continuous-hdmi-stream`：允许一次显式 OFF 失效通知，同时继续拒绝旧轮结果；明确调试图像不进入 bag，预览行为保留。
- `rk3576-tracking-regression`：分别记录观测参考时间和实际发布，区分两个话题、重复抑制及 OFF 控制通知，并版本化新旧证据的可比条件。

## Impact

- 公共算法：`src/hybrid_tracker/hybrid_tracker.*`、`rotation_history.h`，新增 `target_los.h` 及数值测试。
- ROS 层：`src/drone_tracker_node.*`、新增 `src/target_observation.h`、`src/bag_session.h`、`src/tests/ros_video_test.cpp` 及相关配置测试。
- 构建与配置：根目录及公共算法的 `CMakeLists.txt`、`package.xml`，三个 RK3576 参数模板与专用 camera/video launch 的配置测试。新增显式 `geometry_msgs` 依赖；launch 继续由 `params_file` 提供算法参数。
- 证据：`src/diagnostics/`、`tests/tracking_regression/` 的采集、指标、身份、格式说明与测试。不得将浮点发布框继续伪报为整数显示框。
- 下游兼容：消费者需识别角度消息字段、采集时间默认值和图像 QoS；依赖调试图像 bag 的工具需迁移到独立录像或实时订阅。
- 验证：先运行无设备数值及 ROS 测试，再运行 `./build.sh --test`。生产验收使用指定 HZ-RK3576_WuKong Board、既有模型、Runtime 2.3.2 与驱动 0.9.8；每次性能或负载运行不超过 180 秒。当前模拟内参只能支持接口与资源验收，不能证明实际角度精度。
