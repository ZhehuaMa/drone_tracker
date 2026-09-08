# drone_tracker 仓库工作约定

## 分支与迁移目标

- 将 `rk3576` 视为主要开发分支，目标是在 Rockchip RK3576 上完整复现现有无人机跟踪程序。除非任务明确要求，不要在 `jetson` 分支上开发新功能。
- 将 `jetson` 视为可在 Jetson 硬件上运行的完整参考实现。移植或排查行为差异时，使用 `git show jetson:<path>` 或分支间 diff 核对源实现，不要凭印象重写算法。
- 优先保持外部行为和算法语义一致，再做 RK3576 专属优化。未经对应 OpenSpec 变更，不要改变 ROS 话题、参数含义、跟踪状态或模型前后处理结果。
- 识别并隔离 Jetson 专属边界。当前源码和构建文件直接依赖 CUDA、TensorRT、VPI、Jetson Multimedia API、Tegra 库及 CUDA 架构 87；这些是迁移源实现，不是 RK3576 的目标依赖。

## 架构与术语

- `src/drone_tracker_node.*` 是 ROS 2 编排层：读取参数，选择相机或视频输入，驱动跟踪器，并发布检测框、图像和状态。
- `src/mv_camera/` 是采集、解码和帧缓冲层。Jetson 实现以 `VPIImageWrapper` 传递图像，并使用 Jetson Multimedia API；移植时把平台资源管理留在该边界内。
- `src/hybrid_tracker/` 是算法管线：异步 YOLO 检测、NanoTrackV3 跟踪、可选 DCF 融合，以及带相机旋转补偿的卡尔曼滤波。`ALGORITHM_REVIEW.md` 可作算法说明，但以代码和已批准的 OpenSpec 为准。
- “三个模型”固定指 YOLO、NanoTrackV3 Backbone 和 NanoTrackV3 Head。Jetson 使用 TensorRT `.engine`，三者均已有对应的 RKNN 平台版本。
- `launch/` 和 `parameters/` 定义运行入口与实机参数；`models/` 安装到 ROS 包的 share 目录，模型路径由节点在运行时拼接。

## RKNN 移植约束

- 使用已有 RKNN 模型，不要把重新训练或重新转换模型作为默认前置任务。模型文件未作为普通源码纳入仓库；未经明确要求，不要提交大型模型二进制。
- 替换推理后端时逐项验证模型契约：输入尺寸、颜色或灰度格式、HWC/CHW 布局、数据类型、量化参数、归一化、letterbox/裁剪规则、输出张量顺序，以及 YOLO 解码与 NMS。二进制能加载不等于行为等价。
- 保持 `HybridTracker` 的平台无关状态机和检测/跟踪仲裁逻辑稳定。优先在图像封装、预处理和推理引擎层引入 RK3576 实现，避免把 RKNN 细节扩散到 ROS 编排层。
- 资源生命周期和并发是高风险区域。修改异步 YOLO、帧队列、图像锁或设备缓冲区时，明确所有权与线程边界，并执行对应的竞态、压力或回放验证。

## OpenSpec 工作流

- 在 `rk3576` 分支进行功能、架构、接口或可观察行为变更前，先检查 `openspec/changes/`，继续匹配的现有 change；需要新 change 时，先完成相应 OpenSpec 提案、设计、规格和任务，再实施代码。
- 实现时以 change 的 `tasks.md` 为执行清单。完成后验证实现与 proposal、design 和 delta specs 一致，再同步或归档；不要只改代码而让规格失真。
- 遵守 `openspec/config.yaml`：所有 OpenSpec 工件使用简体中文，并写成无需依赖对话上下文即可理解的独立文档。首次出现的专业术语要说明其当前含义和作用。

## 构建与验证

- 本项目是 C++17 的 ROS 2 `ament_cmake` 包。保留相邻代码风格，避免顺手格式化无关代码。
- 开始开发或构建前，若仓库根目录存在 `AGENTS.local.md`，先读取其中的本地工具、路径和容器约定。本地系统专属信息只写入该文件，不要写入本文件。
- 在匹配的目标架构和 SDK 环境中构建。常规入口为 `colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release`；RK3576 的本地构建方式以 `AGENTS.local.md` 为准。
- Jetson 参考实现应在匹配的 Jetson 硬件和 SDK 环境中构建。常规入口同样是 `colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release`；当前 Jetson 源实现还要求 ROS 2 依赖、OpenCV、CUDA、TensorRT、VPI、FFmpeg 和 Jetson Multimedia API。
- 需要测试目标时增加 `-DDRONE_TRACKER_BUILD_TESTS=ON`。`hybrid_tracker_tests` 覆盖算法级逻辑；`vpi_race_repro` 依赖 VPI/CUDA；`hybrid_tracker_stress` 和 `replay_harness` 需要真实模型，回放测试还需要录制视频。
- 不要用非目标主机上的编译结果代替实机验证。交付时说明使用的平台、模型和输入数据，并明确列出因缺少 RK3576/Jetson 硬件、SDK、模型或录像而未运行的检查。

## 兼容性与评审重点

- 默认保持 `/fpv/gimbal`、`/fpv/odom`、`/fpv/tracker_trigger` 输入，以及 `/fpv/target_bbox`、`/fpv/tracker_image`、`/fpv/tracker_state` 输出的名称、消息类型和时序语义。
- 保持 `TrackerState` 数值语义：`Detecting=0`、`TargetFound=1`、`Tracking=2`、`TargetLost=3`。参数默认值或单位发生变化时，同步检查头文件、两个 YAML 配置和 launch 覆盖项。
- 将 `readerwriterqueue.h`、`threadpool.h` 等第三方或内嵌依赖视为外部代码；除非任务直接要求，不要对其进行风格重构。
- 仓库未提供可见的 CI 配置。评审时重点检查跨平台依赖是否泄漏、模型契约是否一致、设备资源是否释放、队列是否仍符合其生产者/消费者假设，以及失败路径能否安全停止线程。
