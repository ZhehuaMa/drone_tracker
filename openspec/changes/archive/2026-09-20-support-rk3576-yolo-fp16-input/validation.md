# FP16 YOLO 输入支持验证记录

日期：2026-09-16。分支：`rk3576`。变更：`support-rk3576-yolo-fp16-input`。

## 当前结论

完成 **15/15 项**。用户在相机前放置气球图案后，开发板 2 两轮均实际检测到目标并进入 Tracking=2，ON/OFF 与退出清理通过。此次目标为纸上的气球图案，证明真实相机输入上的检测与跟踪集成，不代表立体气球、各种光照或长时质量验收。未改变阈值，尚未归档变更。

| 检查 | 结论 |
|---|---|
| ARM64 ROS 构建与 CTest | 通过，12/12 |
| 独立 ARM64 CTest | 最终通过，33/33 |
| FP16 六输出与同源 ONNX 对照 | 通过预先约定容差 |
| 固定可见气球样本检测 | 通过，类别 0，分数 0.793586 |
| 原兼容 INT8 新旧版本回归 | 输入、六输出、解码预测逐字节一致，检测与停止一致 |
| 相机实际目标与跟踪 | 补测通过，两轮均经历 Detecting=0、TargetFound=1、Tracking=2 |
| 相机 ON/OFF 和退出清理 | 两轮通过，控制恢复，无遗留进程或设备占用 |
| OpenSpec 严格校验与 diff 空白检查 | 通过 |

## 实现与无设备验证

YOLO 校验增加 FP16 NONE/AFFINE 分支，保留 INT8 AFFINE、zp=-128、scale=1/255 的严格检查。程序始终提交 UINT8 NHWC；NHWC 表示批次、高、宽、通道排列。`pass_through=0` 表示 Runtime 按模型配置转换。成功和失败诊断均说明 mean=0/std=255 是兼容要求，普通属性查询不能证明归一化配置。Float32 和 UINT8 原生 YOLO 模型仍拒绝。

生产输入缓冲、输出处理、检测阈值、模型、ROS 话题和状态机未改动。新增测试逐字节检查 1,228,800 字节输入，覆盖三通道像素 0/114/150/255，检查 `want_float=1` 浮点输出请求、独立存储、失败回收和停止等待。覆盖 FP16 NONE/AFFINE、INT8 参数及形状/布局/类型/六输出结构拒绝边界。完整装配覆盖 FP16 YOLO 与 NanoTrack 成功、类别越界不启动线程、失败会话释放。

首次独立回归为 32/33：既有 `frames()` 断言仍要求正式工厂拒绝相机，与已完成的 ROS 相机变更不一致。用户明确授权修正后，改为验证缺少 media 路径的配置在访问设备前被拒绝。没有移除相机检查，最终 33/33 通过。

构建使用既有镜像 `roc-rk3576-ros2-build:humble-arm64`，容器 RKNN SDK 为 2.3.0。没有更新镜像或 SDK。

- `./build.sh --test`：最终 ROS/公共 12/12，通过日志为 `.docker-artifacts/log/build-console.log`、`test-console.log`。
- 独立源码 `src/platform/rk3576`，构建目录 `.docker-artifacts/yolo-fp16-build`。配置 Release、`RKNN_INCLUDE_DIR=/opt/rknn-runtime/include`、`RKNN_LIBRARY=/opt/rknn-runtime/lib/librknnrt.so`；`cmake --build` 后执行 `ctest --output-on-failure`。最终日志为 `.docker-artifacts/log/yolo-fp16-standalone-final.log`，首次失败和修复后日志仍保留。
- 镜像无 clang-tidy、clangd，仓库未配置相应静态检查命令，未执行这些检查。
- `git diff --check` 与 `openspec validate support-rk3576-yolo-fp16-input --strict` 通过。逐项对照 proposal、design 和增量规格，没有未解释的实现偏差；相机目标验收仍缺证据。

## 开发板 2 与模型来源

通过 `ssh firefly2` 运行。新 C++ 工具实际查询 Runtime 2.3.2（429f97ae6b）、驱动 0.9.8。系统 Runtime `/usr/lib/librknnrt.so` 前后 SHA-256 均为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，未覆盖系统库。

SHA-256 是文件内容指纹，仅用于追溯，不是模型白名单。

