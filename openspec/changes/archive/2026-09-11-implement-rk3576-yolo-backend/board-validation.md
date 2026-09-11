# RK3576 YOLO 实机与交付验证记录

日期：2026-09-10。变更：`implement-rk3576-yolo-backend`。本记录补充 [任务 1.1–5.6 的验证记录](implementation-validation.md)，不替代其中的公共测试证据。

任务 6.1–7.3 已完成。补充查找官方原视频后，任务 6.2 的录像部分也已通过，整个 change 为 26/26。尚未进入独立 verify、规格同步或归档。未执行 Jetson、完整跟踪、相机或性能验收。

## 平台与文件身份

目标为真实 RK3576，Linux 6.1.141、aarch64。C++ 查询报告 Runtime `2.3.0 (c949ad889d@2024-11-07T11:35:33)`、驱动 `0.9.8`。编译使用约定的 ARM64 临时容器镜像，未重建镜像。OpenCV C++ 为 4.5.4；板上数值参考使用既有 RKNN Python 环境，NumPy 1.26.4、OpenCV Python 4.11.0、ONNX Runtime 1.16.0、RKNNLite 2.3.0。板上未发现 conda，因此使用已有模型验证环境。

SHA-256 是文件内容校验标识，用于确保对照使用同一文件，不是模型加载白名单。

| 文件 | SHA-256 |
| --- | --- |
| `yolov8n-model_zoo-int8_fp16-multi_cls.rknn` | `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3` |
| 原模型 `yolov8n_multi_cls.onnx` | `27c8b84be4d92f08aa2132fda5c07602e70e88b524b3e4b2b8ee311a2e8e19f1` |
| 同源六输出 `yolov8_model_zoo.onnx` | `7780c4b35c8958dad8c8b27b267e1e6201c52097aeee6b3613b100fdd317586a` |
| 图像 `V_DRONE_051_frame_000105.jpg` | `1c6dda59cbfdb441031dd3a1166a2359bfeaad3565e612ccd1afffd28db694a6` |
| C++ 图像预处理输入 `image.input.u8` | `478d8ca3142f1adbed4d07f8e256d0964435671b3ff6fda281405b15477029a6` |

板上模型位于 `/home/firefly/drone_tracker_test/models/`，原 ONNX 位于 `/home/firefly/Desktop/models/`，图像位于 `/home/firefly/Desktop/drone_yolo_dataset/images/test/`。六输出 ONNX 来自 `/home/firefly/benchmarks/drone_model_recording_runs/yolov8n_multi_cls_4_models_test_20260904/artifacts/`。该转换目录内的 `models/rknn_zoo_int8_fp16_cls.rknn` 与本次加载的 RKNN 哈希相同，配置记录原 ONNX、mean=0/std=255 和目标 rk3576；由此关联转换来源。

工具、完整输入和原始张量保留在板上 `/home/firefly/drone_tracker_test/yolo_acceptance_20260910/`。模型和大型张量未加入源码。小型查询日志与结果摘要保存于本目录的 [evidence](evidence/)。

## 6.1 常量输入与契约

C++ `query` 与 `region` 模式都成功。输入为 `[1,640,640,3]`、INT8 NHWC，零点 -128、比例约 0.00392157。实际提交 UINT8 像素。NHWC 表示批次、高、宽、通道；NCHW 表示批次、通道、高、宽。输出原生属性如下：

| 索引 | 形状 | 类型 | 零点 | 比例 | 原生字节数 |
| --- | --- | --- | --- | --- | --- |
| 0 | `[1,64,80,80]` | INT8 | 43 | 0.525238 | 409600 |
| 1 | `[1,4,80,80]` | FP16 | 0 | 1 | 51200 |
| 2 | `[1,64,40,40]` | INT8 | 91 | 0.995673 | 102400 |
| 3 | `[1,4,40,40]` | FP16 | 0 | 1 | 12800 |
| 4 | `[1,64,20,20]` | INT8 | -12 | 0.368377 | 25600 |
| 5 | `[1,4,20,20]` | FP16 | 0 | 1 | 3200 |

