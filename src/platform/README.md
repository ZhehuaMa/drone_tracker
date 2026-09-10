# 平台工厂接入说明

`PlatformFactory` 是生产程序统一创建平台资源的入口。它创建帧源、帧创建器和整套跟踪依赖。公共算法只使用接口，不链接硬件实现。

当前生产构建默认使用 `DRONE_TRACKER_PLATFORM=jetson`。`rk3576` 尚未实现；选择它或未知值时，CMake 在启用 CUDA 和查找硬件开发库前失败，并列出支持值 `jetson`。模拟组件仅供测试，不能作为生产平台选项。

## 新增调用方

最终程序显式链接 `hybrid_tracker` 和 `platform_jetson`。包含 `platform/platform_factory.h` 与 `hybrid_tracker.h`，无需包含 VPI、TensorRT 或具体相机头文件。

按以下顺序创建管线：

1. 调用 `CreatePlatformFactory()`。
2. 相机或视频输入调用 `createFrameSource(config)`。查询帧源尺寸后，计算现有相机内参。
3. 回放或合成输入调用 `createFrameBuilder()`，用 `copyFrom(image, timestamp_us)` 复制主机图像。时间戳单位为微秒。
4. 将三个完整模型路径交给 `createTrackerComponents(paths)`。失败结果只含诊断，不提供半套依赖。
5. 把 `components.value` 移交 `HybridTracker::CreateInstance(settings, std::move(components.value))`，然后调用 `init()`。
6. 初始化成功后才启动业务和采集。停止采集与节点工作线程后再销毁跟踪器。

例如，回放已得到灰度图 `gray`，模型路径已填入 `settings`：

```cpp
auto factory{CreatePlatformFactory()};
auto builder{factory->createFrameBuilder()};
auto components{factory->createTrackerComponents({settings.yolo_engine_path,
    settings.nanotrack_backbone_path, settings.nanotrack_head_path})};
if (!builder || !components) {
    throw std::runtime_error((!builder ? builder.error : components.error).describe());
}
auto tracker{HybridTracker::CreateInstance(settings, std::move(components.value))};
if (!tracker->init()) { throw std::runtime_error("Tracker initialization failed"); }
auto copied{builder.value->copyFrom(gray, 1000000)};
if (!copied) { throw std::runtime_error(copied.error.describe()); }
FrameContext context{};
context.frame = std::move(copied.value);
context.now_us = 1000000;
auto output{tracker->update(context)};
```

`TrackerComponents` 只能移动，表示调用后依赖的独占所有权交给跟踪器。它包含两个模型后端、两个预处理器和一个帧兼容性检查对象。NanoTrack 后端内部持有 Backbone 与 Head 两个模型。组件和已返回的帧不借用工厂对象，可以比工厂存活更久。

`FrameCompatibility` 检查帧的真实实现类型。另一个创建器产生的兼容帧仍可使用。错误类型在更新时间戳、帧计数和检测提交前被拒绝，`TrackerOutput::consumed` 为 `false`，`info` 含原因。预处理和推理边界继续执行自身的资源检查。

## 帧源与停止

帧源保留原采集队列。包含 `platform/frame_source_ops.h` 后，调用 `waitForLatestFrame(source, frame, 1s)`，由公共函数先等待首帧，再排空积压取得最新帧。可选回调在收到首帧后、排空前执行，节点用它记录处理计时起点。只能有一个消费者，不应再添加转发消费者。相机队列容量仍为 10，视频队列容量仍为 30；满队列时沿用原实现丢弃新帧。

启动前调用 `configureFrameSourceRecording(source, {need_save, directory, prefix})`。公共函数把配置交给帧源，相机转交给原 `MVCamera::setup()`；视频输入不创建相机录像。节点和模拟测试使用相同的取帧与录像配置函数。ROS bag 仍由节点管理。

OFF/ON 继续使用既有停止和 `reset()` 流程，模型不卸载。最终销毁时，跟踪器先等待检测线程结束，再释放缓存、模型和预处理资源。`DetectionThreadLauncher` 是可替换的线程启动函数，仅用于验证启动失败；默认值创建普通 `std::thread`。

## 公共验证

在有 C++17 编译器、OpenCV 和线程库的环境执行：

```sh
cmake -S src/hybrid_tracker -B build/platform-contracts \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/platform-contracts -j4
ctest --test-dir build/platform-contracts --output-on-failure
```

此入口编译真实公共跟踪器及可替换加载器的实际装配函数，不发现硬件开发库。测试没有真实模型或采集输入。平台选项测试只请求拒绝 `rk3576` 和 `other`，不配置 Jetson 目标。

本次变更只提供公共测试和 Jetson 源码审查证据。未配置、编译或运行 Jetson 程序，也未验证真实像素、设备资源安全或跟踪行为等价。具体结果和已发现的问题见本变更的 `verification.md`。