| 角色 | 路径 | SHA-256 |
|---|---|---|
| FP16 气球 YOLO，类别 0 | `/home/firefly/Desktop/models/balloon_best_zoo6_fp16.rknn` | `69fd2fb4753b4fcd91820acd54fc648b82a74a4ec37a5459a8bb3f42b1f2b232` |
| 同源六输出 ONNX | `/home/firefly/benchmarks/balloon-convert-20260916/balloon_best_zoo6.onnx` | `4c6c487c6c7d45d6c9c08b01040511ed299607d015c6ee3828a70b290d9258c9` |
| 原兼容 INT8 YOLO，回归类别 2 | `/home/firefly/ros-camera-final-20260915/models/yolo.rknn` | `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3` |
| 固定数值与 INT8 回归图像 | `/home/firefly/ros-camera-final-20260915/scene.png` | `68badc14f294f000a18cb7a5eb9f9796a8d638c32699a15895d1dc7073c73b51` |
| 固定气球图像 | `/home/firefly/Desktop/openimages_v7_balloon_yolo/images/test/fe9fa5eef6817ec8.jpg` | `3a47cc6fea76d4f21a72d7723a7c06d1ddabff14e3c77f4bfe59cd4b4bf4c9dd` |
| 数值对照实际提交输入 | 板端验收目录 `scene.input.u8` | `f23faa3e7c5d1386ca12c7b202a59316c9f509f78d303f727f9c54b6d2e80341` |

FP16 转换目录中的 RKNN 与部署文件哈希相同。已读取转换脚本和 [model_manifest.json](evidence/model_manifest.json)，确认源 ONNX 哈希、输出角色、mean=[0,0,0]、std=[255,255,255]。原 INT8 文件可读元数据同样记录该 mean/std，见 [int8-metadata.txt](evidence/int8-metadata.txt)；此读取仅用于验证证据，未加入生产加载路径。

FP16 输入实际报告 `[1,640,640,3]`、AFFINE、zp=0、scale=1、2,457,600 原生字节，程序仍提交 1,228,800 字节 UINT8。六输出均为 NCHW FP16，类别数 1，见 [fp16-query.log](evidence/fp16-query.log)。

## 数值对照预先约定（执行前记录）

参考采用已核对转换清单的六输出 ONNX，SHA-256 为 `4c6c487c6c7d45d6c9c08b01040511ed299607d015c6ee3828a70b290d9258c9`。转换目录内 RKNN 与待测 FP16 文件哈希完全相同。清单明确 mean=[0,0,0]、std=[255,255,255]，六输出按 80/40/20 尺度交替为框分布与类别原始分数。

固定数值样本选择 `/home/firefly/ros-camera-final-20260915/scene.png`，它只用于非均匀灰度数值对照，不作为气球检测成功证据。由 C++ `region` 模式导出实际 UINT8 NHWC 输入；ONNX 参考只对该缓冲转为 Float32 NCHW 并除以 255，不重新缩放图像。

六输出统一采用逐元素 `abs(actual-reference) <= 0.5 + 0.02*abs(reference)`，并要求每个输出平均绝对误差不超过 0.1。另报告最大绝对误差、平均绝对误差、最大/平均相对误差；相对误差分母为 `max(abs(reference), 1e-3)`，防止零附近除零。所有输出必须有限。

依据：同模型转换阶段三个独立彩色样本的六输出平均绝对误差约 0.026–0.050，存在 FP16 累积舍入；平均误差门槛取 0.1，同时用绝对 0.5 加相对 2% 限制离群元素。这是本次输入接入的工程验收门槛，不是气球质量或跨平台精度承诺。门槛在获取本轮对照结果之前确定，失败后不得无依据放宽。

## 数值结果与复现

复用已有 `rk3576_yolo_probe --mode region --dump`，未新增生产推理路径。板端目录为 `/home/firefly/yolo-fp16-acceptance-20260916`，保留输入和原始输出。本地只保存小型日志及结果，不提交模型或大型张量。

```bash
cd /home/firefly/yolo-fp16-acceptance-20260916
./rk3576_yolo_probe --model /home/firefly/Desktop/models/balloon_best_zoo6_fp16.rknn \
  --mode region --image /home/firefly/ros-camera-final-20260915/scene.png --dump scene
/home/firefly/venvs/rknn-2.3.2/bin/python compare_onnx.py \
  /home/firefly/benchmarks/balloon-convert-20260916/balloon_best_zoo6.onnx scene numerical.json
```

