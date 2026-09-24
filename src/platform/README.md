# 平台工厂接入说明

`PlatformFactory` 是生产程序统一创建平台资源的入口。它创建帧源、帧创建器和整套跟踪依赖。公共算法只使用接口，不链接硬件实现。

生产构建默认使用 `DRONE_TRACKER_PLATFORM=jetson`，也支持显式选择 `rk3576` 的本地 MJPEG 视频与相机节点。未知值在硬件依赖发现前拒绝。RK3576 相机支持连续及软件触发；硬件解码和 RGA 图像加速仍未提供。可控模型仅在测试中注入，生产程序没有假模型选项。

## RK3576 SC233HGS 独立录像

录像默认关闭，仅支持 SC233HGS 连续采集。启用后从相机采集边界复制完整 NV12 画面，使用 Rockchip MPP 硬件编码 H.264，并写入独立的 MPEG-TS 分段。NV12 包含亮度和色度平面；录像保存两者，不从带框的 ROS 图像或 HDMI 画面取图。画面不增加裁剪、缩放、翻转或业务叠加。H.264 是有损压缩，解码像素不等同于算法编码前的原始像素。

在生产板匹配 SDK 的构建中增加 `-DDRONE_TRACKER_BUILD_RECORDING=ON`。该选项安装 `rk3576_recording_worker` 和 `rk3576_recording_inspect`；MPP 与 FFmpeg 封装库只链接录像程序。工作程序必须与 `drone_tracker_node` 安装在同一目录。不要替换系统 RKNN Runtime。仅需独立构建录像工具时使用：

```sh
cmake -S src/platform/rk3576/recording -B build/recording -DCMAKE_BUILD_TYPE=Release
cmake --build build/recording -j1
```

先为节点运行账号创建持久目录并授予写权限。目录必须是绝对路径，且实际挂载为本地 ext4；不能使用 tmpfs、ramfs 或网络文件系统。程序复核内核挂载信息，不能只凭路径名判断。目录父级必须已经存在；不要在启动、安装或升级脚本中清空历史录像。

生产相机 YAML 的启用示例（其余模型、相机与 OSD 参数沿用已核验的生产配置）：

```yaml
drone_tracker_node:
  ros__parameters:
    need_save: false
    recording:
      enabled: true
      directory: /var/lib/drone_tracker/recordings
      bitrate_bps: 12000000
      segment_seconds: 60
      sync_interval_ms: 1000
      max_total_bytes: 21474836480
      min_free_bytes: 5368709120
```

`bitrate_bps` 单位为位/秒；默认 12 Mbps 约产生每小时 5.4 GB 视频，实际值还受编码内容及封装开销影响。`segment_seconds` 可取 10–600 秒，到期后在下一张 IDR 帧切段。IDR 是不依赖前面图像的解码起点，各段独立保存必要参数。`sync_interval_ms` 只允许 500、1000 或 2000 毫秒，表示向文件系统发起数据同步的周期。它不是掉电损失上限。

`max_total_bytes` 默认 20 GiB，统计完成、未完成及附属文件；`min_free_bytes` 默认保留 5 GiB 磁盘空间。达到任一保护线时停止本轮录像并保留历史文件。系统不循环覆盖，也不会自动删除旧会话。操作人员应在任务间导出、核对并按自己的保留策略清理。

所有 `recording.*` 都是启动参数，运行期修改会被拒绝。新录像与 `need_save`、ROS bag、ROS 图像和 HDMI OSD 开关相互独立，不要求发现目标。相机输入仍须保持 `need_save=false`。视频输入、AR0234 和 Jetson 拒绝启用该录像能力。

ON 准备新会话，只有相机启动预热通过并完成驱动缓冲归还的合格帧才进入录像。预热观察帧不会补录。首个被录像接受的帧 PTS 为零；PTS 是视频中的显示时间，后续间隔来自真实采集时间，录像掉帧不会压缩时间轴。OFF 停止接收并排空已接受内容，正常退出会保存完成标记。预热中取消或失败保存零帧原因，不创建占位 `.ts`。关闭录像不会禁用相机预热。

每轮建立排他创建的 `session-…` 目录，包含以下证据：

- `session.json`：程序、模型、配置、板卡及实际图像格式身份。
- `000001.ts` 等：原始分段视频。
- `000001.frames.jsonl` 等：一行一个帧记录，关联采集身份、PTS、编码时刻和完整视频帧的结束字节位置。
- `events.jsonl`：驱动缺口、跟踪队列丢弃和录像支路丢弃，分别保留分类及序号区间。
- `commits.jsonl`：已同步视频和附属时间记录的提交边界。
- `camera.json`：预热、合格帧及终止统计。
- 段级和会话级 `complete.json`：已完成收尾的结果。会话仍须检查 `outcome` 与 `video_success`；零帧取消不表示存在有效视频。

