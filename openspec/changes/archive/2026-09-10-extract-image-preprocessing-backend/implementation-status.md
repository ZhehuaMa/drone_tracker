# 实现与验收记录

`extract-image-preprocessing-backend` 已完成 29/29 项任务。前一阶段完成 9 项，本阶段完成其余 20 项，并落实已确认的测试接入点。以下结论仅覆盖本 change 规定的公共测试和静态审查范围。

## 实现结果

- 公共预处理接口描述输入形状、元素类型、布局、数值缩放、错误分类和生命周期，不暴露设备地址或硬件 SDK 类型。
- YOLO 每次请求建立一个私有帧快照。ROI、全图和切片从同一会话顺序准备输入；第一个产生有效目标的阶段终止后续级联。
- Jetson 预处理器管理图像、stream 和输入缓存。stream 是提交异步图像操作的执行队列。模型输入结果持有缓存租约，即在消费者释放结果前禁止覆盖对应存储。
- NanoTrack 预处理器管理当前目标的灰度均值、模板和搜索缓存。重置立即使旧均值失效；映射失败沿用填充值 114。
- TensorRT 适配器在绑定输入地址前检查具体结果类型、输入数量、维度、元素类型、布局、动态尺寸范围和存储长度。YOLO 与 NanoTrack 的数值缩放要求由预处理器校验。
- `detail/model_calls.h` 提供内部模型调用测试接入点。生产实现共享持有原有 TensorRT 输出缓冲区，以 OpenCV 图像头读取原始输出，没有额外复制输出像素。公共测试注入模拟模型，运行真实检测器和跟踪器。
- `HybridTracker::CreateInstance()` 和 ROS 节点调用方式未改变。已更新默认装配和压力测试中的内部构造调用。

## 测试与工具结果

公共测试在本机 Docker 的 Linux ARM64 临时容器中运行。使用已有镜像 `roc-rk3576-ros2-build:humble-arm64`，GCC 11.4 和 OpenCV 4.5.4。容器使用 `--rm`，产物保存在被 Git 忽略的 `.docker-artifacts/preprocess-common/`。

输入为人工尺寸、坐标、灰度图和模拟模型张量；没有使用真实模型、摄像头或录像。这是通用 CPU 算法测试，不是 RK3576 实机测试。

| 检查 | 结果 |
|---|---|
| `preprocess_common_tests` | 通过：几何舍入、奇数边距、四侧边缘、零填充、无效尺寸、整数范围、错误名称 |
| `preprocess_contract_tests` | 通过：真实异步级联顺序、ROI 放宽阈值、提前终止、预处理失败不推理、真实 NanoTrack 失败状态保持、失败后恢复、输入租约和目标均值重置 |
| CTest | 2/2 通过 |
| 实际链接命令检查 | 几何测试仅链接公共几何库；调用链测试仅增加 OpenCV，均无 VPI、CUDA 或 TensorRT |
| 公共头文件独立解析 | C++17，`-Wall -Wextra -Werror -Wswitch-enum` 通过 |
| 硬件 SDK 依赖扫描 | 公共预处理头文件、YOLO 与 NanoTrack 头文件无匹配；两者实现中无残留预处理资源 |
| clang-tidy | 公共几何、两组测试、YOLO、NanoTrack、DCF 共 6 个编译单元分析通过；测试中故意注入非法契约枚举值的两行带有明确局部豁免注释 |
| clangd | 上述 6 个编译单元无源码解析诊断；YOLO 和 DCF 分别有 4 次、1 次工具内部 ExtractFunction 重构探测失败，不能将完整工具检查称为全绿 |
| `git diff --check` | 通过 |
| `openspec validate extract-image-preprocessing-backend --strict` | 通过 |

上一阶段 clang-tidy 的系统头文件问题已解决：生成使用本机 LLVM 驱动及正确 macOS SDK 的专用编译数据库。Jetson 专属编译单元未运行编译型 lint；当前缺少相应 SDK，且本 change 明确排除 Jetson 编译任务。

公共测试复现命令（在具有 OpenCV 开发包的环境中执行）：

```sh
cmake -S src/hybrid_tracker -B .docker-artifacts/preprocess-common \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .docker-artifacts/preprocess-common -j2
ctest --test-dir .docker-artifacts/preprocess-common --output-on-failure
```

## 规格场景对应审查

