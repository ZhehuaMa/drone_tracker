## Why

RK3576 当前只能独立运行目标检测，尚不能使用仓库中的公共跟踪算法持续跟踪已选目标。需要补齐 NanoTrackV3 推理与图像预处理，同时允许以后替换输入输出契约相同的模型，而不将程序绑定到固定模型文件或精度。

## What Changes

- 新增 RKNN NanoTrack 后端。Backbone 是从图像提取特征的模型；Head 是比较模板特征与搜索特征、输出分类及位置的模型。使用模板 Backbone、搜索 Backbone、Head 三个 RKNN 文件装配一个后端。
- 新增 OpenCV 灰度预处理，保持现有裁剪、均值填充、浮点缩放和三通道输入语义，复用公共 NanoTracker 的框更新算法。
- 将现有 RKNN Runtime 拆为通用单模型执行会话与模型专属检查。会话负责多输入输出及资源管理；YOLO 后端保留现有尺寸、量化和六输出检查，NanoTrack 后端管理特征衔接与模板状态。
- 使用同一后端支持 FP16、INT8 及两种 Backbone/Head 混合精度组合。FP16 是 16 位浮点，INT8 是通过比例和零点表示实数的 8 位整数。模型间首版使用 Float32（32 位浮点）特征交接。
- 按数据含义、形状和受支持的存储属性接受模型替换，不设置文件哈希或版本白名单。记录模型身份仅用于重现测试。
- 增加独立 ARM64 查询与回放入口。比如给定视频、首帧目标框和三个模型路径后，依次建立模板、搜索后续帧、输出框和置信度；替换为契约兼容模型后，无需重新编译即可重复该流程。
- 首先用全 FP16 建立实现正确性基线，再用相同灰度输入验证 INT8 和混合精度；最终部署精度由质量与性能数据决定。

## Capabilities

### New Capabilities

- `rk3576-model-session`: RKNN 通用同步执行、多输入输出校验、独立输出所有权及安全失败行为。
- `rk3576-nanotrack-backend`: 可替换模型的 NanoTrack 三文件装配、灰度预处理、精度适配、模板状态与独立验证入口。

### Modified Capabilities

无。现有 `model-execution`、`model-backends`、`model-input-preprocessing` 和 `rk3576-yolo-backend` 要求继续有效；YOLO 执行层提取不改变其模型契约。完整 `platform-factory` 接入另行变更。

## Impact

- 主要影响 `src/platform/rk3576/` 的运行时、工厂、CMake 与测试；复用 `src/hybrid_tracker/model/` 输出校验和公共预处理几何。
- 新增平台内部 Session、NanoTrack 后端、预处理器及独立命令，不将 RKNN 类型暴露到公共接口，不改 ROS 参数和话题。
- 目标兼容性基线为 RKNN Runtime 2.3.2、RKNPU 驱动 0.9.8 与 OpenCV。本次沿用既有 2.3.0 构建 SDK（编译所用的头文件与链接库），产物在目标板 Runtime 2.3.2 上验证；不要求重建 SDK 镜像。不新增 CUDA、TensorRT、VPI 或 RGA 依赖。
- 保留现有 YOLO 独立入口及构建方式。完整 RK3576 平台选择、相机采集、ROS 节点装配、零拷贝、重新训练或转换模型均不在本次范围内。
- 验收区分公共测试、ARM64 构建、RK3576 实机灰度回放与尚未取得的 Jetson 对照证据。现有彩色模型评估不能替代本次灰度语义验证。