缺少完成标记表示未完成，不能仅凭文件存在推断视频成功。`.pending` 是未完成发布的元数据，保留为故障证据。同步水位表示最后成功同步到文件系统的位置；尚未建立时日志显示未知。默认同步周期下，最新合格输入比水位落后超过两秒会报告退化。持续五秒没有同步进展或实际同步超时会结束本轮录像。录像故障通过日志与状态文本报告，不结束仍健康的跟踪；旧进程尚未回收时禁止下一轮复用其缓冲。

重启后可独立运行以下只读检查，报告路径必须尚不存在。检查与相机 ON 无依赖，可以单独安排执行：

```sh
rk3576_recording_inspect /var/lib/drone_tracker/recordings /var/lib/drone_tracker/inspection-001.jsonl
```

检查器保留原件，报告缺失或损坏的元数据、最后可解析提交及完整 TS 包前缀，并限制单行、文件数量、内存和报告大小。TS 包结构完整不代表成功解码；检查器不会解码、转换或重写视频。解析出提交记录也不能证明突然掉电前的同步调用已成功返回。通用解码验证属于独立验收，不改变生产回放入口。

回滚时先 OFF 并等待停止日志，正常退出节点，然后设置 `recording.enabled=false` 并重启。保留已有会话供检查。本功能不包含离线转换、回放解码扩展或循环覆盖。非断电测试不能承诺真实掉电的数据损失上限，也不能承诺硬件损坏后恢复。

## RK3576 独立 YOLO 使用

在 ARM64 环境使用 C++17、OpenCV（core、imgproc、dnn、video、imgcodecs、videoio）、线程库及 RKNN SDK。目标兼容性基线为 Runtime 2.3.2、驱动 0.9.8。本次沿用既有 2.3.0 构建 SDK（编译所用的头文件与链接库），产物在目标板 Runtime 2.3.2 上验证，不要求重建 SDK 镜像。此入口无需 ROS、CUDA、TensorRT 或 VPI。开发机的容器和挂载约定见仓库根目录 `AGENTS.local.md`；以下命令在该 ARM64 环境内执行，SDK 路径按实际安装位置填写：

```sh
cmake -S src/platform/rk3576 -B build/rk3576-yolo \
  -DRKNN_INCLUDE_DIR=/usr/include \
  -DRKNN_LIBRARY=/usr/lib/librknnrt.so -DCMAKE_BUILD_TYPE=Release
cmake --build build/rk3576-yolo -j4
ctest --test-dir build/rk3576-yolo --output-on-failure
```

模型必须是固定 640×640 灰度三通道输入、六输出的兼容 YOLO。三个尺度按 80、40、20 排列，每个尺度先输出 64 通道框分布，再输出 C 通道类别原始分数。C 是模型类别数。模型原生输入支持 INT8（八位整数）或 FP16（十六位浮点），布局均为 NHWC，即批次、高、宽、通道顺序。INT8 必须采用 AFFINE 仿射量化，零点 -128、比例 1/255。FP16 接受 NONE 或 AFFINE 标记，不套用整数 scale/zp 限制。

两类模型都必须采用 mean=0/std=255，即像素减零后除以 255。普通属性查询不能证明该配置，须由转换记录或模型元数据确认，并用固定输入数值对照验证。Float32 原生输入及任意 mean/std 配置不在支持范围。

程序始终提交 UINT8 NHWC `[1,640,640,3]`，共 1,228,800 字节，`pass_through=0` 表示由 Runtime 按模型配置转换。应用不提前除以 255、减 128 或改存 FP16。例如像素 150 仍提交为字节 150，模型需要的数值含义约为 150/255。FP16 属性报告的 2,457,600 字节是模型原生存储大小，不是提交缓冲大小。输出使用 `want_float=1` 请求 Float32，不重复反量化。

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

此入口编译真实公共跟踪器及可替换加载器的实际装配函数，不发现硬件开发库。测试没有真实模型或采集输入。平台选项测试检查默认 Jetson、未知值提前拒绝及 RK3576 缺失 RKNN 依赖的诊断；不执行 Jetson 编译。

此前 `introduce-platform-factory` 变更只提供公共测试和 Jetson 源码审查证据，详见 [平台工厂验证报告](../../openspec/changes/archive/2026-09-10-introduce-platform-factory/verification.md)。该阶段未配置、编译或运行 Jetson 程序，也未验证真实像素、设备资源安全或跟踪行为等价。