| 规格场景 | 对应证据 |
|---|---|
| 解析公共声明、查询输入契约 | 独立头文件解析、`MockInput` 契约查询、SDK 依赖扫描 |
| 同步消费与结果释放 | 生产和模拟存储共同使用 `ReusableInput`；测试覆盖多个共享引用、拒绝复用、释放后复用，以及结果比缓存活得更久 |
| ROI 未检出后处理全图 | 真实检测器测试确认一次 `beginFrame()`，区域顺序为 ROI、全图、切片 |
| 原帧由其他消费者持有 | Jetson 会话构造在源帧读取租约中复制并同步；会话成员不保存原始帧，后续仅访问私有快照。此项为静态证据 |
| 非方形区域与奇数边距 | 公共几何测试固定实际整数内容尺寸、四侧边距与每轴比例 |
| 灰度三通道、114 边距 | Jetson 实现按公共变换线性缩放，以 1/255 转换内容及四条边带，再复制为三个独立平面。此项未做设备像素验证 |
| 模板位于图内、搜索越界 | 公共裁剪测试；Jetson 实现以 1.0 转换灰度和均值边带，模板 127、搜索 255，保持 0–255 范围 |
| 目标重置与新均值 | 真实 Tracker reset 传播测试；生产 `TargetMean` 状态测试覆盖均值 20、重置、均值 90、映射失败回退 114 |
| 空帧、Bgr8、非法区域、契约错误 | 公共校验函数及模拟预处理器分类测试；生产 Jetson 实现复用同一校验函数 |
| 平台资源类型不匹配 | 静态检查 `dynamic_pointer_cast<const VpiFrame>` 和 `dynamic_cast<const JetsonPreparedModelInput *>`，失败返回 `PlatformTypeMismatch` |
| ROI 命中后终止 | 真实检测器异步测试确认停止后续区域，并验证同样的 0.4 分数在 ROI 被接受、全图被拒绝 |
| NanoTrack 搜索失败不更新 | 真实跟踪器成功初始化并搜索后，注入预处理失败；测试读取真实中心、尺寸及输出框并确认不变，模型调用次数也不增加 |
| 模型测试接入点 | 公共测试实际编译 `nano_tracker.cpp` 与 `yolo_detector.cpp`，不链接 Jetson 模型调用实现 |

已用 `git show jetson:<path>` 对照参考实现：NanoTrack 输出数学处理、锚点生成、框转换与裁剪正文一致。YOLO 解码和 NMS 正文仅改为访问公共维度容器，计算保持一致。ROI、全图、切片顺序及阈值表达式另由源码检查和真实调用链测试覆盖。

## 设备资源与异常路径审查

| 资源 | 所有权与释放顺序 |
|---|---|
| 源帧读取租约 | YOLO 快照复制和 NanoTrack 图像准备均先建立 `VpiFrameReadAccess`，后建立 `Batch`，因此先同步并销毁本批 view，再释放源帧租约 |
| VPI stream | `Stream` 独占拥有；正常路径显式同步。同步失败时销毁 stream 并清空句柄，销毁操作隐式同步，之后才释放图像引用 |
| 临时 VPI view | `Batch` 保存独占 `Image` 对象；析构先排空 stream，再销毁 view。创建或提交中途失败也走相同顺序 |
| YOLO 工作图像 | 由单个会话持有；尺寸变化通过 `Image::ensure` 更新句柄。填充图只有成功写入后才标记有效，失败不会复用未初始化填充像素 |
| NanoTrack 缓存 | 由目标预处理器独占拥有；均值图尺寸或目标均值变化时重填，失败保持待重填标记 |
| CUDA 平面拷贝 | 图像锁覆盖三个通道的拷贝和同步；部分提交失败仍先同步已提交任务，再解锁并报告失败 |
| 模型输入存储 | 缓存持有一个共享引用，成功结果另持租约；存在结果引用时拒绝写入。失败释放局部租约，不暴露部分结果 |
| 模型输出存储 | `TrtOutputStorage` 持有输出缓冲区，生命周期覆盖后处理期间的 OpenCV 图像头借用 |

TensorRT 元素类型与布局查询依据 [NVIDIA ICudaEngine API](https://docs.nvidia.com/deeplearning/tensorrt/archives/tensorrt-861/api/c_api/classnvinfer1_1_1_i_cuda_engine.html)。VPI stream 销毁的隐式同步保证依据 [NVIDIA Stream API](https://docs.nvidia.com/vpi/group__VPI__Stream.html)。这些 API 文档核对属于静态审查，不能替代目标 SDK 的编译和运行证据。

## 验证边界

本次没有执行 Jetson 平台编译、测试或运行，也没有执行 RK3576 实机验证。设备故障恢复、VPI/CUDA 实际像素结果、真实模型输出等价性及长期并发稳定性均未获得运行时证据。本 change 不实现 RKNN/RGA 后端，不宣称完整程序已经能在 RK3576 上运行。
