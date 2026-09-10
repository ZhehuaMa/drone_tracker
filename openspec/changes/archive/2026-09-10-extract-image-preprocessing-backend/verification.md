# extract-image-preprocessing-backend 验证报告

首次验证日期：2026-09-09；构造接口修复复核日期：2026-09-10。验证对象为 `rk3576` 分支当前工作区，包含尚未提交的实现与变更工件。工作流为 `spec-driven`。首次验证后已修复压力测试的构造调用；任务仍为 29/29 项完成，尚未归档。

## 结论

原 1 项 CRITICAL（归档前必须修复）已解决：压力测试已适配新的 YOLO 构造接口。当前无未解决问题，可在本变更约定的验证范围内归档；设备运行验证边界保持不变。

| 维度 | 结果 |
|---|---|
| 完整性 | 29/29 项任务已勾选；9/9 条要求找到实现；遗漏的调用点已完成迁移 |
| 正确性 | 两个公共测试目标通过；设备像素处理及 TensorRT 适配仅完成静态核查 |
| 一致性 | 接口、会话、结果存储保护及生产装配符合设计；压力测试装配遗漏已修复 |

## 按优先级列出的问题

### CRITICAL

无未解决项。原问题已于 2026-09-10 修复：`src/hybrid_tracker/tests/stress_main.cpp:34` 现在注入 `createYoloModelCalls()` 和 `JetsonYoloPreprocessor`，并补齐后者的头文件。模型路径及五个原数值参数保持不变。已检查仓库内三个 YOLO 构造调用点，均使用新接口。

### WARNING

无独立告警。下述设备运行证据缺口属于设计明确排除的范围，不据此要求追加 Jetson 验收任务。

### SUGGESTION

无。

## 规格及场景核查

以下路径均相对于仓库根目录。自动测试指实际运行的公共测试；静态核查指检查实现及与源实现的差异，不代表设备运行结果。

| 要求 | 对应场景与证据 |
|---|---|
| 模型输入契约不暴露平台 SDK 类型 | 公共头文件随测试成功解析，依赖扫描无匹配；`model_input.h` 的只读契约和契约测试覆盖属性查询，不提供设备地址。 |
| 预处理结果在消费期间保持有效 | `preprocess/reusable_input.h` 的共享存储租约同时用于生产和测试。契约测试覆盖仍有引用时拒绝复用、最后引用释放后复用、缓存销毁后结果仍有效。Jetson 结果持有 `TensorBuffer`；同步推理调用期间保留结果引用。 |
| YOLO 对同一请求使用一致快照 | `jetson_yolo_preprocessor.cpp` 构造会话时在源帧读取租约内复制并同步；后续区域仅访问 `snapshot_`。契约测试通过真实异步检测器验证一次 beginFrame 后按 ROI、全图、切片顺序执行。源帧并发主机映射场景完成静态隔离核查，未运行设备并发测试。 |
| YOLO 数值语义 | `preprocess_geometry.cpp` 与几何测试覆盖非方形区域、奇数边距和每轴实际比例。Jetson 实现先对 U8 图像线性缩放，再按 1/255 转换；`fillBands` 写入互不重叠的 114 边带，`copyPlanes` 写三个独立浮点平面。255→1.0 和填充值 114/255 为源码核查，未运行像素数值对照。 |
| NanoTrack 模板与搜索语义 | 模板和搜索契约分别固定为 127 和 255，缩放系数为 1。`jetson_nanotrack_preprocessor.cpp` 使用公共裁剪几何、均值边带及三个浮点平面。`TargetMean` 测试覆盖新旧目标使用不同均值、重置失效、主机映射失败回退到 114。内部模板裁剪和边缘搜索像素路径完成静态核查。 |
| NanoTrack 裁剪舍入 | 几何测试覆盖图像内部、左上角及四侧边缘、半像素中心、奇偶尺寸和无效输入；实现保留规定的 floor 公式。 |
| 失败可诊断且不触发无效推理 | 公共测试覆盖空帧、Bgr8、非法区域、错误通道数、布局及元素类型、输入占用。真实 YOLO 流程的 prepare 失败不调用模拟模型。两个 Jetson 预处理器使用受检查的帧类型转换；`trt_engine.cpp:212` 起检查准备输入类型、输入数量、形状、布局、类型、存储及动态尺寸范围，再执行推理。平台类型不匹配与设备失败路径仅静态核查。 |
| 保持检测与跟踪编排 | `checkYolo` 覆盖 ROI 放宽阈值命中后停止、全图命中、切片命中、全图低分拒绝。`checkNano` 覆盖搜索失败时中心、尺寸和输出框不变，模型不被调用，下一次搜索可继续。源码对照确认 YOLO 坐标还原及 NMS、NanoTrack 锚点和框更新数学处理未被重写。 |
| 无硬件测试运行真实调用链 | 契约测试直接编译生产 `yolo_detector.cpp` 和 `nano_tracker.cpp`，注入模拟模型与预处理器。生产装配调用 `detail/jetson_model_calls.cpp` 中的 TensorRT 工厂。符合设计中已确认的测试接入点。 |

