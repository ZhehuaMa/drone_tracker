## Why

当前跟踪管线把 Jetson 的 VPI 图像类型直接传入相机队列、跟踪状态机和 ROS 编排层，导致后续接入其他平台帧时必须同时修改整条处理链。此次改动先建立稳定的通用帧边界，并保持 Jetson 上现有检测、跟踪和发布行为不变，为后续 RK3576 迁移降低耦合风险。

例如，改动前相机产生 `VPIImageWrapper`，队列、`FrameContext`、补帧缓存和图像发布都直接操作 `VPIImage`；改动后相机以 `FrameRef` 传递帧，公共代码只读取尺寸、像素格式、采集时间戳和受生命周期保护的主机视图，只有现有 Jetson 实现访问 VPI 原生句柄。

## What Changes

- 新增平台无关的 `Frame` 抽象，定义帧尺寸、像素格式、采集时间戳和只读主机映射。
- 新增 `FrameReadView`。它是在 CPU 可读取帧数据期间有效的只读视图，并通过 RAII 在析构时自动解除平台映射或锁定。
- 新增 `FrameRef` 共享只读帧引用，用于相机队列、`FrameContext`、异步检测请求和补帧缓存，确保帧在所有消费者完成前保持有效。
- 将现有 `VPIImageWrapper` 重构为 `VpiFrame`，作为 `Frame` 的 Jetson VPI/CUDA 实现；它继续负责 `VPIImage` 的所有权、销毁、主机锁定和原生句柄访问。
- 将帧采集时间戳收敛到 `Frame`，移除 `FrameContext` 中重复的帧时间戳来源。
- 在需要 VPI 原生句柄的现有 YOLO 和 NanoTrack 路径设置受检查的 `VpiFrame` 转换边界；不在通用接口中暴露 `void *` 或 `VPIImage`。
- 保留异步 YOLO 工作线程的私有 VPI 帧副本，避免它与主线程的主机映射并发操作同一 `VPIImage`。
- 调整公共头文件依赖，使通用帧接口以及 `HybridTracker` 的公共帧输入不暴露 CUDA 或 VPI 类型。
- 不实现 RK3576 帧，不抽象相机、预处理、模型执行或平台工厂，也不移除当前 CUDA、VPI 和 TensorRT 构建依赖。
- 不改变 ROS 话题、消息类型、参数含义、`TrackerState` 数值、模型前后处理或跟踪状态机行为。

## Capabilities

### New Capabilities

- `generic-frame`: 定义跨模块传递帧所需的平台无关元数据、共享所有权、只读主机访问和现有 VPI 实现兼容要求。

### Modified Capabilities

无。

## Impact

- 主要影响 `src/mv_camera/` 的帧包装与队列、`src/hybrid_tracker/` 的帧输入和缓存，以及 `src/drone_tracker_node.cpp` 的图像发布。
- 将增加通用帧头文件和 VPI 帧实现，并更新对应 CMake 源文件列表；本地验收仅包括头文件冒烟检查、源码扫描和人工审查。
- Jetson 的 VPI、CUDA、TensorRT、Jetson Multimedia API 和现有模型依赖保持不变。
- 当前本地系统不具备 Jetson 编译或运行环境，因此本 change 不包含 Jetson 构建、设备运行、压力测试、性能测量或固定录像回放任务。
- 本次改动不能单独使工程在 RK3576 上构建或运行；RK3576 帧管理和其他平台后端将在后续 OpenSpec change 中处理。