当前 `implement-rk3576-yolo-backend` 已补充 RK3576 真实模型、图像、录像和有限资源生命周期验证，详见 [实机记录](../../openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/board-validation.md) 和 [独立验证报告](../../openspec/changes/archive/2026-09-11-implement-rk3576-yolo-backend/verification.md)。这些结果不代表 Jetson 实机等价性或完整跟踪平台验收通过。

## RK3576 完整跟踪录像回放

新增独立入口 `rk3576_tracker_replay` 通过统一工厂创建四个模型文件对应的组件，并运行真实公共 `HybridTracker`。它从自动检测开始，无需人工首帧框。四文件分别为 YOLO、模板 Backbone、搜索 Backbone 和 Head；两个 Backbone 分别提取模板和搜索图像的特征，Head 比较两者。原有两个 probe 继续保留。

此入口使用上述独立 ARM64 CMake 构建，额外需要 OpenSSL Crypto 开发库，用于计算 SHA-256 文件内容指纹。公共算法和旧 probe 不链接 Crypto。根目录生产构建现已支持 Jetson 和 RK3576 ROS 视频；独立 RK3576 工厂拒绝相机请求，视频请求按下文的 MJPEG 帧源契约提供。原录像回放工具继续使用单独的同步解码器，不提供 ROS、相机采集或 RGA 加速。

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

`createFactory(...)->createFrameSource(config)` 现在支持 `FrameSourceType::Video`。创建时打开文件并检查首帧，随后关闭文件，返回对象尚未启动。`getImageSize()` 在启动前即可查询实际尺寸。每次 `start()`（包括第一次）重新打开当前路径并从首帧播放；文件被移走、不可读或尺寸变化时启动失败，恢复后可重试。视频忽略相机尺寸、fps、翻转和镜像设置；提交录像配置不会创建录像目录或输出文件。

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

## RK3576 ROS 视频节点

在满足依赖的 ARM64 ROS 2 Humble 环境中，从工作区根目录构建：

```bash
colcon build --packages-select drone_tracker --cmake-args \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_PLATFORM=rk3576 \
  -DDRONE_TRACKER_BUILD_TESTS=ON
source install/setup.bash
# 运行时库目录必须指向当前机器安装的 RKNN Runtime；开发板应使用 2.3.2。
export LD_LIBRARY_PATH=/path/to/rknn-runtime/lib:${LD_LIBRARY_PATH}
ctest --test-dir build/drone_tracker --output-on-failure
```

节点沿用 `vision_msgs`、`mavros_msgs`、`cv_bridge` 和 `rosbag2_cpp` 等 ROS 依赖。RKNN 编译依赖默认位于 `/opt/rknn-runtime/include` 和 `/opt/rknn-runtime/lib/librknnrt.so`，可用 `RKNN_INCLUDE_DIR`、`RKNN_LIBRARY` 显式覆盖。目标版本仍为 Runtime 2.3.2、驱动 0.9.8；现有 2.3.0 编译 SDK 不需要重建。开发机环境补充约定见 `AGENTS.local.md`。

先复制安装目录中的 `parameters/rk3576_video_params.yaml`，填写四个模型角色和视频内参。示例保留空模型路径和无效内参，以免误用后静默启动：

- `tracker.yolo_engine`：YOLO 检测模型。
- `tracker.nano_template_backbone`：模板图像特征提取模型。
- `tracker.nano_search_backbone`：搜索图像特征提取模型。
- `tracker.nano_head`：两组特征的匹配模型。
- `tracker.nano_backbone`：在 RK3576 必须为空。Jetson 继续使用原三路径默认值。

模型绝对路径直接使用；相对路径基于安装包的 `share/drone_tracker/models/`。无需复制大模型进仓库。目标类别从零开始，必须落在模型类别范围内。

`camera.fx/fy/cx/cy` 是实际视频像素坐标中的焦距和主点，用于旋转补偿和预测。四值必须有限，fx/fy 必须大于零。例如，某段 640×480 视频的标定可能为 `800.0/810.0/320.0/240.0`；这只是格式示例，必须替换成该录像对应的标定。节点不按 `mv_camera.*` 再缩放这组值。

```bash
ros2 launch drone_tracker rk3576_video.launch.py \
  params_file:=/absolute/path/video_params.yaml \
  video_source:=/absolute/path/recording.mov
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header "{frame_id: ON}"
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header "{frame_id: OFF}"
```

