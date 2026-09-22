# Proposal

## Why

生产开发板计划出货时使用型号为 SC233HGS 的相机模组，但当前 RK3576 平台唯一实现的相机采集能力（`rk3576-camera-frame-source`）专门适配的是仅用于开发板 1、2 的 AR0234 测试相机。AR0234 和 SC233HGS 是完全不同的硬件：AR0234 由厂商私有驱动直接给出 8 位灰度像素，并提供一组私有的软件触发控制项；SC233HGS 使用 RK3576 官方内置驱动，原始像素是 10 位，且这颗相机的驱动**不提供任何软件触发命令**，只能连续不断地出帧。如果不新增专门支持 SC233HGS 的采集实现，生产开发板将无法从相机取得图像，现有的目标检测与跟踪算法也就无法在生产板上运行。同时，开发板 1、2 仍需继续使用 AR0234，因此本次改动必须在新增 SC233HGS 支持的同时，完整保留 AR0234 现有实现的行为。

## What Changes

- 在公共的相机采集配置结构体（`FrameSourceConfig`）中新增一个"传感器型号"选择字段，取值为 `ar0234` 或 `sc233hgs`，默认值为 `ar0234`，用于告诉 RK3576 相机创建入口应当构造哪一种具体的采集实现。系统 SHALL 按该字段显式派发，不做自动探测，不在型号不匹配或缺失依赖时回退到另一型号。
- 新增一套独立的 SC233HGS 采集实现：直接打开生产板系统镜像预先建立好的、已经过 ISP（图像信号处理器，负责自动曝光、自动白平衡等图像预处理）3A（自动曝光、自动白平衡、自动对焦的统称，这里特指前两项）处理链路的采集节点，协商为 NV12（一种由亮度 Y 平面加交织色度 UV 平面组成的图像格式）格式，只提取其中的 Y（亮度）平面作为最终的 8 位灰度（Gray8）输出，交给下游检测/跟踪算法。这套实现不会向传感器发送任何软件触发命令，也不做 AR0234 那种传感器寄存器级别的格式协商。
- 从现有 AR0234 采集实现（`Ar0234CameraDevice`）中抽取一个与具体传感器无关的通用基类，封装两种相机共用的 V4L2（Linux 视频采集标准接口）缓冲区管理逻辑：内存映射、缓冲区申请、出队轮询、停止时唤醒等待。AR0234 专属的触发控制项读写、传感器格式协商字符串、按字节拷贝 Gray8 像素的逻辑，保留在原有类中作为可替换的专属策略，对 AR0234 的外部行为不做任何改变。
- 新增 ROS 生产节点参数 `mv_camera.sensor_model`，用来在启动配置里选择使用哪种相机。当该参数取值为 `sc233hgs` 时，`capture_mode` 只允许配置为 `continuous`（连续出帧）；如果配置为 `software_trigger`（软件触发），节点 SHALL 在启动阶段明确拒绝，并说明该型号不支持软件触发，而不是启动后静默按连续模式运行。
- 更新 `parameters/rk3576_camera_params.yaml`，为 SC233HGS 增加参数示例与注释，说明其视频采集节点路径需要在生产板上执行一次 `media-ctl -p`（一个用于查询/配置 Linux 媒体设备拓扑的命令行工具）核实后再填入，不支持运行时自动发现。

本次改动默认值保持 `ar0234`，因此开发板 1、2 现有的启动参数文件无需任何修改即可继续工作，属于新增能力而非破坏性变更。

## Capabilities

### New Capabilities
- `rk3576-sc233hgs-camera-frame-source`：描述 SC233HGS 相机采集实现的可观察行为契约——仅支持连续出帧、拒绝任何触发相关配置、从 NV12 缓冲区提取 Y 平面作为 Gray8 输出、要求显式配置采集节点路径（不做运行时拓扑自动发现）、假定 ISP/3A 链路已由外部系统就绪、以及对应的时间戳与队列语义。

### Modified Capabilities
- `platform-factory`：`FrameSourceConfig` 新增传感器型号选择字段；RK3576 相机创建入口按显式型号在 AR0234 与 SC233HGS 两种具体实现之间派发，不自动探测、不跨型号回退。
- `rk3576-ros-camera`：新增 `mv_camera.sensor_model` 参数，并新增该参数与 `capture_mode` 之间的兼容性校验——`sc233hgs` 型号仅接受 `continuous`，配置 `software_trigger` 时启动 SHALL 明确失败。

## Impact

- **代码**：`src/platform/rk3576/`（`ar0234_camera_device.*`、`ar0234_camera_frame_source.*`、`platform_factory.cpp`、`production_factory.cpp`、`backend.cmake`、`ar0234_camera_standalone.cmake` 等）需要重构出通用 V4L2 基类并新增 SC233HGS 专属的采集设备与帧源实现文件；`src/platform/platform_factory.h` 的 `FrameSourceConfig` 新增字段；`src/drone_tracker_node.h`/`.cpp` 新增 `mv_camera.sensor_model` 参数声明与校验；`parameters/rk3576_camera_params.yaml` 增加配置示例。
- **依赖**：假定生产板系统镜像已经准备好可用的 ISP 采集节点、3A 守护进程和调校（IQ）文件，drone_tracker 自身不负责启动或检查这些依赖，只负责打开已经工作正常的采集节点并读取像素；不引入新的编译期 SDK 依赖，继续复用现有 V4L2 与 `media-ctl` 调用方式。
- **不受影响**：AR0234 相机在开发板 1、2 上的现有采集行为、ROS 话题名称与消息语义、跟踪状态编码、`jetson` 分支均不受本次改动影响。
- **遗留问题（本次不解决）**：SC233HGS 在生产板上实际的采集节点路径、NV12 缓冲区行跨度对齐规则，以及 ISP 冷启动收敛期是否会影响现有"开流后 2 秒无首帧即超时失败"的约束，均需要后续实机验证，本次变更范围不包含这些实机验收任务。
