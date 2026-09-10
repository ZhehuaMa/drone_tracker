## 1. 建立平台无关预处理契约

- [x] 1.1 公共代码能够完整描述模型输入：新增 `src/hybrid_tracker/preprocess/model_input.h`，定义 `TensorElementType`、`TensorLayout`、`TensorShape`、`ModelInputContract` 和只读 `PreparedModelInput`；完成条件是这些声明不包含平台设备地址或硬件 SDK 类型。
- [x] 1.2 调用方能够区分所有规定的失败原因：在 `src/hybrid_tracker/preprocess/model_input.h` 定义预处理错误码和结果结构，覆盖空帧、不支持格式、无效区域、契约不匹配、平台类型不匹配、输入占用和平台处理失败，并通过枚举覆盖检查确认每种错误都有稳定名称。
- [x] 1.3 YOLO 和 NanoTrack 具有独立的模型语义接口：新增 `src/hybrid_tracker/preprocess/image_preprocessor.h`，定义 `YoloPreprocessor`、`YoloFrameSession` 和 `NanoTrackPreprocessor`；完成条件是接口只接收 `FrameRef`、普通几何参数及平台无关模型输入契约。
- [x] 1.4 接口生命周期可由调用方直接理解：在两个公共头文件补充 Doxygen，说明逐帧会话、目标重置、结果租约、调用顺序和失败后的状态要求，并通过人工检查确认每个公共 API 都说明所有权或借用关系。

## 2. 固定公共几何与数值契约

- [x] 2.1 YOLO 坐标变换只有一个计算来源：新增 `src/hybrid_tracker/preprocess/preprocess_geometry.h` 和 `.cpp`，实现 `calculateLetterboxTransform()`，并通过具体断言确认最小比例、最接近整数舍入、尺寸上限、奇数边距分配及实际每轴比例符合规格。
- [x] 2.2 NanoTrack 裁剪范围只有一个计算来源：在 `preprocess_geometry.*` 实现 `calculateNanoCropGeometry()`，并通过具体断言确认 `floor(position - (original_size + 1) / 2 + 0.5)` 及四侧越界填充符合规格。
- [x] 2.3 平台无关几何行为能够回归验证：新增 `src/hybrid_tracker/tests/preprocess_geometry_test.cpp` 和独立的 `preprocess_common_tests` 目标，覆盖方形输入、横竖非方形输入、奇数边距、零填充、四个图像边缘及无效尺寸；完成条件是该目标不链接 VPI、CUDA 或 TensorRT，且所有断言通过。
- [x] 2.4 YOLO 后处理使用公共变换结果：修改 `src/hybrid_tracker/yolo_detector.*`，让坐标还原消费 `LetterboxTransform`，并通过源码检查确认不存在另一套独立计算 `scale_x`、`scale_y`、`left` 或 `top` 的公式。

## 3. 建立准备输入与 TensorRT 的受检查交接

- [x] 3.1 可复用输入缓冲区在消费期间不会被覆盖：新增 `src/hybrid_tracker/preprocess/jetson/jetson_model_input.h` 和对应实现，使 `JetsonPreparedModelInput` 持有 `TensorBuffer` 租约及公共契约；完成条件是上一个结果存活时再次申请同一缓存会返回输入占用错误。
- [x] 3.2 TensorRT 只接受兼容的准备输入：修改 `src/hybrid_tracker/trt_engine.*`，增加接收 `PreparedModelInput` 的输入适配入口，并在绑定设备地址前检查具体类型、输入数量、形状、元素类型和布局；完成条件是所有不匹配路径均在 `enqueueV3()` 前返回可诊断失败。
- [x] 3.3 公共接口不因 TensorRT 适配重新泄漏平台类型：整理包含关系和前置声明，并通过 `rg -n '#include.*(NvInfer|cuda|vpi|rknn|rga)|nvinfer1::|cuda(Stream|Error|Memcpy)|VPI(Image|Stream)' src/hybrid_tracker/preprocess/model_input.h src/hybrid_tracker/preprocess/image_preprocessor.h src/hybrid_tracker/preprocess/preprocess_geometry.h` 确认无匹配结果。

## 4. 提取 YOLO 预处理实现

- [x] 4.1 一次检测请求只建立一个隔离帧快照：新增 `src/hybrid_tracker/preprocess/jetson/jetson_yolo_preprocessor.h` 和 `.cpp`，实现 `beginFrame()` 及 `YoloFrameSession`，在 `VpiFrameReadAccess` 作用域内完成私有帧复制和同步；完成条件是源帧租约释放后会话只持有私有图像状态。
- [x] 4.2 YOLO 区域能够生成既定模型输入：在 Jetson YOLO 会话实现 `prepare(region)`，使用公共 letterbox 结果执行线性缩放、114 边带填充、`1/255` 转换和三通道 `Float32 NCHW` 写入；完成条件是输入契约逐项校验且所有失败路径返回明确错误。
- [x] 4.3 VPI 重叠写规避规则得到保留：在 Jetson YOLO 实现中继续使用与内容区不相交的上、下、左、右边带，并通过源码审查确认不存在“填充整张输出后再覆盖内容区”的重叠提交顺序。
- [x] 4.4 YOLO 检测级联只依赖预处理接口：修改 `src/hybrid_tracker/yolo_detector.*`，让工作线程对每个请求调用一次 `beginFrame()`，再按 ROI、全图、切片顺序调用同一会话的 `prepare()`；完成条件是第一个有效目标仍终止后续级联，预处理失败不会触发推理。
- [x] 4.5 YOLO 不再管理预处理平台资源：从 `yolo_detector.*` 删除已迁移的 VPI stream、私有帧、缩放图、填充图、浮点画布、输入缓冲区和相关辅助函数，并通过 `rg -n 'VPI(Image|Stream)|vpi_|cudaMemcpy|input_buffer' src/hybrid_tracker/yolo_detector.h src/hybrid_tracker/yolo_detector.cpp` 确认没有预处理资源遗留。

