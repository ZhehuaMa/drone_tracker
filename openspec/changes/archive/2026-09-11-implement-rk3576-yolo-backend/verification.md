# 独立验证报告：implement-rk3576-yolo-backend

验证日期：2026-09-10。分支：`rk3576`。对象为当前工作区，包括未提交的新文件和修改。采用 `spec-driven` 工作流，核对 proposal、design、tasks 和三个 delta specs（本变更新增或修改的规格）。初次验证仅新增报告；后续按评审结果补充缩放测试并修正文档，未修改生产实现、任务勾选或执行归档。

## 结论

初次发现的 1 项 WARNING（验证缺口）和 1 项 SUGGESTION（文档改进）均已修复并复核。当前无未解决的 CRITICAL、WARNING 或 SUGGESTION。本阶段检查通过，可以进入同步及归档流程；未执行的跨平台和范围外检查仍见末节。

| 维度 | 结果 |
| --- | --- |
| 完整性 | 26/26 任务已勾选；11/11 条要求找到实现及验证证据 |
| 正确性 | 31 个规格场景均找到代码处理和测试或实机证据；已补充非均匀图像放大、缩小的独立像素对照 |
| 一致性 | 7 项设计决策遵循；验证证据和历史文档表述已补齐 |

## 本轮执行与证据范围

在 `AGENTS.local.md` 指定的 `roc-rk3576-ros2-build:humble-arm64` 镜像内，用临时 `docker run --rm` 容器重建并测试当前源码。仓库及 build/install/log 均按本地约定挂载，没有重建镜像。

```sh
cmake --build build/rk3576-yolo -j4
ctest --test-dir build/rk3576-yolo --output-on-failure
cmake --build build/yolo-common -j4
ctest --test-dir build/yolo-common --output-on-failure
openspec validate implement-rk3576-yolo-backend --strict
git diff --check
```

- ARM64 独立工程：11/11 通过，包括 Runtime 故障注入、停止、预处理、输出数学、公共算法和平台选择测试。
- ARM64 公共入口：8/8 通过。它与独立工程共享部分测试，不是额外 8 种独立覆盖。
- OpenSpec 严格校验、差异空白检查均通过。
- 通过 SSH 在真实 RK3576 复测固定图像。Runtime 为 `2.3.0 (c949ad889d@2024-11-07T11:35:33)`，驱动为 `0.9.8`，类别数为 4。
- 当前重建 probe 与板上已部署 probe 的 SHA-256 完全一致：`b794d3c11b4efb71a62103ab18e3f227cacfe8d90904859d3f828d7aea2daa55`。因此本轮实机复测使用的程序与本轮构建对应。
- 模型 `yolov8n-model_zoo-int8_fp16-multi_cls.rknn` 哈希再次确认是 `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3`。

实机命令：

```sh
/home/firefly/drone_tracker_test/yolo_acceptance_20260910/rk3576_yolo_probe \
  --model /home/firefly/drone_tracker_test/models/yolov8n-model_zoo-int8_fp16-multi_cls.rknn \
  --mode detect --target 2 \
  --image /home/firefly/Desktop/drone_yolo_dataset/images/test/V_DRONE_051_frame_000105.jpg
```

输出再次为 `frame=1 hit=1 class=2 score=0.53206`，框为 x=413.811、y=260.84、宽=26.1902、高=21.1553，与既有记录一致。

本轮复核了 [实机记录](board-validation.md) 及其保存的常量输入、数学对照、最终框和录像证据。本轮没有重新执行全部 ONNX/RKNNLite 对照、309 帧两轮录像或生命周期压力循环；这些结论引用既有验收证据。既有录像两次均为 159 帧命中、150 帧未命中，结果文本一致。既有资源检查为 300 次串行推理、30 次可恢复输入错误和 10 次运行中停止。它们不构成性能或无限时长无泄漏证明。

## 要求与场景映射

下表源码路径相对仓库根目录。每行列出对应规格的全部场景组；“覆盖”包含模拟边界测试和代码路径核对，不表示每个场景都使用真实模型执行。