视频必须显式提供。相对视频路径基于启动进程的工作目录，因此推荐绝对路径。只接受本地 MJPEG，即逐帧 JPEG 编码的视频。节点初始化后等待 ON；ON 从头播放，重复 ON 不增开线程。OFF 主动停止，EOF（文件正常结束）自动收尾。收尾后可再次 ON，四模型复用，旧检测请求和结果清空。SIGINT 退出进程时等待设备不再访问帧后释放资源。

`use_history_timestamp` 和 `use_sim_time` 都必须为 false。帧时间采用系统起点加 PTS（视频帧显示时刻），等待采用单调时钟。输出图像保留帧时间，检测框使用当前预测时间。运行中普通超时继续等待，不会把长帧间隔误判为 EOF。历史姿态同步不在本阶段范围内；独立顺序回放和独立视频帧源的历史模式继续可用。

启动后不能修改输入、模型、内参、算法、记录或时钟配置。修改参数文件后重启节点。launch 只覆盖视频路径，不覆盖 DCF 等算法参数。DCF 是用于辅助跟踪的相关滤波器。

`need_save=true` 时，每轮在 `save_path` 下按触发时间创建独立 ROS bag（ROS 消息记录文件）。目录碰撞或打开失败会拒绝本轮；写入失败结束本轮。姿态、里程计、检测框及开启的图像输出按原话题和时间记录。视频输入不创建额外相机录像。OFF、EOF 和普通错误均关闭记录；恢复文件或目录后可重试。

ROS 视频接入已完成规定的 RK3576 实机验收，包括纯 FP16 和纯 INT8 模型配置的基础集成、失败清理，以及固定 INT8 配置下同进程 20 轮启停和额外至少 30 分钟播放负载。验证证据与缺口见 [ROS 视频接入验证记录](../../openspec/changes/integrate-rk3576-ros-video/validation.md)。这些结果不代表模型检测质量、部署性能或 Jetson 实机等价性通过；NPU 设备内存尚未测量。前序完整回放的 7.2、7.3 仍未完成。

## RK3576 独立原生灰度相机源

`implement-rk3576-camera-frame-source` 提供 AR0234 连续采集的独立入口。输出为原尺寸 `Gray8`，即每个像素一个八位灰度值。它通过 V4L2（Linux 视频设备接口）采集，不加载模型，不使用显示链。后续 ROS 相机变更已增加周期软件触发和正式入口，见下节。

### 配置与接口

必须显式提供采集节点、媒体设备、传感器实体、宽高和整数帧率。传感器实体是媒体拓扑中标识传感器的名称。下面的路径和实体名仅展示配置格式，使用前必须根据当前连接的相机替换：

```cpp
#include "platform/rk3576/platform_factory.h"

FrameSourceConfig config{};
config.type = FrameSourceType::Camera;
config.path = "/dev/videoX";                     // 当前采集节点
config.camera_media_device = "/dev/mediaX";     // 当前媒体设备
config.camera_sensor_entity = "actual sensor"; // 当前传感器实体
config.width = 1920;
config.height = 1200;
config.fps = 60;
config.trigger_mode = false;                   // 连续采集；软件触发改为 true
config.camera_trigger_rate_hz = 0;              // 软件触发须显式填写 1..fps 的频率
config.output_width = 0;                       // 0 表示保留原尺寸
config.output_height = 0;
config.camera_buffer_count = 4;                // 允许 2–32

rk3576::FactoryOptions options{};
options.enable_camera = true;
auto factory = rk3576::createFactory({}, options);
auto result = factory->createFrameSource(config);
// 检查 result；失败时 result.error.describe() 给出平台、设备路径和原因。
// 成功时通过 result.value 的 start()/waitFrame()/stop() 操作。
```

输出尺寸也可显式填为输入尺寸。外部引脚触发、缩放、翻转、镜像和历史时间均被拒绝。实体名不能包含引号或控制字符。`configureRecording(true, ...)` 抛出 `std::invalid_argument`，表示不支持录像；禁用录像安全接受。工具保存的少量静态快照不属于录像功能。

工厂创建阶段只设置传感器并核验设备格式，随后恢复本次修改的控制项并关闭设备。此时不申请采集缓冲、不启动线程。每次 `start()` 重新打开设备，并核对实际的灰度格式、尺寸、帧间隔和行跨度。行跨度是相邻两行起点之间的字节数。例如输入宽 1920、行跨度 2048 时，每行只复制前 1920 字节，输出不含 128 字节填充。

每个输出帧都有独立像素存储。设备缓冲在复制后立即归还。消费者持有的帧及只读视图在源停止或销毁后仍有效。缓冲索引、平面数量、偏移、使用长度和错误标志不合法时，本轮失败，不交付该帧。

