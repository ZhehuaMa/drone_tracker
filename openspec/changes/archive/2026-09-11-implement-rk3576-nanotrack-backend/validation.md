# NanoTrack 实施验证记录

验证日期：2026-09-11。本变更规定的独立入口验收已完成，任务 29/29；尚未归档。

## 当前验证状态

下表是当前结论。后文“历史记录”保留修复前的命令、模型身份和失败诊断，不表示当前仍受阻。

| 任务/范围 | 当前状态与证据 |
| --- | --- |
| 5.4 ARM64 构建与契约测试 | 完成；核验阶段重新构建，16/16 CTest 通过，见 [核验报告](verification.md) |
| 6.1 四组合属性、提交属性与归一化责任 | 完成；四组合 query 均退出 0，见 [FP16 修复及复测](fp16-fix.md) |
| 6.2 固定浮点输入数值对照 | 完成；每组 50 项比较最大绝对差为 0，绝对/相对容差各为 1e-5，见 [数值验证](numeric-validation.md) |
| 6.3 提取前后真实 YOLO 回归 | 完成；309 帧结果、固定图像原始输出和五类失败行为一致，见下文 6.3 |
| 6.4 四精度灰度回放 | 完成；每组 309 帧、308 次搜索，质量与耗时分别报告，见 [四组合回放](fp16-fix.md) |
| 6.5 NanoTrack 与异步 YOLO 共存 | 完成有限实机检查；全 INT8、20 轮、约 18 秒，未观察到持续内存增长或停止失败，见下文 6.5 |
| 6.6 证据汇总 | 完成；本文件与所链接报告记录版本、模型、输入、容差和未执行范围 |
| Jetson 对照 | 未执行；本地无对应硬件和 SDK |
| 完整 RK3576 平台、相机和 ROS 验收 | 不属于本次独立入口范围，尚未完成 |
| 长期稳定性和部署性能验收 | 未执行；短时共存及单视频结果不能替代 |

## 平台与构建证据

目标为 RK3576 ARM64，Linux 6.1.141，RKNN Runtime `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`，RKNPU 驱动 `0.9.8`。Runtime 是主机调用 NPU 的运行库。目标兼容性基线为 Runtime 2.3.2、驱动 0.9.8。按用户要求，本次沿用既有 2.3.0 构建 SDK，即编译所用的头文件与链接库，没有升级或重建镜像。其产物已在上述目标版本上完成四组合固定输入数值对照；结论限于所记录模型和输入，不表示任意 SDK 版本均兼容。

早期验收读取 `.docker-artifacts/nanotrack/ctest-final.log` 的 16/16 测试结果，未在该阶段重跑。后续核验已重新构建并运行 CTest，16/16 通过，包括会话、NanoTrack、预处理、YOLO 和平台选择检查。最新测试日志为 `.docker-artifacts/build/nanotrack-contracts/Testing/Temporary/LastTest.log`。注入 SDK 测试以可控返回值验证程序逻辑，不能替代真实模型执行。

## 历史记录：修复前命令与输入

通过 `ssh firefly` 连接目标板。把 `.docker-artifacts/build/nanotrack-contracts/` 下两个 probe 程序复制至新的 `/home/firefly/drone_tracker_test/nanotrack_acceptance_20260911/`，保留旧 YOLO 基线。以下命令在板上执行：

```sh
cd /home/firefly/drone_tracker_test/nanotrack_acceptance_20260911
m=/home/firefly/benchmarks/nanotrackv3_evaluation_runs/nanotrackv3-official-20260910/models/components/fp16
./rk3576_nanotrack_probe --template-model "$m/template_backbone.rknn" \
  --search-model "$m/search_backbone.rknn" --head-model "$m/head.rknn" \
  --mode query > query-fp16.log 2>&1
sha256sum rk3576_nanotrack_probe rk3576_yolo_probe "$m"/*.rknn
```

模型来自板上现有官方评估目录。首次 FP16 尝试仅查询属性；后续全 INT8 查询、灰度回放和共存测试见下文。SHA-256 是文件内容指纹，仅用于重现，不作为模型接受白名单。

