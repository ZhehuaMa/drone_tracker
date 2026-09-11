# 平台工厂接入说明

`PlatformFactory` 是生产程序统一创建平台资源的入口。它创建帧源、帧创建器和整套跟踪依赖。公共算法只使用接口，不链接硬件实现。

当前生产构建默认使用 `DRONE_TRACKER_PLATFORM=jetson`。完整 `rk3576` 平台尚未实现；选择它或未知值时，CMake 在启用 CUDA 和查找硬件开发库前失败，并列出支持值 `jetson`。RK3576 已有独立 YOLO 和 NanoTrack 验证入口；NanoTrack 全 INT8 已有灰度回放和共存验证。FP16 属性误拒绝已修复；四种精度组合均通过实机查询和 309 帧灰度回放。采集和完整 ROS 节点尚未接通。模拟组件仅供测试，不能作为生产平台选项。

## RK3576 独立 YOLO 使用

在 ARM64 环境使用 C++17、OpenCV（core、imgproc、dnn、video、imgcodecs、videoio）、线程库及 RKNN SDK。目标兼容性基线为 Runtime 2.3.2、驱动 0.9.8。本次沿用既有 2.3.0 构建 SDK（编译所用的头文件与链接库），产物在目标板 Runtime 2.3.2 上验证，不要求重建 SDK 镜像。此入口无需 ROS、CUDA、TensorRT 或 VPI。开发机的容器和挂载约定见仓库根目录 `AGENTS.local.md`；以下命令在该 ARM64 环境内执行，SDK 路径按实际安装位置填写：

```sh
cmake -S src/platform/rk3576 -B build/rk3576-yolo \
  -DRKNN_INCLUDE_DIR=/usr/include \
  -DRKNN_LIBRARY=/usr/lib/librknnrt.so -DCMAKE_BUILD_TYPE=Release
cmake --build build/rk3576-yolo -j4
ctest --test-dir build/rk3576-yolo --output-on-failure
```

模型必须是固定 640×640 灰度三通道输入、六输出的兼容 YOLO。三个尺度按 80、40、20 排列，每个尺度先输出 64 通道框分布，再输出 C 通道类别原始分数。C 是模型类别数。输入原生编码为 INT8 NHWC，零点 -128、比例 1/255，转换使用 mean=0/std=255。工具提交 UINT8 NHWC，即按批次、高、宽、通道排列的字节像素。预处理不再除以 255。

模型提供者须确认上述数值语义；形状查询不能判断输出是否已经执行 Sigmoid（将原始分数转为概率）。不兼容的九输出或已输出概率的变体不在支持范围内。模型文件由部署者管理，不提交到源码。

四类别模型中无人机编号为 2 时，完整使用顺序如下。`sample.jpg` 和 `clip.mp4` 是部署者提供的真实图像与录像。

```sh
build/rk3576-yolo/rk3576_yolo_probe --model models/four_classes.rknn --mode query
build/rk3576-yolo/rk3576_yolo_probe --model models/four_classes.rknn \
  --mode detect --target 2 --image sample.jpg
build/rk3576-yolo/rk3576_yolo_probe --model models/four_classes.rknn \
  --mode detect --target 2 --video clip.mp4
mkdir -p results
build/rk3576-yolo/rk3576_yolo_probe --model models/four_classes.rknn \
  --mode region --image sample.jpg --dump results/sample
```

查询输出包含 Runtime、驱动、C 和全部张量属性。检测输出包含帧号、是否命中，以及命中时的类别、分数和原图框。工具在输入边界显式将彩色图像转灰度。检测使用真实异步 ROI、全图、切片级联；可用 `--roi x y width height` 指定优先区域。区域模式导出 `.input.u8`、六份 `.output0.f32`～`.output5.f32` 和 `.prediction.f32`，便于逐层数值对照。也可用 `--constant 0`、`114` 或 `255` 代替图像执行区域模式。

更换为六类别兼容模型且无人机编号为 4 时，结束旧进程，查询新模型，再重新启动：

```sh
build/rk3576-yolo/rk3576_yolo_probe --model models/six_classes.rknn --mode query
build/rk3576-yolo/rk3576_yolo_probe --model models/six_classes.rknn \
  --mode detect --target 4 --image sample.jpg
```

预期查询报告 C=6，命中时只报告 class=4；六类别示例不表示已取得真实六类别模型验收证据。无需改代码，不依据文件名推断类别。公共参数 `tracker.yolo_target_class_id` 同样改为单个精确编号，默认值仍为 0。以前把 0 当作“接受所有类别”的部署必须重新配置。候选直接使用目标类分数，其他类别不参与 NMS（去除高度重叠的重复框）。该规则也适用于 Jetson。

失败诊断与处理：

