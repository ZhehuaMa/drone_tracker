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

帧源保留原采集队列。包含 `platform/frame_source_ops.h` 后，调用 `waitForLatestFrame(source, frame, 1s)`，由公共函数先等待首帧，再排空积压取得最新帧。可选回调在收到首帧后、排空前执行，节点用它记录处理计时起点。只能有一个消费者，不应再添加转发消费者。相机与视频队列的构造参数分别为 10 和 30；底层按块分配，因此这些数字不代表严格容量。满队列时丢弃新帧，不覆盖旧帧。

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

## RK3576 完整跟踪录像回放

新增独立入口 `rk3576_tracker_replay` 通过统一工厂创建四个模型文件对应的组件，并运行真实公共 `HybridTracker`。它从自动检测开始，无需人工首帧框。四文件分别为 YOLO、模板 Backbone、搜索 Backbone 和 Head；两个 Backbone 分别提取模板和搜索图像的特征，Head 比较两者。原有两个 probe 继续保留。

此入口使用上述独立 ARM64 CMake 构建，额外需要 OpenSSL Crypto 开发库，用于计算 SHA-256 文件内容指纹。公共算法和旧 probe 不链接 Crypto。根目录生产构建仍只支持 Jetson；独立 RK3576 工厂拒绝相机请求，视频请求按下文的 MJPEG 帧源契约提供。原录像回放工具继续使用单独的同步解码器，不提供 ROS、相机采集或 RGA 加速。

### 单文件配置与调用

完整示例见 [纯 FP16 配置](../../parameters/replay_fp16.yaml) 和 [纯 INT8 配置](../../parameters/replay_int8.yaml)。FP16 是十六位浮点，INT8 是八位整数模型编码。复制示例后，填写实际的四模型文件、录像路径、帧率、相机内参及新的输出目录。示例中的相机数值只是格式展示，不能作为真实标定。

每次运行恰好传入一份配置：

```sh
build/rk3576-yolo/rk3576_tracker_replay parameters/replay_fp16.yaml
build/rk3576-yolo/rk3576_tracker_replay parameters/replay_int8.yaml
```

模型、录像、相机和算法设置全部来自这一个文件。程序不读取 ROS YAML，不读取环境覆盖，也不接受额外命令选项。相对路径以配置所在目录为基准。例如 `parameters/run.yaml` 中的 `models/yolo.rknn` 指向 `parameters/models/yolo.rknn`，与启动工作目录无关。

配置使用 OpenCV 支持的 YAML 1.0 子集，保留 `%YAML:1.0` 标头。所有字段必填，布尔值仅用整数 `0`/`1`。未知键、重复键、多文档、非有限数、错误类型及越界值均在模型加载前拒绝。整数以十进制书写，范围受有符号 32 位字段限制；帧号和时间记录另用 64 位整数。`max_frames: 0` 表示读到结束，正值表示主动限制帧数。输出目录必须尚不存在；重复运行先在配置中指定新的目录。

`camera.fx/fy/cx/cy` 的单位是像素，必须对应解码图像尺寸。程序不会缩放图像或相机参数。`tracking_roi_scale_factor` 对应公共 `roi_scale_factor`，决定围绕预测位置的搜索区域大小。`yolo_target_class_id` 是精确类别编号，加载后还会确认它小于模型类别数。`enable_dcf` 控制灰度相关滤波辅助跟踪。概率、IoU（框交并比）、倍率及噪声的合法范围见本变更设计，示例列出了全部字段及单位说明。

### 输入时间与送帧规则

使用者必须提供固定帧率录像。程序不能凭平均帧率证明输入不是变帧率录像。Gray8 是单通道八位灰度，BGR8 是三通道八位彩色；彩色帧在回放边界只转一次灰度。其他格式、尺寸中途变化或有效录像帧率与配置冲突都会失败。

第 `i` 帧的时间为 `1000000 + round(i * 1000000 / fps)` 微秒。30 FPS 的前三帧为 1000000、1033333、1066667。预测时间等于该帧时间，旋转矩阵固定为单位阵。帧时间必须严格递增且不能溢出。

送帧使用单调墙钟。下一次更新至少在前次更新结束一个帧周期之后开始。30 FPS 的一次更新若耗时 60 ms，下一次更新还要再等待约 33.333 ms。解码慢时继续顺延，不跳帧、不连续补发追赶。真实 YOLO 保持异步；该节奏和 `update_ms` 都不能作为总模型吞吐量指标。

### 输出解释与结束状态

每次运行产生三个文件：

| 文件 | 内容 |
| --- | --- |
| `frames.csv` | 每次 `update` 的输入帧号、采集/预测时间、接受标记、跟踪状态、框与有效性、NanoTrack 分数、文本和计时 |
| `run.json` | 完整解析配置、绝对路径、程序/配置/四模型/录像 SHA-256、构建提交及修改标记、同次加载的模型张量和 Runtime/驱动属性、输入统计、状态切换、计时和结束原因 |
| `diagnostics.log` | 模型、算法及主流程诊断；包括 C/C++ 标准输出和错误输出，与 CSV 分开 |