## 设计与资源协议核查

- 两个预处理接口分别表达逐帧检测和目标级跟踪状态。公共检测器及跟踪器不再包含 VPI 图像、stream 或 CUDA 输入缓存成员。
- YOLO 和 NanoTrack 的源帧读取租约先于 `Batch` 创建。`Batch` 析构先等待提交的操作结束，再销毁其图像视图，随后释放源帧租约。正常路径也有显式同步。
- `copyPlanes` 等待已提交的 CUDA 拷贝结束后才解锁图像。准备失败不返回部分输入；存储由缓存和结果共享管理。
- NanoTrack 重置使均值失效并将填充缓存标记为待更新；模板输入准备成功后才提交均值有效状态。
- 公共几何只有一个生产编译目标。测试目标的实际链接命令未包含 VPI、CUDA 或 TensorRT。
- 对照当前 HEAD 差异及 `git show jetson:src/hybrid_tracker/yolo_detector.cpp`、`nano_tracker.cpp` 核查源算法。已批准的通用帧边界使用当前分支实现作为直接迁移基线。
- 未发现 ROS 话题、参数默认值、TrackerState 数值或公开 `HybridTracker::CreateInstance()` 接口变更。

## 实际执行的验证

平台：本地 Docker 的 ARM64 Linux 容器，镜像 `roc-rk3576-ros2-build:humble-arm64`，GNU C++ 11.4.0、OpenCV 4.5.4。临时容器使用 `--rm`。构建产物位于 `.docker-artifacts/preprocess-verify/`。

使用 `DRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON` 单独配置 `src/hybrid_tracker`，重新编译后执行 CTest：

- `preprocess_common_tests`：通过。
- `preprocess_contract_tests`：通过。
- 公共预处理头文件、YOLO/NanoTrack 头文件的平台类型扫描：无匹配。
- YOLO/NanoTrack 实现中预处理平台资源残留扫描：无匹配。
- `git diff --check`：通过。
- `openspec validate extract-image-preprocessing-backend --strict`：通过。

输入为测试内生成的几何数据、恒定灰度模拟帧和模拟模型输出，未加载真实模型或录像。

## 验证边界

proposal、design、tasks 和 delta spec 均已读取，没有因缺少工件而跳过完整性、正确性或一致性检查。遵照本 change 的明确范围及 `AGENTS.local.md`，未执行 Jetson 编译、设备像素数值测试、TensorRT 运行、竞态压力测试或录像回放。也未执行 RK3576 实机推理。容器公共测试结果不能替代这些平台验证。

## 2026-09-10 修复复核

从修改后的压力测试文件直接提取 YOLO 构造语句，配合真实公共头文件，在同一 ARM64 容器中执行 C++17 `-Wall -Wextra -Werror -fsyntax-only` 检查，退出码为 0。该检查只证明构造语句的语法和类型兼容，不代表整个 Jetson 压力测试已编译或运行。检查文件位于忽略目录 `.docker-artifacts/preprocess-verify/stress_constructor_check.cpp`。

`git diff --check` 和 OpenSpec 严格校验再次通过。公共算法未修改，未重复运行前述两组公共测试。仓库未发现配置的 C++ lint 或语言服务器检查入口；由于缺少 Jetson SDK，未对整个 `stress_main.cpp` 执行 clang-tidy、clangd 检查或压力测试目标构建。