对照脚本见 [compare_onnx.py](compare_onnx.py)。板端沿用已有 RKNN 虚拟环境 Python 3.10；本地文件处理使用 conda base。预处理输入有 61 种灰度值，范围 11～226，三通道相同，符合非均匀固定输入要求。结果见 [numerical.json](evidence/numerical.json)。ONNX Runtime 打印 GPU 发现告警，本次显式使用 CPUExecutionProvider，参考计算完成。

| 输出 | 最大绝对误差 | 平均绝对误差 | 最大相对误差 | 结论 |
|---|---:|---:|---:|---|
| 0 | 0.14926910 | 0.01295125 | 37.52827466 | 通过 |
| 1 | 0.05489635 | 0.01058352 | 0.00485934 | 通过 |
| 2 | 0.46470642 | 0.04290407 | 73.26268802 | 通过 |
| 3 | 0.12689018 | 0.02380827 | 0.00705856 | 通过 |
| 4 | 0.34796715 | 0.02575923 | 86.08725102 | 通过 |
| 5 | 0.10348129 | 0.01692169 | 0.00879963 | 通过 |

零附近的框分布数值会产生较大相对误差；判定使用预先规定的绝对加相对容差和平均误差限制，不以相对误差单独判定。结果证明本次固定灰度输入的六输出数值在容差内，不代表全数据集检测精度或 Jetson 等价性。

## 固定气球检测

已目视检查上述气球测试图：左上、上中及左下有气球。原始尺寸为 1024×768，数据集类别 0 标注包含左上目标。运行现有 `--mode detect --target 0 --image <固定气球图像>`，工具保持置信度/NMS 参数，不降低阈值。结果为 `hit=1 class=0 score=0.793586`，框左上角 `(0.0128052, 9.83438)`、宽高 `(282.112, 316.357)` 像素，对应左上方气球。见 [balloon-detect.log](evidence/balloon-detect.log)。进程正常退出，不将单图成功表述为数据集精度通过。

## 原 INT8 回归

从改动前提交 `40d6a43548f62398bf99376f07a88398716bde61` 使用 `git archive` 导出隔离源码，在同一 ARM64 镜像构建旧版 `rk3576_yolo_probe`，日志为 `.docker-artifacts/log/yolo-fp16-baseline-build.log`。不修改当前工作树，也不以另一个模型作为基线。

在同一开发板使用表中原 INT8 模型、scene.png 和类别 2，对新旧工具分别执行 region 导出及 detect。输入、六个 Float32 原始输出、解码后的 prediction 均逐字节相同。两者均报告 `hit=1 class=2 score=0.512029`，框左上角 `(-0.448724,-29.4625)`、宽高 `(228.69,146.117)`。加载、推理、解码和退出均成功，见 [int8-comparison.json](evidence/int8-comparison.json)。这里只验证行为一致，不判断历史场景中是否存在正确的类别 2 目标。

## 相机集成：初次无目标检查与后续补测

在隔离目录复制新 ROS 节点，复用开发板 2 的 `ros-camera-final-20260915/ros_acceptance.py` 验收函数。参数底稿见 [fp16-software_trigger.yaml](evidence/fp16-software_trigger.yaml)，运行时覆盖 `tracker.yolo_engine` 为上表 FP16 气球模型，`tracker.yolo_target_class_id=0`。20 Hz 软件触发，1920×1200 灰度相机；模拟内参仍为 1000/1000/960/600，不声称正式标定。

NanoTrack 使用 `/home/firefly/ros-camera-final-20260915/models/fp16/` 下三个文件：

| 角色 | 文件 | SHA-256 |
|---|---|---|
| 模板 Backbone | template_backbone.rknn | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| 搜索 Backbone | search_backbone.rknn | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| Head | head.rknn | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

两轮约 8 秒和 3 秒，分别读取/消费 162、62 帧。发布图像符合 1920×1200 BGR8，原话题和状态值未修改。停止耗时为 513.911 ms、44.164 ms，均恢复相机控制。第二轮 63 次触发读取 62 帧，停止边界差 1 保留记录，不归因或宣称一触发一帧通过。节点退出码 0，四模型只加载一次，检测线程只启动一次；最终 pgrep 和设备占用检查为空。

两轮均无有效目标框，订阅状态只有 Detecting=0；实际画面较暗且未见明确气球。当时 4.5 保持未完成，不能将 ON/OFF 通过等同于跟踪成功。证据见 [camera-results.json](evidence/camera-results.json)、[fp16-camera.log](evidence/fp16-camera.log)。此段保留首次无目标检查结果；用户随后放置目标后的结果见下节。