输出均为 NCHW。完整节点名称及属性见 [constant-0.log](evidence/constant-0.log)。FP16 按浮点语义读取，不再应用整数反量化。三种输入 0、114、255 的六份 Float32 输出和合并预测均为有限值。与 design 的探索模型身份和属性一致。

## 6.2 真实图像与官方原视频

使用上述 640×512 图像，工具先转灰度，再上下各填充 64 像素，填充值 114。无需缩放。对照程序直接读取 C++ 导出的输入字节，避免把不同预处理误差混入推理比较。

- 常量和真实图像共 24 份 C++ 原始输出，与 RKNNLite 逐元素最大绝对差均为 0。
- 同一 RKNN 输入采用 `atol=1e-5, rtol=1e-5`；所有元素通过。容差定义为 `|实际-参考| <= atol + rtol*|参考|`。
- 独立双精度数学参考与实际 C++ 合并预测通过 `atol=1e-4, rtol=1e-5`。图像最大绝对差为 `3.049435451885074e-05`。
- 真实异步检测器使用目标编号 2，连续三次独立执行结果完全一致：帧号 1、命中、class=2、score=0.53206、框 `[26.1902 x 21.1553 from (413.811,260.84)]`。
- 独立目标类后处理读取第 6 通道，以 0.5 分数阈值、0.5 NMS 阈值和 0.8 宽高比例限制筛选。NMS 用于去除高度重叠框。3 个目标候选保留 1 个，参考分数 `0.532060444355011`，参考框 x/y/w/h 为 `413.81146907806396 / 260.8403081893921 / 26.19017219543457 / 21.15526008605957`，与工具显示精度一致。没有借用旧多类别 NMS 结果。

### 原视频来源与补充验收