CSV 的 `display_*` 与 `realtime_*` 使用中心坐标 `cx/cy/width/height`；`nano_*` 使用左上角 `x/y/width/height`。无效框坐标留空，非有限分数留空；负 NanoTrack 分数表示该帧没有有效推断。`consumed=false` 的行仍保存，但不计入接受帧数。`Detecting=0`、`TargetFound=1`、`Tracking=2`、`TargetLost=3` 分别表示检测中、发现目标、跟踪中和目标丢失。

CSV 整数直接写入，JSON 的计数与帧号均保存为十进制字符串，解析时转为任意精度或 64 位整数，避免经过双精度浮点丢失精度。模型属性按行保存在 `model_runtime_attributes` 数组中，包含实际查询的 Runtime/驱动和张量描述；不为生成报告再次加载模型。`git_dirty_at_build` 表示构建配置时工作区是否修改，不代表后续仓库状态。

`decode_ms` 为解码时间，`convert_copy_ms` 为转灰度及帧复制时间，`update_ms` 为公共更新调用耗时，`wait_ms` 为调度等待时间。`submit_ns` 为相对本次送帧起点的单调墙钟纳秒数；`interval_ms` 为相邻提交间隔，`lag_ms` 为相对录像时间轴的落后量。汇总给出均值与 P95；P95 取排序后第 `ceil(0.95*N)` 个样本，空样本为 JSON `null`。仅保留计时与状态切换样本，图像及完整 CSV 行不累计在内存中。

`run.json` 先写 `running`，最终汇总通过临时文件替换。写入、刷新或关闭失败会返回失败。普通中断在主线程清理，先停止送帧，再等待检测线程退出；末尾不增加虚拟帧。每个进程只处理一段录像。

| 原因 | 退出码 | 含义 |
| --- | --- | --- |
| `eof` | 0 | 停止解码时已达到声明帧数；不证明文件无损 |
| `frame_limit` | 0 | 达到配置帧上限 |
| 配置/命令错误 | 2 | 初始化前拒绝；不创建运行目录 |
| `decode_error` | 1 | 声明帧数未达到、运行中的格式或尺寸错误、解码异常 |
| `end_unverified` | 1 | 无可信总帧数，无法确认完整结束 |
| `initialization_error` / `runtime_error` | 1 | 普通初始化或运行失败 |
| `interrupted` | 130 | SIGINT 已由主流程安全处理 |

不可写目录可能导致完全没有报告。SIGKILL、断电或设备致命失败不能保证最终汇总；已有 `running` 文件可显示未正常结束。普通模型推理失败仍由原算法处理，未发现目标不能单独证明设备故障。成功退出也不代表找到目标或质量合格。

### 本次验证范围

本次新增实机矩阵仅包含纯 FP16 和纯 INT8；每组模板、搜索、Head 必须采用同一精度。两组使用相同 YOLO、录像和算法参数。已有混合精度后端支持保持不变，但不作为本次新增实机前置条件。

待实机验收时，每组重复三次并覆盖 DCF 开/关，分别检查自动捕获、离开返回、无目标、至少 20 轮实例创建销毁、至少 30 分钟资源负载和失败退出。真实异步运行比较状态切换帧差异，不要求 CSV 字节相同。缺少场景素材或标注时保留未覆盖，不用持续跟踪录像替代丢失恢复验收。

当前仅执行 ARM64 容器构建及可控无设备测试。按照本次任务要求，开发板运行与依赖其结果的质量评估尚未执行，详见 [实施验证记录](../../openspec/changes/assemble-rk3576-tracker-replay/validation.md)。Jetson、ROS 和真实相机也不在这次验证范围内。

## RK3576 持续视频帧源

`createFactory(...)->createFrameSource(config)` 现在支持 `FrameSourceType::Video`。创建时打开文件并检查首帧，返回对象尚未启动。`getImageSize()` 在启动前即可查询实际尺寸，首帧仍会参与播放。视频忽略相机尺寸、fps、翻转和镜像设置；提交录像配置不会创建录像目录或输出文件。

| 入口 | 输入与用途 | 消费较慢时 |
| --- | --- | --- |
| `ReplayReader` / `rk3576_tracker_replay CONFIG.yaml` | 原有顺序算法回放，接口和配置不变 | 整体放慢，逐帧处理 |
| 工厂创建的 `FrameSource` | 本地 MJPEG 文件，验证持续供帧和队列行为 | 生产按视频时间推进，消费者可取最新帧并跳过积压 |

MJPEG 是每帧独立压缩为 JPEG 的视频。帧源使用 FFmpeg 读取容器，再使用 JPEG 库解码亮度。只接受本地普通文件，首个视频流必须为 MJPEG，音频等其他流会跳过。支持八位灰度和 YCbCr 的 4:2:0、4:2:2、4:4:4 采样；YCbCr 的 Y 分量表示亮度。输出为原尺寸 Gray8（每像素一个八位灰度值），不转彩色、不拉伸亮度范围，也不暴露 JPEG 边缘填充。其他颜色编码、位深或采样会明确失败。

