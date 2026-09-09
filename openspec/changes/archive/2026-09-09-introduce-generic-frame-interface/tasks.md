## 1. 建立通用帧契约

- [x] 1.1 通用代码能够描述帧及只读主机视图：新增 `src/frame/frame.h`，定义 `FramePixelFormat`、`FrameReadView`、`Frame` 和 `FrameRef`，并通过只包含该头文件的编译冒烟测试确认不需要 VPI、CUDA、RKNN 或 RGA 类型。
- [x] 1.2 帧接口的生命周期约束可被调用方直接理解：在 `src/frame/frame.h` 为时间戳单位、只读语义、`FrameReadView` 有效期和映射失败结果补充 Doxygen 文档，并通过人工检查确认每个公共 API 都说明所有权或借用关系。

## 2. 实现 Jetson VPI 帧

- [x] 2.1 VPI 图像具有明确且唯一的所有者：新增 `src/frame/vpi_frame.h` 和 `src/frame/vpi_frame.cpp`，实现 `VpiFrame` 的接管创建、`cv::Mat` 复制创建、格式校验和单次销毁，并通过源码审查确认空句柄与不支持格式具有明确失败分支、对象复制已被禁止。
- [x] 2.2 主机映射能够自动解除 VPI 锁：在 `src/frame/vpi_frame.cpp` 实现 `VpiFrameReadView`，保留底层所有权并在所有成功、失败和异常退出路径正确调用 `vpiImageUnlock()`，通过 `rg 'vpiImageLockData|vpiImageUnlock' src` 确认通用帧消费者不再直接管理主机锁。
- [x] 2.3 同一 VPI 帧的主机与设备读取使用统一协调器：在 `VpiFrame` 中实现逐帧访问互斥量及 `VpiFrameReadAccess`/`acquireVpiRead()`，并通过逐个检查访问入口确认主机视图和 VPI 读取租约持有同一个协调器、不同帧不共享全局锁。
- [x] 2.4 VPI 原生读取的同步前置条件被固定为可审查契约：在 `src/frame/vpi_frame.h` 记录 VPI stream 必须在释放 `VpiFrameReadAccess` 前同步，并通过代码审查检查清单确认所有租约调用点均在作用域内完成同步。

## 3. 迁移帧生产与公共传递链

- [x] 3.1 相机和视频生产者只向队列发布共享只读帧：修改 `src/mv_camera/mv_camera_base.h`、`src/mv_camera/mv_camera.cpp` 和 `src/mv_camera/mv_video.cpp`，让 `ImageQueue` 保存 `FrameRef` 并创建 `VpiFrame`，通过源码审查确认队列满时仍只在生产端丢弃最新帧且不执行消费端出队操作。
- [x] 3.2 公共跟踪输入不再暴露 VPI 类型：修改 `src/hybrid_tracker/hybrid_tracker.h` 和 `src/hybrid_tracker/hybrid_tracker.cpp`，让 `FrameContext` 与 `BufferedFrame` 保存 `FrameRef`，并通过公共头文件编译冒烟测试确认 `FrameContext` 声明不需要 VPI 或 CUDA 头文件。
- [x] 3.3 采集时间戳只有帧对象一个来源：删除 `FrameContext::frame_ts_us` 及补帧缓存中的重复帧时间字段，修改 `src/drone_tracker_node.cpp`、`src/hybrid_tracker/hybrid_tracker.cpp`、`src/hybrid_tracker/dcf_tracker.cpp` 和回放代码统一调用 `Frame::timestampUs()`，通过 `rg 'frame_ts_us|get_timestamps' src` 确认不存在旧时间戳入口，并人工核对 `now_us` 仍只用于实时外推。

## 4. 迁移帧消费者

- [x] 4.1 DCF 通过通用视图读取图像：修改 `src/hybrid_tracker/dcf_tracker.h` 和 `src/hybrid_tracker/dcf_tracker.cpp`，移除 `VpiGrayLock` 并使用 `FrameReadView` 处理 `Gray8` 与 `Bgr8`，通过 `rg 'VpiGrayLock' src/hybrid_tracker` 确认旧锁类型已清除，并人工核对 BGR 转灰度分支保持原有转换规则。
- [x] 4.2 ROS 图像发布通过通用视图读取图像：修改 `src/drone_tracker_node.cpp`，使用 `FrameReadView` 生成 `bgr8` 消息并显式处理映射失败，通过源码审查确认灰度扩展、BGR 直用、行跨度和 `Frame::timestampUs()` 分支均已覆盖。
- [x] 4.3 Jetson YOLO 在受检查的 VPI 边界处理帧：修改 `src/hybrid_tracker/yolo_detector.h` 和 `src/hybrid_tracker/yolo_detector.cpp`，拒绝不兼容帧，并在 `VpiFrameReadAccess` 作用域内完成源帧到工作线程私有 VPI 图像的复制与 stream 同步；通过源码检查确认复制完成后立即释放源帧引用，且不存在绕过读取租约的源帧句柄访问。
- [x] 4.4 Jetson NanoTrack 在受检查的 VPI 边界处理帧：修改 `src/hybrid_tracker/nano_tracker.h`、`src/hybrid_tracker/nano_tracker.cpp` 和 `src/hybrid_tracker/dcf_tracker.cpp`，在每次原生预处理期间持有 `VpiFrameReadAccess` 并在释放前同步，通过逐个检查初始化、更新和补帧调用点确认没有直接访问无租约的 `VPIImage`。
- [x] 4.5 跟踪状态机能够安全拒绝非 VPI 帧：修改 `src/hybrid_tracker/hybrid_tracker.cpp` 的当前 Jetson 调用边界，对 `FrameRef` 执行受检查转换并返回可诊断失败，通过源码审查确认失败分支不会更新帧计数、补帧缓存、跟踪状态或滤波状态。

## 5. 清理依赖与旧实现

- [x] 5.1 公共头文件不再间接包含平台 SDK：整理 `src/hybrid_tracker/dcf_tracker.h`、`src/hybrid_tracker/hybrid_tracker.h` 及相关前置声明，把 VPI 具体包含保留在专属头文件或源文件，并通过依赖扫描确认公共帧输入头文件中不存在 `vpi`、`cuda`、`rknn` 或 `rga` 包含。
- [x] 5.2 旧帧包装不再参与构建：删除 `src/mv_camera/image_wrapper.h`，更新顶层、`src/mv_camera/CMakeLists.txt` 和 `src/hybrid_tracker/CMakeLists.txt` 的包含目录与源文件，并通过 `rg 'VPIImageWrapper|image_wrapper.h' src CMakeLists.txt` 确认没有遗留引用。
- [x] 5.3 本次变更保持既定范围：检查变更文件，确认没有新增 RK3576 帧、通用相机接口、通用预处理接口、通用推理接口或平台工厂，并确认 CUDA、VPI 和 TensorRT 构建依赖仍由现有 Jetson 构建使用。