| 文件 | SHA-256 |
| --- | --- |
| rk3576_nanotrack_probe | `edee9260aee8e3241f90d544b33e0d1862f4e5dc0597419903ebdf14f32d4945` |
| rk3576_yolo_probe | `03a7fbd13e2ca7696c1461d8e6ceae526eccc09d5cf1b638c10a202347e3258f` |
| template_backbone.rknn | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| search_backbone.rknn | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| head.rknn | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

## 历史记录：FP16 误拒绝与当时阻塞

本节对应修复前程序。该问题已修复，成功复测见 [FP16 修复记录](fp16-fix.md)。

查询退出码为 1：`Unsupported NanoTrack quantization [input#0] expected=unquantized Float32/FP16 or affine INT8`。原始日志保存在板上上述目录的 `query-fp16.log`。

| 模板张量 | 实际属性 |
| --- | --- |
| 输入 #0 | `[1,127,127,3]`，FP16，NHWC，AFFINE，zp=0，scale=1，48387 元素，96774 字节，w_stride=128，size_with_stride=97536 |
| 输出 #0 | `[1,96,8,8]`，FP16，NCHW，AFFINE，zp=0，scale=1，6144 元素，12288 字节，size_with_stride=12288 |

NHWC/NCHW 是张量各轴的排列。AFFINE 是 SDK 报告的仿射量化标记。当时的 `rknn_nanotrack_contract.cpp` 拒绝任何带量化标记的非 INT8 张量，因而在模板输入检查处结束。后续核实后删除了这一重复检查，并补充浮点元数据回归测试；没有更换构建 SDK 或模型。

## 历史记录：修复前任务状态与容差

下表是早期验收快照，保留当时的证据边界。当前状态以文首表格为准。

| 任务/范围 | 状态及证据限制 |
| --- | --- |
| 6.1 四组合属性、提交属性与归一化责任 | 部分完成；全 INT8 属性和角色匹配通过，其余三组受 FP16 检查阻塞 |
| 6.2 固定浮点输入数值对照 | 未执行；尚未测重复执行误差，数值容差未确定 |
| 6.3 提取前后真实 YOLO 回归 | 完成；309 帧结果、固定图像原始输出和五类失败行为一致 |
| 6.4 四精度灰度回放 | 部分完成；全 INT8 已运行同一带标注视频，其他三组受阻 |
| 6.5 NanoTrack 与异步 YOLO 共存 | 完成有限实机检查；全 INT8，20 轮，约 18 秒，无持续内存增长或停止失败 |
| Jetson 对照 | 未执行；本地无对应硬件和 SDK |
| 完整 RK3576 平台和 ROS 验收 | 不属于本次独立入口范围，尚未完成 |

当时未使用已有彩色评估替代灰度验证，没有修改公共阈值，也未把模型加载或构建成功作为推理正确的证明。按用户“先跳过阻塞项”的要求继续独立验证；当时任务 6.1、6.2、6.4 保持未勾选，后续取得完整证据后才勾选，完成条件未缩减。

## 历史记录：修复前四组合查询

完整诊断保存在 [evidence](evidence/)。模型目录仍为上述 `models/components`：

| Backbone / Head | 文件子目录 | 结果 |
| --- | --- | --- |
| FP16 / FP16 | 均为 `fp16` | 退出 1，模板 FP16 属性被拒绝 |
| INT8 / INT8 | 均为 `int8/rknn_int8` | 退出 0，全部角色匹配 |
| FP16 / INT8 | `fp16`、`int8/rknn_fp16_backbone_int8_head` | 退出 1，模板 FP16 属性被拒绝 |
| INT8 / FP16 | `int8/rknn_int8`、`fp16` | 退出 1，Head FP16 属性被拒绝 |

全 INT8 的两个图像输入均为 NHWC、zp=-128、scale=1。模板输出为 NCHW、zp=7、scale=0.103884；Head 模板输入为 NHWC、zp=6、scale=0.100217。搜索输出和 Head 搜索输入均为 zp=11、scale=0.125591。两端量化参数不完全相同，因此浮点特征交接有实际必要。提交采用 Float32、原生逻辑排列、`pass_through=0`；输出请求 `want_float=1`，表示让 SDK 返回浮点。代码不额外归一化或反量化。当时尚未执行 6.2，不能仅凭这些属性推断数值正确；后续独立对照见 [数值验证](numeric-validation.md)。