板端成功路径验证产物在最后一处通用会话失败诊断补充之前生成；最后补充仅作用于加载失败的错误文本，成功执行路径未改动，最终源码重新通过 12/12 和 33/33 回归。没有替换用户手动部署。Jetson、长时性能、NPU 设备内存和正式标定不属于本次已完成证据；其他 change 状态未修改。


## 用户放置气球图案后的最终补测

用户明确告知已放置气球后，继续任务 4.5。此次不修改生产源码；将最终通过 12/12 和 33/33 测试的节点复制到独立目录 `/home/firefly/yolo-fp16-acceptance-20260916/balloon-present`。本地与板端节点 SHA-256 均为 `0c5c14defcd8fe72c23fe15f0a7507fab67d060af25bf4c31c7b64505815e950`。FP16 模型和系统 Runtime 指纹与前述记录相同，未替换用户手动部署。

沿用前述 FP16 YOLO 和三个 NanoTrack 文件、20 Hz 软件触发配置及模拟内参。可复现脚本为 [camera_balloon_check.py](camera_balloon_check.py)，它复用板端历史验收脚本的采集、ON/OFF 和退出函数。运行前加载 ROS Humble 与历史安装环境，在本次目录以 `/usr/bin/python3 camera_balloon_check.py` 执行。参数覆盖仍为 FP16 气球模型路径和类别 0，无阈值变化。

| 指标 | 第一轮（约 10 秒） | 第二轮（约 5 秒） |
|---|---:|---:|
| 发布图像/框消息 | 200/200 | 99/99 |
| 有效跟踪框 | 196 | 96 |
| 观察到的状态顺序 | 0 → 1 → 2 | 0 → 1 → 2 |
| 相机读取帧/跟踪消费帧 | 202/200 | 102/99 |
| 相机停止耗时 | 542.809 ms | 533.307 ms |
| 节点完整 OFF 耗时 | 550.037 ms | 540.089 ms |
| 控制恢复 | 成功 | 成功 |

状态 0、1、2 分别表示 Detecting（寻找目标）、TargetFound（找到目标）、Tracking（跟踪中）。两轮事件日志均记录 `Target Found` 和 `Tracking...`。ROS 六个原有话题名称和消息类型已通过订阅查询核实，详见 [camera-balloon-results.json](evidence/balloon-present/camera-balloon-results.json)。相机诊断自身的 state 数值不等于 TrackerState，不混用两者。

第一轮模型初始化框为左上角 `(584.194,99.4061)`、宽高 `(428.431,352.085)` 像素。首个收到的有效 ROS 框中心为 `(805,291)`、宽高为 `(397,361)`。已目视检查保存画面，框覆盖用户展示的气球图案。图像与状态话题独立传输，因此保存画面的叠字仍为 `Target Found`；Tracking 的成立依据是两轮实际订阅到的状态 2 及有效框，而不是截图文件名。

每轮最后一次触发与读取相差 1，且停止时尚有已采集帧未交给跟踪器。原始计数已保留；不将此边界检查表述为完整一触发一帧或性能验收。无命令错误、队列丢帧或序号缺口。

脚本确认 OFF 后消息静止，控制均恢复。节点退出码为 0，PID 23416 已不存在；四模型只加载一次，检测线程只启动一次。最终 `pgrep` 与 `/dev/video0`、`/dev/v4l-subdev2` 占用检查均为空。完整节点日志见 [fp16-balloon-camera.log](evidence/balloon-present/fp16-balloon-camera.log)。

两轮真实相机检测、跟踪启动和停止已满足任务 4.5，任务清单更新为 15/15。当前只证明气球图案场景下的短时集成，模拟内参、Jetson 检查、长时质量和 NPU 设备内存的既有限制保持不变。其他 OpenSpec 变更未修改。

补测图像保留在板端 `balloon-present/`，未将大图加入源码。`scene.png` 的 SHA-256 为 `6b8d539035ea84042127f9144214d66160d2e106c54bb0885d5dab1eb2dc046d`；`tracking.png` 为 `b90d08ae28844e69eba329e92d3acd514016f10e4859dfaf29139196ff17098f`。本地查看副本为 `.docker-artifacts/fp16-balloon-tracking.png`。
