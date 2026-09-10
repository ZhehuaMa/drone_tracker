## Why

现有检测与跟踪程序仍不能独立替换模型执行实现，迁移硬件时容易同时改变模型数据交接和跟踪行为。帧与预处理接口已经提取，本次继续隔离模型执行边界，使现有 Jetson 实现成为可替换且可验证的具体后端。

## What Changes

- 提取 `ModelExecutor`，统一单个模型的加载结果、输入输出描述、同步执行和错误报告。张量是模型使用的多维数组；其契约描述形状、类型、布局和数据含义。
- 将现有 `TrtEngine` 改造为 `TensorRtModelExecutor` 具体实现，把 TensorRT 维度、CUDA 指针和设备同步留在 Jetson 内部。
- 将内部模型调用接入点提升为 `YoloBackend` 与 `NanoTrackBackend`。前者执行检测模型，后者组织 Backbone（图像特征提取模型）和 Head（比较模板与搜索特征的模型），并管理模板生命周期。
- 提供具名、受存储所有权保护的输出；补齐输入、输出及模型组合的校验，安全拒绝不支持的数据类型。
- 保留现有预处理接口、输入租约和公共后处理。租约表示结果仍被持有时，底层缓存不能被覆盖。后处理继续负责把模型数值转换为检测框和跟踪结果。
- 默认装配仅提供 Jetson 后端，继续加载现有三个 TensorRT 引擎文件。增加公共接口及真实调用链的无硬件测试入口。

例如，当前 NanoTrack 搜索依赖具体引擎缓冲区，把两个输出按通道数分辨后交给算法。改造后依次执行搜索 Backbone、模板与搜索特征校验、Head 推理，再返回具名分类与位置输出。公共算法仍按原公式产生相同的目标框。

## Capabilities

### New Capabilities

- `model-execution`：平台无关张量描述、同步执行、存储生命周期、契约校验、诊断和 Jetson 执行实现。
- `model-backends`：YOLO 与 NanoTrack 模型级接口、输出角色、模板状态、默认装配及公共算法行为保持。

### Modified Capabilities

- `model-input-preprocessing`：将“无硬件测试运行真实检测与跟踪调用链”中的内部模型测试接入点升级为正式模型后端接口；保留现有预处理输入与失败语义。

## Impact

- 主要涉及 `src/hybrid_tracker/trt_engine.*`、`detail/model_calls.h`、`detail/jetson_model_calls.cpp`、`preprocess/jetson/jetson_model_input.*`、检测器与跟踪器、生产装配和相关 CMake 测试目标。
- 新增 `src/hybrid_tracker/model/` 公共模型接口与 `model/jetson/` 实现目录；不重排其他模块。
- 不实现 RK3576、RKNN、RGA 或其他生产后端，不引入平台切换选项，不修改模型二进制。
- ROS 话题、参数、状态数值、YOLO 级联顺序和 NanoTrack 数学处理保持不变。非法模型将获得更早、更明确的拒绝诊断。设备提交后若无法确认停止访问存储，则输出致命诊断并立即终止进程；这是严重设备故障时明确批准的行为变化。
- 整个程序仍有帧、采集和其他 Jetson 依赖；本次不以全程序脱离硬件 SDK 为完成标准。
- 当前可执行验收以公共测试和静态资源审查为界。Jetson 编译、真实模型和录像对照是尚缺的运行时证据，不以本机测试冒充，也不沿用前次归档作为本次运行证据。