全 INT8 模型 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| template_backbone.rknn | `848ec40a659b5a3484c0d76ba6ded353250f524c7f384fec95a0f85cf2682bdf` |
| search_backbone.rknn | `ecabaefdeadba721b466d942f76195bb9a5184e8e0a7d2a8262cc2d86e38ccdd` |
| head.rknn | `f4fc6259eaeb83860515773df49391c860a3be31507f613b2c15fcde6430e736` |

## 6.3 YOLO 提取前后回归

两个既有程序均在当前 Runtime 2.3.2、驱动 0.9.8 上运行。提取前程序来自板上 `yolo_acceptance_20260910/rk3576_yolo_probe`，SHA-256 为 `b794d3c11b4efb71a62103ab18e3f227cacfe8d90904859d3f828d7aea2daa55`。模型为 `yolov8n-model_zoo-int8_fp16-multi_cls.rknn`，SHA-256 为 `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3`。

使用原视频 `V_DRONE_051.mp4`（640×512、309 帧、30 FPS），SHA-256 为 `01c49506366d2b8f273df4951bc8546e2f0b5920203f815589950d90aa5b53d7`。固定图像 `video-frame-1.png` 的 SHA-256 为 `bd3783fca65e01383666dcbf44c466f3f4bd584364bac3aa83f8e5ebecdafa93`。输入来源见 [此前实机记录](../archive/2026-09-11-implement-rk3576-yolo-backend/board-validation.md)。

分别执行 `--model MODEL --mode detect --target 2 --video VIDEO`，两次退出码均为 0。随后执行本目录 [yolo_regression.py](yolo_regression.py)，比较日志，并运行图像及失败输入。板上无 conda，使用 `/usr/bin/python3` 标准库完成比较。

- 309 帧的命中、类别、分数和框文本完全相同，均为 159 帧命中、150 帧未命中。
- 固定图像的输入、六份 Float32 原始输出和合并预测逐字节相同，最大数值差为 0。
- 模型缺失、图像缺失、目标类别越界、非法 ROI、误用双输入 Head 模型，均退出 1，且没有成功帧输出。允许诊断文字增加张量细节，不要求整段错误文本相同。
- 比较采用精确字节/结果文本相等，比此前原始输出 `atol=1e-5, rtol=1e-5` 和显示框 `atol=1e-3, rtol=1e-5` 更严格，没有放宽容差。

摘要见 [yolo-regression.json](evidence/yolo-regression.json)，完整回放见 `evidence/old-video.log` 和 `evidence/new-video.log`。未通过故意破坏驱动制造真实设备故障；设备失败注入仍由已有契约测试覆盖。

## 6.4 历史记录：首次全 INT8 灰度回放

本节保留首次全 INT8 回放的数据与统计口径。后续同条件四组合比较已完成，见 [四组合回放](fp16-fix.md)。

对同一原视频使用数据集首帧标签 `labels/test/V_DRONE_051_frame_000001.txt`，内容为 `2 0.738281 0.556641 0.032813 0.039062`。标签是类别、归一化中心及宽高；按 640×512 换算，首帧框为 `461.99968,275.00032,21.00032,19.999744`。后续标签按原帧号匹配；数据集从 1 计数，probe 从 0 计数。全部 308 个后续帧均找到唯一类别 2 标签，没有选择检测结果作为真值。

```sh
cd /home/firefly/drone_tracker_test/nanotrack_acceptance_20260911
m=/home/firefly/benchmarks/nanotrackv3_evaluation_runs/nanotrackv3-official-20260910/models/components/int8/rknn_int8
timeout 120 ./rk3576_nanotrack_probe --template-model "$m/template_backbone.rknn" \
  --search-model "$m/search_backbone.rknn" --head-model "$m/head.rknn" --mode replay \
  --video /home/firefly/drone_tracker_test/yolo_acceptance_20260910/V_DRONE_051.mp4 \
  --bbox 461.99968,275.00032,21.00032,19.999744 > replay-int8.csv 2> replay-int8.log
/usr/bin/python3 summarize_replay.py "$PWD"
```

程序退出 0。彩色视频明确转灰度，公共算法参数保持默认。统计排除模板初始化帧；没有重置、重检测或按真值修正后续跟踪。标注对应数据集同名原帧，JPEG 与原视频解码像素可能存在压缩差异，坐标仍按相同 640×512 图像空间解释。