## 5. 提取 NanoTrack 预处理实现

- [x] 5.1 NanoTrack 平台缓存具有单一所有者：新增 `src/hybrid_tracker/preprocess/jetson/jetson_nanotrack_preprocessor.h` 和 `.cpp`，集中拥有 VPI stream、补齐画布、均值图、模型尺寸缓存和模板/搜索输入缓冲区；完成条件是析构和失败路径对每个资源只有一个释放责任方。
- [x] 5.2 模板准备建立当前目标的填充状态：实现 `prepareTemplate()`，从初始化帧计算全图灰度均值，映射失败时使用 114，并按公共裁剪几何生成 `[1, 3, 127, 127]` 的 `Float32 NCHW` 输入；完成条件是像素保持 0 至 255 范围且三个通道一致。
- [x] 5.3 搜索准备复用当前目标而非旧目标状态：实现 `prepareSearch()` 和 `reset()`，按公共裁剪几何生成 `[1, 3, 255, 255]` 输入，并在目标重置时使均值及相关填充缓存失效；完成条件是新目标首次模板准备前不能使用旧目标均值。
- [x] 5.4 NanoTrack 主算法只依赖预处理接口：修改 `src/hybrid_tracker/nano_tracker.*`，保留中心、`s_z`、`scale_z`、`s_x` 和输出数学计算，把模板及搜索像素准备改为调用注入的 `NanoTrackPreprocessor`；完成条件是模板失败保持未初始化，搜索失败不改变中心和尺寸。
- [x] 5.5 NanoTrack 不再管理预处理平台资源：从 `nano_tracker.*` 删除已迁移的 VPI stream、补齐图、均值图、模型图缓存、输入缓冲区和辅助函数，并通过 `rg -n 'VPI(Image|Stream)|vpi_|cudaMemcpy|get_subwindow_buffer|update_mean_image|update_padded_image' src/hybrid_tracker/nano_tracker.h src/hybrid_tracker/nano_tracker.cpp` 确认没有预处理资源遗留。

## 6. 完成装配与构建描述

- [x] 6.1 默认程序能够获得匹配的预处理实现：修改 `src/hybrid_tracker/hybrid_tracker.cpp`、`src/hybrid_tracker/dcf_tracker.*` 及相关构造函数，显式创建并注入 Jetson YOLO 与 NanoTrack 预处理器；完成条件是 `HybridTracker::CreateInstance()` 的公开签名和节点调用方式保持不变。
- [x] 6.2 公共检测与跟踪头文件不再声明预处理平台类型：整理 `yolo_detector.h`、`nano_tracker.h` 和必要的实现私有状态，使用前置声明或实现对象隐藏剩余 TensorRT 细节；完成条件是两个头文件中不存在 VPI 类型、CUDA 输入缓冲区或 `nvinfer1::Dims`。
- [x] 6.3 构建描述包含全部新增生产代码：更新 `src/hybrid_tracker/CMakeLists.txt` 的公共几何、接口和具体预处理实现源文件列表；完成条件是每个新增生产源文件只由一个目标编译，且依赖可见性与其平台边界一致。
- [x] 6.4 平台无关测试目标保持独立：在 `src/hybrid_tracker/CMakeLists.txt` 增加 `preprocess_common_tests` 的源文件和链接描述；完成条件是该目标的链接接口不包含 `vpi`、`CUDA::cudart` 或 `nvinfer`。

## 7. 平台无关验证与静态审查

- [x] 7.1 会话、结果租约和失败传播可独立验证：增加内部模型调用测试接入点，生产注入 TensorRT 实现，测试注入模拟模型并运行真实检测器和跟踪器；新增使用模拟 `Frame`、模拟预处理器和模拟准备输入的测试，覆盖一次 `beginFrame()` 后多区域顺序、结果未释放时拒绝复用、格式错误、无效区域、契约不匹配和搜索失败不更新状态；完成条件是测试不依赖任何硬件 SDK 且全部通过。
- [x] 7.2 外部算法编排保持不变：通过源码对照确认 YOLO 的 ROI、全图、切片顺序及阈值选择未改变，并确认 NanoTrack 的模板建立、搜索调用、锚点处理和边界框更新公式未改变；完成条件是审查记录逐项对应 `specs/model-input-preprocessing/spec.md` 的场景。
- [x] 7.3 平台资源协议可静态审查：逐个检查 Jetson 预处理实现的帧读取租约、stream 同步、VPI view 销毁、输入租约释放和异常返回路径；完成条件是每次源帧设备读取都在租约释放前同步，且不存在重复释放或悬空句柄路径。
- [x] 7.4 变更满足仓库与 OpenSpec 质量门槛：运行公共头文件依赖扫描、`git diff --check` 和 `openspec validate extract-image-preprocessing-backend --strict`；完成条件是命令均通过，并在交付说明中明确本 change 未覆盖的平台运行时验证边界。