| 诊断 | 检查与处理 |
| --- | --- |
| 找不到 RKNN | 检查 `RKNN_INCLUDE_DIR`、`RKNN_LIBRARY` 和 ARM64 库版本 |
| 模型或张量契约不匹配 | 按模型路径及张量身份核对形状、类型、顺序和量化参数；不改宽容差绕过 |
| 目标编号越界 | 按查询的 C 设置 0～C-1；失败时不启动检测线程 |
| 图像、区域或输入存储错误 | 修正输入；区域缓存仍被持有时释放旧结果后再准备新区域 |
| Runtime 执行失败 | context 失效后重新初始化，不消费部分结果 |
| 无法确认设备停止 | 按致命错误契约终止进程；保留日志检查 SDK/驱动，不继续复用设备存储 |

验证范围和未完成项目见 [本次实机记录](../../openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/board-validation.md)。独立检测运行不代表完整跟踪平台、Jetson 等价性或性能验收通过。

## RK3576 独立 NanoTrack 使用

与上面的 YOLO 入口使用同一独立 CMake 构建，生成 `rk3576_nanotrack_probe`。目标兼容性基线为 Runtime 2.3.2、驱动 0.9.8；2026-09-11 按用户要求先测试既有 Runtime 2.3.0 构建产物，未重建 SDK 镜像。

Backbone 从图像提取特征，Head 比较模板与搜索特征。分别指定模板 Backbone、搜索 Backbone 和 Head 三个文件。例如在上述构建目录中执行：

```sh
build/rk3576-yolo/rk3576_nanotrack_probe \
  --template-model models/template_backbone.rknn \
  --search-model models/search_backbone.rknn \
  --head-model models/head.rknn --mode query
build/rk3576-yolo/rk3576_nanotrack_probe \
  --template-model models/template_backbone.rknn \
  --search-model models/search_backbone.rknn \
  --head-model models/head.rknn --mode replay \
  --video clip.mp4 --bbox 100,80,40,30
```

`--bbox` 是首帧目标的左上角 x、y 与宽、高，单位为像素。回放明确将彩色帧转为 Gray8（每像素一个灰度字节），再调用公共 `NanoTracker`。输出包含帧号、成功标记、框、置信度和毫秒耗时；初始化帧没有置信度，失败帧不输出成功框。

公共裁剪输入为 Float32（32 位浮点）、0～255，先转浮点再缩放，不额外除以 255。NCHW 表示批次、通道、高、宽，NHWC 表示批次、高、宽、通道。平台内部支持这两种排列，并将以下形状统一解释为 NCHW：

| 模型 | 输入 | 输出 |
| --- | --- | --- |
| 模板 Backbone | `[1,3,127,127]` | `[1,96,8,8]` |
| 搜索 Backbone | `[1,3,255,255]` | `[1,96,16,16]` |
| Head | 上述两份特征 | 分类 `[1,2,15,15]`、位置 `[1,4,15,15]` |

角色按唯一形状匹配实际索引，不依赖张量名称。分类必须是未转概率的原值，位置按左、上、右、下距离解释。模型提供方负责保证灰度三通道、归一化及特征的含义兼容；形状检查不能证明这些语义，仍需固定浮点输入对照。

设计支持 FP16（16 位浮点）、Float32 和逐张量仿射 INT8。仿射量化用 scale 和 zp 将整数还原为实数，INT8 要求有效正比例及合法零点。四种 Backbone/Head 精度组合使用 Float32 特征交接；SDK 返回浮点后不再次反量化。FP16/Float32 携带 AFFINE 标记时仍按浮点类型处理，不使用 scale/zp 修改数值或套用 INT8 参数取值限制。旧程序对此误报 `Unsupported NanoTrack quantization` 的检查已移除；未支持的量化形式、类型和布局仍会被拒绝。修复已通过 ARM64 构建与 16 项契约测试，四组合实机查询和回放也已通过，四组合固定输入逐层数值对照也已通过。

契约兼容的权重无需重新编译；结束旧进程后用新的三个路径重新创建后端即可。不设置文件名、哈希或版本白名单，不支持运行中热替换。查询及回放的实机结果、阻塞和未执行项见 [NanoTrack 验证记录](../../openspec/changes/implement-rk3576-nanotrack-backend/validation.md)。

当前全 INT8 在 309 帧标注视频上的 308 次搜索均返回有效框，平均 IoU 为 0.316，平均中心误差为 2.35 像素。IoU 是预测框与标注框的交并比，返回有效框不代表框准确。四精度同条件比较已完成：本序列上 FP16 Backbone + INT8 Head 的平均 IoU 为 0.620。独立数值对照已通过，但不能据单序列结果确定最终部署精度。

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

此前 `introduce-platform-factory` 变更只提供公共测试和 Jetson 源码审查证据，详见 [平台工厂验证报告](../../openspec/changes/archive/2026-09-10-introduce-platform-factory/verification.md)。该阶段未配置、编译或运行 Jetson 程序，也未验证真实像素、设备资源安全或跟踪行为等价。

当前 `implement-rk3576-yolo-backend` 已补充 RK3576 真实模型、图像、录像和有限资源生命周期验证，详见 [实机记录](../../openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/board-validation.md) 和 [独立验证报告](../../openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/verification.md)。这些结果不代表 Jetson 实机等价性或完整跟踪平台验收通过。