| 指标 | 全 INT8 结果 |
| --- | --- |
| 搜索返回有效框 | 308/308 |
| 平均 IoU（预测框与标注框交集面积除以并集面积） | 0.315898 |
| 平均中心误差 | 2.34935 像素 |
| 平均置信度 | 0.997392 |
| 与标注完全不重叠的帧数 | 0/308 |
| 最后帧 IoU / 中心误差 | 0.091477 / 1.34885 像素 |
| 平均 / P95 耗时 | 10.9467 / 11.354 ms |

P95 采用排序后第 `ceil(0.95*N)` 个样本，表示 95% 搜索帧不超过的耗时；测量含灰度转换、预处理和更新，不含视频解码。没有同时运行本次 YOLO 回归或共存程序。高置信度与较低框重叠率同时存在，不能将置信度当作准确率。零重叠帧数与最后帧误差仅描述漂移迹象，不构成已批准的质量门槛。原始 CSV、每帧误差及全部标注哈希见 [统计结果](evidence/replay-int8-summary.json)；统计脚本为 [summarize_replay.py](summarize_replay.py)。该阶段其余三种组合尚未回放。

## 6.5 共存、资源与停止

[board_coexistence.cpp](board_coexistence.cpp) 为独立验收程序，链接现有 ARM64 构建库，不改生产算法或 Runtime。使用约定临时容器和 `--rm` 编译，SDK 仍为现有 2.3.0；程序 SHA-256 为 `9487a5db7990d2e1991b3d29de4e801616fa5fbe76ec0611f7faf32834211fff`。新增翻译单元使用本地检查数据库运行 clang-tidy 的 `clang-analyzer-*` 无诊断、clangd 检查为 0 errors。日志在 `.docker-artifacts/nanotrack/coexistence/`。

在 `nanotrack-contracts` 构建目录中，复现编译命令如下。`repo` 为容器内仓库路径；平台目录使用 `-iquote`，避免平台同名头文件遮蔽 SDK 的 `<rknn_api.h>`：

```sh
repo=/work/ros2_ws/src/drone_tracker
c++ -std=c++17 -O2 -I"$repo/src" -I"$repo/src/hybrid_tracker" \
  -iquote "$repo/src/platform/rk3576" -I"$repo/.docker-artifacts/rknn/include" \
  -I/usr/include/opencv4 "$repo/openspec/changes/implement-rk3576-nanotrack-backend/board_coexistence.cpp" \
  -o board_coexistence librk3576_yolo_sdk.a librk3576_yolo_runtime.a librk3576_yolo_host.a \
  common/libhybrid_tracker.a common/libmodel_common.a common/libpreprocess_geometry.a \
  "$repo/.docker-artifacts/rknn/lib/librknnrt.so" -lopencv_video -lopencv_dnn \
  -lopencv_videoio -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -pthread
```

板上使用上述全 INT8 三文件、相同 YOLO 模型和 `video-frame-1.png`：

```sh
timeout 240 ./board_coexistence "$m/template_backbone.rknn" "$m/search_backbone.rknn" \
  "$m/head.rknn" /home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn \
  /home/firefly/drone_tracker_test/yolo_acceptance_20260910/video-frame-1.png > coexistence.log 2>&1
```

退出 0，20 轮共用时 18.1154 秒。每轮重新加载四个独立模型会话，循环双重 reset、建立模板、发起异步 YOLO、同步搜索 NanoTrack、读取 YOLO 结果，共完成 200 次模板建立、200 次搜索及 200 次 YOLO 结果。每轮另发起一次检测，观察到真实 `rknn_run` 进入后调用两次 stop；20 次均等到工作线程结束，stop 返回时 `run` 已返回。NanoTrack 调用仅在主线程同步执行；两者对象均在调用结束后析构，没有同会话并发。

初始 VmRSS 为 51524 kB，第 1 轮为 60976 kB，第 2～20 轮为 61160～61592 kB，最后为 61508 kB。VmRSS 是进程当前驻留物理内存。每轮销毁后线程数为 1，文件描述符为 6（初始 5），没有持续增长。详细记录见 [coexistence.log](evidence/coexistence.log)。本项是有限负载验证，不证明任意长时间无泄漏，也未直接测量 NPU 设备内存分配。