### 启停、队列和时间

调用方须串行执行 `start()`、`stop()`。启动和销毁前，消费者必须停止取帧；`stop()` 可以与一个消费者并发执行。队列仍为 `BlockingReaderWriterQueue<FrameRef>(30)`，使用不扩容的 `try_enqueue()`；实际容量由该队列实现决定。满队列丢弃新帧，生产线程不取走旧帧。

`tryFrame()` 非阻塞取帧；`waitFrame()` 限时等待。返回 false 时，两者都会清空输出引用。需要最新帧时，可用已有 `waitForLatestFrame()` 排空旧帧。停止保留本轮已入队的尾帧；下一次启动清除旧队列。`isRunning()==false` 不代表队列为空，应结合 `status()` 查询结束原因。相机不会返回文件 `Eof` 状态。

停止事件通过 `eventfd` 唤醒 `poll`。前者是可被等待线程检测到的事件句柄，后者是等待设备可读的系统调用。连续两秒没有成功出队帧会报告采集超时。停流失败时先关闭采集设备，再解除映射；错误在清理后保留。无设备测试的停止上限是 250 ms，实机要求是 1 秒；开发板 2 本次 122 轮最大停止为 60.961 ms。

每轮用单调时钟的前后两个样本夹取系统时间，以中点计算固定偏移。单调时钟不随系统校时跳变。本轮不会重新计算偏移：

- `driver_monotonic`：驱动明确声明时间属于单调时钟，输出时间为驱动时间加固定偏移。
- `dequeue_fallback`：驱动没有声明时钟域，或使用 COPY 标志。输出采用成功出队时采样的单调时间加固定偏移，仅代表出队时刻。

例如，驱动时间为 `10000000` 微秒，偏移为 `1700000000000000` 微秒，输出为 `1700000010000000` 微秒。重复、倒退、溢出、非法原始时间或轮内策略变化均使本轮失败，不人工增加时间。原始 flags 和时间会保留；SOE 表示驱动声明曝光开始，EOF 表示帧结束，本次没有验证它们与真实曝光的同步精度。

### 不依赖设备 SDK 的构建

在 Linux 环境、仓库根目录执行：

```bash
cmake -S src/platform/rk3576 -B build/camera-only \
  -DDRONE_TRACKER_CAMERA_ONLY=ON -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-only -j4
ctest --test-dir build/camera-only --output-on-failure
```

该入口需要 C++17、线程库、OpenCV，以及 JPEG、libavformat、libavcodec、libavutil 开发库。保留视频依赖是为了复用同一个公共平台工厂及其视频功能。该入口不查找 RKNN、ROS、RGA、DRM 或 Jetson SDK。运行相机时还需要 `media-ctl` 配置工具、可用驱动，以及对显式配置设备的访问权限。构建完整 RK3576 后端仍使用原入口，不添加 `DRONE_TRACKER_CAMERA_ONLY`。

本机按 `AGENTS.local.md` 使用临时 ARM64 容器。测试不访问真实相机。构建所得 `ar0234_camera_source_driver` 是 Linux ARM64 程序，不能直接在 macOS 上执行。

### 独立工具与实机验收命令

以下命令用于连接相机后的实机验收。开发板 2 已完成短采集、20 轮启停、100 次新建销毁和 600 秒慢消费负载，详见 [实机验收记录](../../openspec/changes/implement-rk3576-camera-frame-source/board2-validation.md)。先核对实际拓扑和设备占用，再填写参数。不要根据历史节点编号自动猜测设备，不要停止其他占用程序。

```bash
v4l2-ctl --list-devices
# 用实际媒体设备替换以下值，再查看其中的传感器实体。
MEDIA_DEVICE=/dev/mediaX
media-ctl -d "$MEDIA_DEVICE" -p
VIDEO_DEVICE=/dev/videoX
SENSOR_ENTITY='actual sensor'
fuser "$VIDEO_DEVICE"
```

短采集示例使用原尺寸连续灰度配置，保存三张无损 PGM 快照。PGM 是可直接保存原始八位灰度的图像格式。`--compare-pixels` 在同一个驱动缓冲仍由本进程持有时，逐行比较输出副本和原始有效像素；不比较两次独立采集的动态画面。

```bash
./ar0234_camera_source_driver \
  --video-device "$VIDEO_DEVICE" --media-device "$MEDIA_DEVICE" \
  --sensor-entity "$SENSOR_ENTITY" --width 1920 --height 1200 --fps 60 \
  --buffers 4 --frames 300 --snapshots 3 --compare-pixels \
  --output camera-short-new
```

