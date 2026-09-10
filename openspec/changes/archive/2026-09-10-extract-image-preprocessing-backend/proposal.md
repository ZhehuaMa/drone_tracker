## Why

当前 YOLO 检测和 NanoTrack 跟踪的模型输入准备逻辑直接依赖 Jetson 的 VPI、CUDA 和 TensorRT 缓冲区，后续接入 RK3576 时无法只替换平台实现，必须同时修改检测、跟踪和设备资源管理代码。现在通用 `Frame` 边界已经建立，下一步需要把“如何把一帧图像转换为模型输入”收敛为稳定契约，并先用现有 Jetson 逻辑实现该契约。

例如，改动前 YOLO 工作线程自行复制 `VpiFrame`，再为 ROI、全图和切片分别创建 VPI 视图，最后直接写入 `TensorBuffer::device_ptr`；改动后工作线程只负责选择 ROI、全图或切片，Jetson 预处理会话负责一次性隔离源帧、准备对应区域，并返回带 letterbox 坐标变换的受生命周期保护模型输入。letterbox 是保持宽高比缩放后使用固定像素补齐模型尺寸的处理。

## What Changes

- 新增平台无关的模型输入描述，明确张量形状、数据类型、布局和数值变换规则，不暴露 `VPIImage`、CUDA 指针、`nvinfer1::Dims` 或其他硬件 SDK 类型。
- 分别新增 YOLO 和 NanoTrack 预处理契约。YOLO 契约支持同一源帧的一次隔离和多区域顺序处理；NanoTrack 契约区分模板输入、搜索输入和目标重置。
- 将 letterbox 尺寸与偏移计算、NanoTrack 搜索窗几何计算提取为平台无关的纯计算逻辑，并固定现有舍入、填充和坐标语义。
- 新增 Jetson 预处理实现，把现有 YOLO 和 NanoTrack 中的 VPI/CUDA 裁剪、缩放、边界填充、格式转换、CHW 排列和输入缓冲区写入迁移到平台边界内。
- 新增受生命周期保护的模型输入对象，并增加必要的 TensorRT 输入适配，使预处理结果能够安全交给现有推理执行路径，而公共预处理接口无需识别 Jetson 缓冲区。
- 增加经确认的内部模型调用测试接入点，让无硬件测试运行真实检测级联和跟踪状态逻辑；生产装配仍调用现有 TensorRT 引擎。
- 通过构造注入让 YOLO 和 NanoTrack 使用预处理契约；当前装配只创建 Jetson 实现，不增加运行时平台选择。
- 保留 YOLO 的 ROI、全图、切片级联，保留 YOLO 解码与 NMS，并保留 NanoTrack 的状态更新、锚点计算和边界框计算。NMS 是用于去除高度重叠重复检测框的非极大值抑制步骤。
- 不实现 RK3576、RGA 或 RKNN 预处理，不改变相机采集与解码，不改变 ROS 话题、参数、`TrackerState` 数值或跟踪状态转移。
- 本 change 不安排 Jetson 平台的编译、测试或运行任务。

## Capabilities

### New Capabilities

- `model-input-preprocessing`: 定义从通用帧准备 YOLO 和 NanoTrack 模型输入所需的平台无关契约、数值语义、生命周期、错误处理和现有 Jetson 实现兼容要求。

### Modified Capabilities

无。

## Impact

- 主要影响 `src/hybrid_tracker/yolo_detector.*`、`src/hybrid_tracker/nano_tracker.*`、`src/hybrid_tracker/trt_engine.*` 和 `src/hybrid_tracker/CMakeLists.txt`，并新增预处理公共契约、纯几何逻辑及 Jetson 实现文件。
- `YoloV8Detector` 和 `NanoTracker` 的内部构造方式会增加预处理依赖注入，但 `HybridTracker` 的公开输入、ROS 节点调用方式和外部消息协议保持不变。
- 当前构建仍然使用 Jetson 的 VPI、CUDA 和 TensorRT 依赖。本 change 不建立 RK3576 构建路径，也不宣称项目已经能够在 RK3576 上编译或运行。
- 验证范围限定为平台无关几何单元测试、模型输入契约测试、公共头文件依赖扫描、资源所有权审查和 OpenSpec 校验；不包含 Jetson 平台的编译、测试或运行任务。