`use_history_timestamp=false` 时，每次启动取当前系统微秒时间作为基准。开启历史模式时，严格读取容器 `Artist` 字段的正十进制 int64 微秒基准；名称不区分大小写，但重复、缺失或非法值均失败。帧时间等于基准加 PTS，PTS 是帧在视频中的显示时刻，先按视频时间基换算为微秒。换算按最近微秒取整，半微秒向上。首帧非零偏移保留，后续换算值必须严格递增，缺失、负值、重复或溢出均失败。

例如，历史基准 `1700000000000000` 加首帧 PTS `100000`，得到 `1700000000100000` 微秒。该帧在启动后约 100 ms 供给。播放使用单调时钟，即不受系统校时影响的时钟；解码已落后时立即继续，不重写帧时间。

调用顺序如下：

1. 创建视频帧源，查询尺寸，按需提交录像配置。
2. 在没有取帧调用进行时调用 `start()`，随后由单个消费者取帧。运行中再次启动返回 false。
3. 调用 `stop()` 停止生产。它可以与该消费者并发，并能打断尚未到期的播放等待。`waitFrame()` 自身仍可能等到调用方指定的超时。
4. 等消费者退出取帧调用后，才可再次 `start()`。每轮从头打开文件并重建队列，上一轮未消费帧不会混入新轮。重新打开时尺寸变化会启动失败。
5. 析构前先结束消费者。析构会回收生产线程，包括已自然结束但尚未回收的线程。

`stop()` 不清空队列，正常 EOF 后仍可读取尾帧。EOF 表示容器报告读完，不证明所有截断均可被识别。`isRunning()==false` 只表示生产结束，不表示队列为空。限时或非阻塞取帧返回 false 时输出引用清空；这个布尔值不能区分超时、正常结尾和运行错误。

实时模式重启会取新的系统时间基准。历史模式每轮重用录像时间，因此跨轮时间会重复。需要重复运行跟踪器的调用方必须另行处理算法状态复位；帧源只保证队列不混轮。消费者持有的帧及只读视图均独立于解码缓冲区、帧源和工厂存活。

### 无模型测试入口

独立构建额外需要 JPEG、libavformat、libavcodec 和 libavutil 开发库。这些依赖由 `rk3576_video` 目标声明，不反向链接 RKNN 或公共算法。独立顶层配置仍需要既有 ARM64/RKNN 构建 SDK；下面的视频测试不加载模型、不执行 NPU 推理。NPU 是开发板的神经网络计算设备。

构建启用测试后，运行以下命令。`BUILD` 表示独立 CMake 构建目录，例如容器内 `/work/ros2_ws/build/video-source`：

```sh
BUILD=/work/ros2_ws/build/video-source
ctest --test-dir "$BUILD" --output-on-failure
"$BUILD/video_fixture_generator" "$BUILD/video-fixtures"
"$BUILD/video_source_driver" "$BUILD/video-fixtures/slow.mov" 1 60 2
```

驱动的四个参数依次为文件、历史模式（0/1）、每次消费后等待毫秒数、播放轮数。示例通过真实工厂和 `waitForLatestFrame()` 播放两轮，每次消费后等待 60 ms。输出包含轮次、帧微秒时间、像素摘要和结束统计；像素摘要是便于重复比较的 FNV-1a 校验值。该驱动用于视频边界验证，不改变原有完整跟踪回放命令。

Linux 实机另提供 `video_board_validation FIXTURE_DIRECTORY 600`。它先检查两种时间模式各 20 轮重启、错误输入和满队列，再执行同进程 100 次创建销毁及至少 600 秒重启/慢消费负载。输出包含停止耗时及进程内存、线程、文件句柄采样；不自动加入普通 CTest，以免每次回归都运行十分钟。使用前先通过上述生成器准备素材目录。

平台内部 `diagnostic()` 提供线程安全快照。`VideoState` 值依次为 Ready=0、Running=1、Stopped=2、Eof=3、Failed=4，与公共跟踪器的 `TrackerState` 无关。诊断包含路径、从 1 开始的帧序号、原始 PTS、原因和本轮统计。`read` 统计成功读取并进入处理的帧，`enqueued` 统计入队帧，`dropped` 统计队列满而丢弃的新帧。读取或解码失败会标明尝试读取的下一帧序号。默认日志在状态切换时输出，可注入诊断回调供测试使用；回调异常不会终止生产线程。

本地容器测试与实机验收分开记录，见 [视频帧源验证记录](../../openspec/changes/implement-rk3576-video-frame-source/validation.md)。RK3576 已通过两种时间模式的慢消费与各 20 轮启停、同进程 100 次创建销毁，以及 600.187 秒内 318 轮重启/慢消费资源检查。未执行 Jetson、ROS、相机或模型质量验收。
