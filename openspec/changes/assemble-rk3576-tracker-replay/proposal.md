## Why

RK3576 已能分别执行目标检测和单目标跟踪，但尚不能通过统一入口验证自动发现目标、持续跟踪、丢失和恢复的完整过程。现有模型级实机证据已经具备，需要把这些能力接入公共跟踪器，并用可复查的录像结果验证组合行为。

## What Changes

- 新增 RK3576 独立平台装配：统一创建帧、YOLO 检测组件、NanoTrack 跟踪组件和帧兼容性检查，复用已有 RKNN 执行与 OpenCV 预处理。
- 扩展公共模型路径描述，保留 Jetson 的共享 Backbone 路径，新增模板和搜索 Backbone 路径。Backbone 是从图像提取特征的模型；RKNN 版本按两种输入尺寸拆为两个文件。因此完整回放需要 YOLO、模板 Backbone、搜索 Backbone、Head 四个文件，模型职责仍为原有三类。
- 新增 `rk3576_tracker_replay CONFIG.yaml`。命令行只接受一个配置文件路径；模型、录像、帧率、算法参数、相机内参和输出位置全部由该文件提供，不接受命令行覆盖、环境变量覆盖或第二份配置。
- 用真实 `HybridTracker` 执行异步检测、NanoTrack、可选 DCF 辅助跟踪、检测/跟踪仲裁和卡尔曼预测。保持公共阈值、状态数值及调度算法语义。
- 定义固定帧率录像的时间戳、顺序送帧、结果有效性和安全退出规则，输出逐帧记录与运行汇总。
- 本次 NanoTrack 实机精度矩阵仅包含纯 FP16 和纯 INT8。FP16 是 16 位浮点；INT8 是八位整数模型编码。每组模板 Backbone、搜索 Backbone、Head 均采用相同精度，不安排混合精度验收，也不移除已有后端对混合精度的支持。

例如，以前需要分别运行 YOLO 检测工具和依赖人工首帧框的 NanoTrack 工具；完成后，只需传入一份配置，程序便从录像自动发现类别 2 的目标、建立跟踪，并逐帧记录丢失与恢复结果。

## Capabilities

### New Capabilities

- `rk3576-tracker-replay`：单配置文件驱动的完整算法录像回放，包括时间策略、输出、停止及两种纯精度的验证边界。

### Modified Capabilities

- `platform-factory`：追加共享/拆分 Backbone 的路径规则及独立 RK3576 装配能力；明确其生产帧源尚不可用，保留根目录生产构建仅支持 Jetson 的约束。

## Impact

- 涉及 `src/platform/platform_factory.h`、Jetson 装配的路径校验、`src/platform/rk3576/` 的帧与装配代码，以及新增 `src/replay/` 和示例配置。
- 沿用独立 ARM64 CMake 入口，保留现有两个 probe 工具。公共算法不链接 RKNN；新回放产物不链接 CUDA、TensorRT、VPI 或 Tegra 库。
- 配置和报告拟使用 OpenCV FileStorage 支持的 YAML/JSON；文件内容指纹使用通用 SHA-256 实现，拟通过 OpenSSL Crypto 提供。不引入 ROS 配置读取依赖。
- 本次不开放根目录 RK3576 ROS 构建，不实现生产相机/视频帧源、相机录像、ROS 话题接入、旋转数据回放或 RGA 加速。
- 保留已有生产参数与 Jetson 三路径调用兼容性。Jetson 实机等价性、真实相机、长期部署指标仍需后续验收；本次稳定性检查不代替这些结论。