根据板上 `V_DRONE_051_frame_*.jpg` 的序列名称，找到公开数据集 [DroneDetectionThesis/Drone-detection-dataset](https://github.com/DroneDetectionThesis/Drone-detection-dataset)。其四个类别和可见光序列命名与板上数据一致。使用该仓库提交 `442d0c6750708cc221636f7a1fd31cbd8cb0aeb8` 下的 [V_DRONE_051.mp4](https://github.com/DroneDetectionThesis/Drone-detection-dataset/blob/442d0c6750708cc221636f7a1fd31cbd8cb0aeb8/Data/Video_V/V_DRONE_051.mp4)；未将已压缩 JPEG 拼接成录像。现有 JPEG 与原视频解码帧不是逐像素相同的验收输入，因此两者分别记录结果，不把分数差异当成重复运行不一致。

原片为 918247 字节，SHA-256 为 `01c49506366d2b8f273df4951bc8546e2f0b5920203f815589950d90aa5b53d7`。OpenCV 查询为 640×512、30 帧/秒、309 帧，约 10.3 秒。原片保存在板上验收目录，不提交源码。

两次真实异步视频回放均完整返回帧号 1～309，全部结果文本逐帧一致。两次均有 159 帧命中类别 2、150 帧未命中。命中率不是准确率，本次不评估数据集精度。没有配置 ROI，使用原有全图、切片级联。

使用 [video_validation.py](video_validation.py) 抽取第 1、105、309 帧，保存无损 PNG 并记录哈希。每张 PNG 的独立检测结果与视频对应帧一致。对这三帧各自的全图灰度输入比较六份 C++ 与 RKNNLite 输出：18 份张量最大绝对差全部为 0。实际 C++ 适配与独立双精度参考的最大绝对差分别为 `3.051388921448961e-05`、`3.0497667921736138e-05`、`3.0507860174111556e-05`，在既定数学容差内超差元素均为 0。

前两帧全图命中，按目标通道分数和现有 NMS 独立复核框坐标；末帧为未命中，视频与独立检测都返回无目标。框坐标按工具有限打印精度用 `atol=1e-3, rtol=1e-5` 比较，分数打印差小于 `1e-5`；此显示层检查没有改变原始张量或适配层容差。

日志见 [第一次回放](evidence/video-run-1.log)、[第二次回放](evidence/video-run-2.log) 和 [逐层统计](evidence/video-validation.json)。原录像直接从官方地址下载后使用，无需用户另行提供样本。任务 6.2 已取得所需证据。

复现录像验收时，在板上验证目录放置原片及 `video_validation.py`、`compare_outputs.py`。沿用下文 MODEL、OUT 变量：

```sh
for run in 1 2; do
  timeout 240 "$OUT/rk3576_yolo_probe" --model "$MODEL" --mode detect \
    --target 2 --video "$OUT/V_DRONE_051.mp4" > "$OUT/video-run-$run.log" 2>&1
done
/home/firefly/venvs/rknn-toolkit2-2.3.0/bin/python "$OUT/video_validation.py" "$OUT" "$MODEL"
```

240 秒只用于限制测试进程的最长运行时间，不是延迟性能门槛。

## 6.3 同源 ONNX 数学转换

ONNX 是浮点模型格式。本次将同一 C++ 灰度输入转为 Float32 NCHW 并除以 255，分别运行原合并输出模型与同源六输出模型。使用 [adapter_dump.cpp](adapter_dump.cpp) 将六份浮点输出交给实际 `adaptYoloSixOutputs`，而非仅验证另一份公式实现。

实际 C++ 转换对原合并输出的最大绝对差为 `9.1552734375e-05`，在 `atol=1e-4, rtol=1e-5` 下超差元素为 0。双精度独立参考同样通过。完整统计见 [comparison.json](evidence/comparison.json)。

量化差异单独观察：RKNN 合并预测与原 ONNX 在全部候选上的框坐标最大差为 `91.042236328125`，类别概率最大差为 `0.060575932264328`。这些统计含低分候选，不是最终目标框误差或精度指标。原始 C API/RKNNLite 对照一致，且两组数学适配均通过，因此不把此差异归因于 C++ 适配层。未设置或放宽量化精度门槛。

## 6.4 有限资源与停止

[board_lifecycle.cpp](board_lifecycle.cpp) 使用真实模型与固定图像，完成：

- 30 次加载/销毁，每次 10 次推理，共 300 次串行推理。
- 每次先提交一个空字节输入，确认拒绝后仍可正常推理，共 30 次可恢复输入错误。
- 保存第一次输出，后续推理并销毁 Runtime 后检查保存值仍然有效。
- 10 次真实检测器加载；SDK 包装仅记录进入实际 `rknn_run` 的时刻，主线程随后调用 stop，再重复 stop。没有替换推理结果或故意延长执行。
- 每轮对象析构后读取 `/proc/self/status` 和 `/proc/self/fd`，记录驻留内存、线程数和文件描述符数；120 秒外部超时作为挂起保护。

所有循环完成，无挂起或重复释放错误。首次 SDK 初始化后 fd 从 5 变为 6，此后稳定为 6；每轮结束线程数均为 1。串行轮次预热后驻留内存在 57688～57692 kB，停止轮次稳定为 60496 kB。初次初始化与线程使用造成一次性增长，没有持续增长迹象。日志见 [lifecycle.log](evidence/lifecycle.log)。本检查不能证明任意长时间运行都无泄漏，也没有直接测量设备分配；它满足本任务有限运行、停止和资源趋势检查，未采集吞吐或延迟指标。

## 复现命令

先按 [使用说明](../../../../src/platform/README.md) 在约定 ARM64 容器构建独立工程。以下辅助编译在同一容器、独立工程构建目录执行，`REPO` 指容器内仓库路径，`SDK` 指 RKNN SDK 目录。遵守 `AGENTS.local.md` 的 `--rm` 与 build/install/log 挂载规则。

```sh
REPO=/work/ros2_ws/src/drone_tracker
SDK="$REPO/.docker-artifacts/rknn"
CHANGE="$REPO/openspec/changes/implement-rk3576-yolo-backend"
c++ -std=c++17 -I"$REPO/src" -I"$REPO/src/hybrid_tracker" \
  -I"$REPO/src/platform/rk3576" -I"$SDK/include" -I/usr/include/opencv4 \
  "$CHANGE/board_lifecycle.cpp" -o board_lifecycle \
  librk3576_yolo_sdk.a librk3576_yolo_runtime.a librk3576_yolo_host.a \
  common/libhybrid_tracker.a common/libmodel_common.a common/libpreprocess_geometry.a \
  "$SDK/lib/librknnrt.so" -lopencv_video -lopencv_dnn -lopencv_imgcodecs \
  -lopencv_imgproc -lopencv_core -pthread
c++ -std=c++17 -I"$REPO/src" -I"$REPO/src/hybrid_tracker" \
  -I"$REPO/src/hybrid_tracker/tests" -I/usr/include/opencv4 \
  "$CHANGE/adapter_dump.cpp" -o adapter_dump common/libmodel_common.a \
  common/libpreprocess_geometry.a -lopencv_core
```

将两个辅助程序、probe 及 `compare_outputs.py` 放入板上验证目录。在板上执行：

```sh
OUT=/home/firefly/drone_tracker_test/yolo_acceptance_20260910
MODEL=/home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn
IMAGE=/home/firefly/Desktop/drone_yolo_dataset/images/test/V_DRONE_051_frame_000105.jpg
ORIGINAL=/home/firefly/Desktop/models/yolov8n_multi_cls.onnx
SIX=/home/firefly/benchmarks/drone_model_recording_runs/yolov8n_multi_cls_4_models_test_20260904/artifacts/yolov8_model_zoo.onnx
"$OUT/rk3576_yolo_probe" --model "$MODEL" --mode query
for value in 0 114 255; do
  "$OUT/rk3576_yolo_probe" --model "$MODEL" --mode region \
    --constant "$value" --dump "$OUT/constant-$value"
done
"$OUT/rk3576_yolo_probe" --model "$MODEL" --mode region --image "$IMAGE" --dump "$OUT/image"
"$OUT/rk3576_yolo_probe" --model "$MODEL" --mode detect --target 2 --image "$IMAGE"
/home/firefly/venvs/rknn-toolkit2-2.3.0/bin/python "$OUT/compare_outputs.py" \
  "$OUT" "$MODEL" "$ORIGINAL" "$SIX"
timeout 120 "$OUT/board_lifecycle" "$MODEL" "$IMAGE"
```

RKNNLite 查询静态模型动态范围时有告警；实际静态推理成功，未改变模型或忽略数值失败。

## 公共测试、静态检查与规划核对

此前独立 ARM64 工程 11/11、公共入口 8/8 通过，包含原平台选择拒绝测试；命令与隔离证据见 `implementation-validation.md`。本轮未修改生产 C++，新增两个验收辅助程序均在约定 ARM64 容器编译并在板上执行。对两者运行本机 clang-tidy 的 `clang-analyzer-*` 无诊断，clangd `--check --tweaks=none` 均为 0 errors。主机静态检查不替代硬件验证。

| 规划文件 | 实现与证据核对 |
| --- | --- |
| proposal | 范围限独立 YOLO；精确类别已实现；完整工厂与性能不宣称完成 |
| design | 固定结构及输入、输出转换、实际容差、同步执行与独立构建均有对应证据；原视频回放及抽帧数值对照已补齐 |
| model-backends delta spec | 显式属性轴、可变 C、目标类分数与 NMS；公共模拟测试及实图验证支撑 |
| model-input-preprocessing delta spec | 两种输入格式、灰度快照、边距与租约；公共测试及板上输入字节对照支撑 |
| rk3576-yolo-backend delta spec | 契约、混合输出、生命周期、完整平台拒绝；六类别仅模拟测试，官方原视频任务已完成 |

执行 `openspec validate implement-rk3576-yolo-backend --strict` 与 `git diff --check`。严格结构校验通过不能代替剩余任务验收。实现任务已全部完成；后续进入独立 verify、同步及归档流程。

未执行：Jetson 构建/运行、跨平台插值及逐帧等价、真实六类别模型、真实相机、NanoTrack/完整跟踪、性能验收。没有相应硬件或任务范围内证据，不把 ARM64 编译及本次 YOLO 运行扩展为这些结论。