输出目录必须不存在，其父目录必须已存在。参数错误返回退出码 2；设备、报告写入、像素对照、无帧或停止超时失败返回 1；成功返回 0。SIGINT/SIGTERM 会请求停止并释放资源，随后以非零状态退出。工具不会覆盖旧结果，写入失败也不会报成功。

重复与负载验收可复用上述设备参数，分别选择以下参数组合，并为每次运行使用新输出目录：

| 目的 | 参数组合 |
|---|---|
| 同一个源至少 20 轮启停 | `--rounds 20 --frames 300 --snapshots 0` |
| 同进程至少 100 次新建销毁 | `--instances 100 --frames 10 --snapshots 0` |
| 连续至少 10 分钟 | `--seconds 600 --snapshots 0` |
| 慢消费者负载 | `--seconds 600 --slow-ms 60 --snapshots 0` |

`--frames` 限制消费者取得的帧数。`--seconds` 限制每轮时长，两者都给出时先达到者生效。`--slow-ms` 在每次消费尝试后等待指定毫秒数，不改变采集帧率。多轮时每轮都应用这些限制。

报告内容：

- `config.txt`：完整请求配置、时间含义及资源统计口径。
- `frames.csv`：所有成功复制并归还的帧，包括被队列丢弃的新帧。包含原始时间、sequence（驱动序号）、flags、时间策略、固定偏移及采样跨度、实际宽高和行跨度、复制耗时、像素对照结果及两类丢帧统计。
- `rounds.csv`：每轮消费/采集数量、以整个轮次耗时计算的实测吞吐量、停止和设备清理耗时、状态及原因。帧率是测量值，不沿用历史 60 FPS。
- `resources.csv`：创建前、运行中约每秒、停止后和销毁后的主机内存、线程与文件描述符数量。文件描述符是进程打开资源的句柄；统计包含报告文件和查询本身，不包含 NPU 设备内存。
- `frame-*.pgm`：每轮开头的少量无损灰度快照，数量由 `--snapshots` 决定。

`Ar0234CameraDependencies::observe` 在生产线程同步接收逐帧报告，回调抛异常会使本轮失败。报告与快照写入会影响测得吞吐量，评估时应保留对应配置。`diagnostic()` 只读快照另外提供停止及清理耗时；`status()` 提供公共状态和队列统计，不消费帧。

无设备测试、构建及已完成的开发板 2 实机验收见 [相机帧源验证记录](../../openspec/changes/implement-rk3576-camera-frame-source/validation.md)。这些结果不代表 ROS 相机、模型质量、曝光同步、Jetson 等价性或部署性能达标。实拍画面偏暗、模糊，仍需另行检查图像质量。


## RK3576 ROS 相机节点与软件触发

正式入口复用独立相机后端。连续模式由相机自行出帧；软件触发模式由独立线程周期发送拍摄命令。命令线程不读取图像，图像队列仍只有一个采集生产者。ROS 的 ON 表示开始整轮运行，不是单次拍照；OFF 结束本轮，随后 ON 复用已加载模型。

### 填写配置并启动

1. 复制 `parameters/rk3576_camera_params.yaml` 到部署目录。
2. 根据 `media-ctl -p` 核对媒体设备、传感器实体、采集设备与尺寸。示例实体来自历史连接，重新接线后须复核。
3. 填写四个模型文件路径：YOLO、模板 Backbone、搜索 Backbone、Head。Backbone 提取图像特征，Head 匹配模板和搜索特征。共享 `nano_backbone` 留空。
4. 核对 `camera.fx/fy/cx/cy`，分别为像素焦距和主点坐标。当前按用户确认，对 1920×1200 图像暂用模拟值 `1000.0/1000.0/960.0/600.0`：焦距人为选定，主点暂取图像中心。可用来继续接入测试，但不代表镜头标定或旋转补偿精度通过。正式标定后替换四值并重启节点。节点不会再次缩放这些值；缺失或 `.nan` 仍会被拒绝。
5. 在已构建和加载 ROS 包环境的目标系统执行以下命令。首次启动完成初始化后等待 ON。

```bash
ros2 launch drone_tracker rk3576_camera.launch.py \
  params_file:=/absolute/path/camera.yaml video_source:=video0
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header '{frame_id: ON}'
ros2 topic pub --once /fpv/tracker_trigger std_msgs/msg/Header '{frame_id: OFF}'
```

`video_source` 支持 `video10` 等多位设备号和 `/dev/video10` 等绝对设备路径。launch 只覆盖输入路径，算法与模式来自参数文件。初始化核验采集能力；设备恢复后，再次 ON 会重新打开和核验设备。