| 规格要求 | 实现位置 | 场景与验证证据 |
| --- | --- | --- |
| YOLO 后端提供兼容现有解码的原始预测 | `model/validation.cpp:111`、`model/yolo_output_adapter.cpp:32`（均位于 `src/hybrid_tracker/`） | 合法输入、不兼容输出、六类别三尺度、零分数确定性、NaN/Inf：`model_contract_tests`、`yolo_output_adapter_tests`；同源 ONNX 实际 C++ 转换见实机记录 |
| 模型后端接入保持公共检测与跟踪行为 | `src/hybrid_tracker/yolo_detector.cpp:158`、`src/hybrid_tracker/nano_tracker.cpp` | ROI 提前命中、搜索模型失败不更新状态：`preprocess_contract_tests`；核对 Jetson 参考分支的阈值、坐标公式和 NMS 调用 |
| 只报告配置的目标类别 | `src/hybrid_tracker/yolo_detector.cpp:33`、`:281` | 非最高分类别仍被接受、其他类别不抑制目标、越界不启动线程、四类/六类新实例配置：`preprocess_contract_tests`；板上类别 2 实图复测 |
| YOLO 预处理保持现有模型输入数值语义 | `src/hybrid_tracker/preprocess/input_validation.h:22`、`src/platform/rk3576/yolo_preprocessor.cpp:16` | 奇数边距、255 三通道、114 填充：`rknn_preprocess_tests`、`preprocess_common_tests`；非均匀缩放独立对照见已关闭的 W1 |
| 预处理失败可诊断且不会触发无效推理 | `src/platform/rk3576/yolo_preprocessor.cpp:50`、`src/platform/rk3576/rknn_yolo_backend.cpp:17` | Bgr8、错误契约、错误映射或存储：公共契约测试和 RKNN 预处理测试，后端采用受检查的派生类型转换 |
| 重构保持检测与跟踪编排行为 | `src/hybrid_tracker/yolo_detector.cpp:158`、`src/hybrid_tracker/nano_tracker.cpp` | ROI 命中停止级联、NanoTrack 搜索预处理失败保留状态：`preprocess_contract_tests` |
| 主机预处理遵守映射和输入租约 | `src/platform/rk3576/yolo_preprocessor.cpp:28`、`:67` | 非连续行、释放源帧、旧结果阻止覆盖、释放后可复用：`rknn_preprocess_tests` |
| 兼容模型可以替换而不绑定文件身份 | `src/platform/rk3576/rknn_runtime.cpp:67`、`src/hybrid_tracker/model/yolo_output_adapter.cpp:32` | C=1/4/6/64、错误数量/形状/尺度：Runtime 与适配器测试；未绑定节点名称或哈希 |
| 每个输出按自身数值编码读取 | `src/platform/rk3576/rknn_runtime.cpp:67`、`:214` | 混合 INT8/FP16、Float32、错误量化和大小：Runtime 测试；SDK 以 `want_float=1` 转换，实机 C API/RKNNLite 原始输出对照支撑 |
| 同步执行与资源生命周期可验证 | `src/platform/rk3576/rknn_runtime.cpp:159`、`:185` | 输出跨执行器存活、运行中停止、可确认完成的执行失败：Runtime 测试和既有板上生命周期记录；无法确认完成的路径使用隔离子进程验证终止 |
| 独立检测入口不宣称完整平台就绪 | `src/platform/rk3576/CMakeLists.txt:1`、`src/platform/rk3576/rk3576_yolo_probe.cpp:134` | ARM64 独立构建、缺失 SDK 诊断、完整平台拒绝、实机正确性记录：本轮构建及既有隔离/实机证据 |

设计核对：结构兼容代替文件白名单；契约描述实际 UINT8/Float32 提交存储；只读映射内建立快照；同步取得自有输出后释放 SDK 缓冲；公共数学适配不执行业务筛选；目标类别在 NMS 前生效；独立构建保持完整平台拒绝。以上七项均有实现。NMS 指去除高度重叠的重复框，仍由公共检测器执行。

## 问题分级

### CRITICAL

无。未发现未勾选任务或缺失的要求实现。

### WARNING

无未解决项。**W1：非均匀图像的实际缩放缺少独立像素对照——已关闭。**

原问题：像素断言使用常量图，真实图像全图对照仅填充边距，缺少非均匀像素在实际缩放后的独立参考。修复位于 `src/hybrid_tracker/tests/rknn_preprocess_test.cpp` 的 `bilinearReference` 和 `checkNonuniformResize`。

固定 Gray8 图由 `pixel(x,y)=(37*x+61*y+97*((x+y)%2))%256` 生成，无需外部文件。参考按像素中心映射，钳制边缘后以双精度对四个邻点加权，不调用 `cv::resize` 或生产几何函数。测试逐像素核对三个通道，精确断言 114 边距、缩放尺寸和每轴坐标还原比例。

| 场景 | 源图与 ROI（x,y,宽,高） | 缩放及上下边距 | 对双精度参考最大绝对差 | 对参考四舍五入值最大差 |
| --- | --- | --- | --- | --- |
| 非整数放大 | 17×13，`(2,1,11,7)` | 640×407，116/117 | 0.781411 灰度级 | 1 灰度级 |
| 非整数缩小 | 1001×809，`(2,3,997,797)` | 640×512，64/64 | 0.782661 灰度级 | 1 灰度级 |

U8 线性插值使用定点权重与整数舍入，因此测试允许与独立双精度参考四舍五入值相差至多 1 个灰度级。该输入像素容差不修改原模型输出或数学适配的浮点容差。两组均通过；Jetson/VPI 插值差异仍未执行。

复验使用约定 ARM64 容器、OpenCV 4.5.4。独立工程 11/11、公共入口 8/8 再次通过。对修改的翻译单元使用既有 `.docker-artifacts/rknn/lint/compile_commands.json` 运行 `clang-tidy --checks='-*,clang-analyzer-*'` 无诊断，`clangd --check --tweaks=none` 为 0 errors；随后执行构建和测试。日志在 `.docker-artifacts/log/yolo-warning-tests.log`、`yolo-warning-tidy.log`、`yolo-warning-clangd.log`。这次补测使用合成图，不需要真实模型，也未新增实机验收声明。

### SUGGESTION

无未解决项。**S1：历史验证说明容易被误读为当前状态——已关闭。**

已将 [implementation-validation.md](implementation-validation.md:5) 明确标记为任务 1–5 完成时的历史快照，定义下文“本次”的历史范围，并链接实机记录和本报告。

已将 [src/platform/README.md](../../../../src/platform/README.md) 末段的旧验证结论限定为 `introduce-platform-factory`，链接其已归档报告。新增当前 RK3576 YOLO 的实机记录和独立报告链接。独立阅读时可以区分两次变更及对应证据范围。

## 未执行与适用边界

未执行 Jetson 构建或实机运行、VPI/OpenCV 跨平台插值和逐帧等价验证。当前主机没有对应 Jetson 环境。真实六类别模型仅有模拟契约与检测器测试，尚无实机模型证据。NanoTrack RKNN、相机、完整 ROS 跟踪平台及性能验收不属于本次变更范围。没有跳过 proposal、design 或规格审查；没有将 ARM64 构建当作 Jetson 验证。