| 参数 | 约束 |
|---|---|
| `mv_camera.capture_mode` | 必填 `continuous` 或 `software_trigger` |
| `mv_camera.fps` | 正整数设备帧率，独立于命令频率 |
| `mv_camera.trigger_rate_hz` | 软件模式为有限的 1 至 fps；连续模式为 0.0 |
| `mv_camera.media_device`、`sensor_entity` | 显式媒体节点及其传感器实体 |
| `mv_camera.buffer_count` | 2–32，默认 4 |
| `mv_camera.width_px/height_px` | 实际输入像素尺寸 |
| `mv_camera.output_width_px/output_height_px` | 与输入相同或为 0，0 表示不缩放 |
| `need_save`、`use_sim_time`、`use_history_timestamp` | 均为 false |

20 Hz 已在开发板 2 通过新实现的三轮有限计数、20 轮启停、100 次新建销毁、10 分钟慢消费，以及同一 ROS 节点 20 轮启停后额外 30 分钟负载。该结论针对本次设备和配置，不代表相机硬件最高频率。30/60 Hz 新实现测试的命令数与帧数仍不一致，未归因，不能宣称支持一触发一帧。完整环境、模型和限制见 [最终实机记录](../../openspec/changes/integrate-rk3576-ros-camera/final-board-validation.md)。代码不会因推理慢或缺帧自动降低频率。改为连续模式时必须同时设置 `trigger_rate_hz: 0.0`。运行期修改模式、频率、内参、模型等启动配置会被拒绝，修改文件后须重启节点。

### 独立有限触发工具

在上文的独立 ARM64 构建完成后，使用 `ar0234_camera_source_driver`，无需加载模型。下面的命令只作为后续实机步骤，本次未执行：

```bash
./ar0234_camera_source_driver \
  --video-device /dev/video0 --media-device /dev/media0 \
  --sensor-entity 'm00_b_mvcam 4-003b' --width 1920 --height 1200 --fps 60 \
  --mode software_trigger --trigger-hz 20 --commands 60 --tail-ms 500 \
  --snapshots 0 --output camera-trigger-new
```

`--commands 0` 验证无触发，`--commands 1` 验证单发；其余正整数为每轮最多发送的命令数。达到命令上限后停止发送，采集继续等待 `--tail-ms` 毫秒尾帧，再关闭设备。尾帧等待范围为 0–1500 ms，避免与两秒无帧故障阈值冲突。`--seconds` 在有限模式下还可指定整轮工具超时；未完成时超时明确失败。输出目录必须不存在。原连续工具调用仍可使用，不传 `--mode` 时为连续模式。

`triggers.csv` 独立记录每次命令调用前后单调时间、调用成功与错误。`frames.csv` 保留驱动时间、主机出队时间、固定系统时间偏移和独立图像帧时间。`clock_offset_delta_us` 记录当前系统时钟偏移与本轮固定偏移的差，可用于发现校时跳变，不据此重标帧时间。这些时间不代表已证明的曝光时刻，也不按先后顺序强行配对。例如 120 个成功命令只返回 60 帧时，报告数量差 60，不据此判定是相机拒绝还是传输损失。

`rounds.csv` 分别记录调用错误、计划周期跳过、驱动序号缺口、队列丢新帧及消费数量。还包含停止发送时帧数、尾帧数、停止发送及关闭时未确认请求数、工具超时和控制恢复结果。连续模式的命令数为零，命令与帧数差异不用于评价连续模式。有限计数验收须结合原始逐帧报告，区分起始帧、稳定阶段和尾部；零退出码本身不证明一触发一帧。

### 故障与恢复

每轮先保存 `trigger_mode`、`trigger_src`、`frame_rate` 的可读原值，再配置设备。读取失败时不修改控制。初始化验证结束、部分失败及正常停止均尝试恢复；恢复错误保留控制名称及原因，未恢复项可以在再次停止时重试。恢复仅针对本次控制项，不代表整个媒体拓扑已恢复。

设置和恢复控制值后，每项最多等待 100 毫秒，每隔 5 毫秒读回一次，首次一致即继续。等待期间不会重复写入。超时仍不一致时报告设备、控制名、预期值、实际读回值和等待耗时；系统调用本身报错仍按设备错误处理。该时间限制不保证能中断阻塞的驱动调用。

软件命令使用已核实控制接口，直接调用 V4L2，不为每帧启动 shell。周期按本轮起点安排，落后时跳过过期周期，不突发补发。OFF 或工作线程故障会唤醒等待；生命周期所有者先回收命令和采集线程，再停流、恢复、关闭。驱动调用若无法返回，不能保证停止期限，须判为实机失败，不能强制关闭在途句柄。

首帧或后续连续两秒无成功出队为采集故障，短暂等待不等于文件结束。ROS 每秒最多输出一次常规采集汇总，终态另输出完整统计。相机停止耗时与包含模型等待的节点停止耗时分别报告。输入故障和 OFF 不伪造算法的 TargetLost 状态。

相机 `need_save=true` 在初始化时失败，不创建部分 ROS bag 或录像。视频及 Jetson 记录行为保持不变。图像使用原帧时间，预测框使用当前预测时间；六个原有输入输出话题及状态数值保持不变。

本变更的构建、无设备测试及暂缓实机项目见 [ROS 相机实施记录](../../openspec/changes/integrate-rk3576-ros-camera/validation.md)。历史连续相机和 shell 触发记录不能替代新实现验收；模型质量、曝光同步、Jetson 等价性及前序回放 7.2、7.3 的结论保持不变。

### SC233HGS 启动时间戳预热

SC233HGS 每次成功开流后，先观察驱动时间，再交付图像。固定规则为：

- 开流后的单调时钟期限为 2 秒，最多检查 120 个成功出队缓冲。
- 连续 8 帧时间严格递增才开始输出。第 8 帧就是首个合格帧。
- 预热时允许相等时间，但会将连续计数重置为 1。此前观察帧归还驱动，不复制灰度图像，也不进入跟踪队列。
- 到期优先于就绪；第 120 帧若在期限内达到连续条件，允许成功。否则归还后失败，不读取第 121 个观察帧。
- OFF 可唤醒等待；已观察到停止请求时优先停止。新轮重新计时和计数，模型跨轮复用。

例如时间为 `100,100,117,117,134,151,168,185,202,219,236` 微秒时，前十帧被丢弃，第十一帧输出 `236 + 本轮固定偏移`。预热结束不重新采样偏移，不按帧率补时间，也不对重复时间人工加一。未知时钟域继续使用既有 `dequeue_fallback`：时间仅表示主机取得帧的时刻。AR0234 不启用此观察期。

预热只判断时间连续递增，不证明曝光、亮度、对焦或驱动内部问题已经修复。正常输出阶段的重复，以及任意阶段的倒退、非法时间、溢出、时钟策略改变、序号重复或倒退，仍终止本轮。向前序号缺口单独计数，不重置连续计数。错误区分采集无帧超时、预热时间超限、预热检查量超限和致命元数据错误。

节点每秒最多一次输出常规汇总，阶段转换和结束另行报告。`first=unknown` 表示尚未交付首帧。首帧延迟从成功开流后的单调时刻计量，不含模型初始化和开流前配置时间。`CameraCaptureStatus` 是普通 C++ 诊断快照，其中：

- `dequeued_total` 是全部成功出队，`warmup_examined` 是预热检查量。
- `warmup_discarded` 是已成功归还的观察帧，`terminal_discarded` 是因失败或取消未交付的出队帧。
- `read` 是合格帧，`enqueued` 是成功入队，`dropped` 是队列满丢弃。序号缺口不属于出队量。
- 清理后应满足 `dequeued_total = warmup_discarded + read + terminal_discarded`，以及 `read = enqueued + dropped`。
- `resources_reclaimed=false` 表示清理尚未确认成功，不能只凭计数对账宣称设备已释放。

独立工具保留原名称 `ar0234_camera_source_driver`，通过 `--sensor-model sc233hgs` 选择型号。它经公共帧源接口采集，不依赖模型或显示。新增 `warmup.csv` 保存每轮最多 120 条原始元数据，在轮次结束后统一导出；新增 `capture.csv` 保存预热汇总、首帧身份和资源回收状态。没有首帧时使用 `unknown`。原 `frames.csv` 仅保存合格帧，包含队列满丢弃帧。

先按本地约定复核设备和占用，再使用实际节点运行。例如本次生产板确认的节点为 `/dev/video22`：

```bash
./ar0234_camera_source_driver --sensor-model sc233hgs \
  --video-device /dev/video22 --media-device /dev/media2 \
  --sensor-entity 'm00_b_sc233hgs 0-0030' \
  --width 1920 --height 1200 --fps 60 \
  --frames 60 --rounds 20 --output warmup-rounds
```

不要用启动短测替代 100 次新建销毁、20 分钟持续采集或正式模型 ROS 验收。此变更不实施录像；未来录像分流只能位于同一预热合格边界之后。部署与回滚范围是应用程序，系统 RKNN Runtime 保持不变；回滚旧程序仍会恢复其启动重复时间失败限制。
