# introduce-platform-factory 验证记录

## 当前结论

公共构建及 6 个测试目标通过。Jetson 工厂和业务入口已完成代码迁移与依赖拆分。经范围确认，旧相机内部最小清理修复已纳入提案、设计、规格和任务，并完成源码逐路径审查。24/24 项任务完成，当前没有已知的范围内阻塞。

本记录区分公共测试与 Jetson 静态审查。静态审查只说明代码调用及所有权关系，不证明实际设备安全或数值等价。

## 环境与复现

- 日期：2026-09-10。
- 开发分支：`rk3576`；修改前基准提交：`26007f0d1008f5a7bf1d81639142ad97e684196a`。本次修改尚未提交。
- 公共测试环境：临时 Linux ARM64 容器，镜像 `roc-rk3576-ros2-build:humble-arm64`，GNU C++ 11.4.0，OpenCV 4.5.4，C++17，Debug 配置。
- 输入：模拟模型执行器、两种不同实现类型的帧、固定灰度/BGR 图像、人工控制的线程启动失败和正在执行的检测。无真实模型文件、录像或硬件采集。
- 容器使用 `--rm`；公共测试使用独立构建目录。没有配置或编译 RK3576 生产目标，也没有配置 Jetson 目标。

可移植复现命令：

```sh
cmake -S src/hybrid_tracker -B build/platform-contracts \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/platform-contracts -j4
ctest --test-dir build/platform-contracts --output-on-failure
openspec validate introduce-platform-factory --strict
```

首次公共构建发现现有 `cv::Vec4d::zeros()` 与 `cv::Vec6d::zeros()` 在该 OpenCV 版本返回不可直接赋值的矩阵类型。已改为同样数值的显式零向量，再构建通过；滤波初值与算法未改变。

## 公共测试结果

| 测试目标 | 结果 | 证据与边界 |
|---|---|---|
| `model_contract_tests` | 通过 | 真实模型串联、张量验证及模拟执行器回归 |
| `preprocess_common_tests` | 通过 | 原有几何、裁剪、letterbox 数值回归；letterbox 指保持宽高比缩放后补齐输入尺寸 |
| `preprocess_contract_tests` | 通过 | 原有真实检测/NanoTrack 逻辑、输入租约、级联次序和错误路径 |
| `hybrid_tracker_tests` | 通过 | DCF、卡尔曼滤波及切片算法回归 |
| `platform_contract_tests` | 通过 | 公共头文件编译、只能移动的集合、缺失组件、启动异常、完整跟踪入口、真实装配函数失败回收、模拟帧源 |
| `platform_selection_tests` | 通过 | 顶层请求 `rk3576`、`other` 均在硬件发现前报告请求值和支持值 `jetson` |
| OpenSpec 严格校验 | 通过 | 工件格式有效，不作为代码行为证据 |

`platform_contract_tests` 的资源计数器检查每个组件恰好销毁一次。检测进行中销毁测试通过条件变量固定执行时机，确认析构等待检测返回后才释放组件。线程启动函数可注入异常，默认生产路径仍创建 `std::thread`。这些测试未运行设备工作。

## 规格场景逐项关联

| 规格场景 | 证据 | 状态 |
|---|---|---|
| 使用既有默认构建入口 | 顶层 `CMakeLists.txt` 默认 `jetson`；模型路径仍由节点拼接 | 静态审查符合；不配置 Jetson |
| 请求尚未实现的平台 | `platform_selection_tests` | 公共测试通过 |
| 相机和回放使用一致平台 | 节点、`replay_harness.cpp`、`stress_main.cpp` 均调用 `CreatePlatformFactory()` | 静态审查符合 |
| 第三个模型加载失败 | `testAssembly` 向生产使用的 `assembleTracker` 注入 Head 加载失败 | 公共测试通过；身份、路径、无半套结果、资源计数均检查 |
| 复制后修改原图 | Jetson 创建器直接委托 `VpiFrame::copyFrom()`，原实现使用 `mat.clone()`；`testSourceAndFrames` 检查模拟契约 | 静态审查及模拟测试通过；没有运行真实 VPI 复制 |
| 非连续主机图像 | 原复制实现由 OpenCV clone 按实际跨度复制；模拟灰度和 BGR 子图测试 | 静态审查及模拟测试通过 |
| 不支持的图像输入 | 原复制实现拒绝空图与非 Gray8/BGR8；公共结果补充失败诊断 | 静态审查及模拟测试通过 |
| 相机启动录像 | 适配器把三个参数直接交给 `MVCamera::setup()`；节点 ROS bag 代码保留 | 静态审查符合；模拟检查参数传递 |
| 视频文件启动 | 适配器的视频分支不设置相机借用指针，录像调用无相机操作 | 静态审查符合 |
| 采集停止后再次启动 | 公共模拟源重复 stop/start；适配器不创建队列或额外线程 | 模拟测试通过；相机失败清理另见下文静态审查 |
| 模型创建中途失败 | `testAssembly` 覆盖 YOLO、Backbone、Head 各阶段加载及契约异常 | 模型回收测试通过；已创建相机的回收见下文静态审查 |
| 检测线程启动失败 | `testMissingAndStartup` 在真实启动位置注入异常 | 公共测试通过，所有组件释放，无活动检测 |
| 创建器先于帧销毁 | 组件没有工厂借用；VPI 帧状态共享持有资源；模拟视图超出创建器和帧引用作用域 | 静态审查及模拟测试通过 |
| 正在检测时退出 | `testCompatibilityAndDestruction` 阻塞模拟推理并并发销毁跟踪器 | 公共测试通过，等待结束后释放资源 |
| 无硬件开发库的公共测试 | 独立 CMake 入口仅发现 OpenCV、Threads，编译完整 `hybrid_tracker` | 公共构建通过 |
| 静态审查生产依赖声明 | `hybrid_tracker` 不链接平台、VPI、CUDA 或 TensorRT；最终程序显式链接 `platform_jetson` | 静态审查符合；Jetson 依赖无 RKNN/RGA |
| Jetson 消费者收到非 VPI 帧 | `JetsonFrameCompatibility` 使用实际类型检查；原 Jetson 预处理器自身检查保留 | 静态审查符合 |
| 不兼容帧不占用时间戳 | `testCompatibilityAndDestruction` 先送错配帧，再送时间戳 1000000 的合法帧 | 真实公共跟踪入口测试通过 |
| 独立创建器产生兼容帧 | 两个 `Builder` 的帧均进入真实跟踪器；不比较工厂地址 | 公共测试通过 |
| 绕过跟踪入口直接预处理 | 现有预处理资源检查未删除，原契约测试保留 | 静态审查及公共回归通过；未运行 Jetson 预处理 |
| 默认创建跟踪器 | 原模型加载器由 `jetson_model_factory.cpp` 统一提供，公共跟踪器仅接受注入集合 | 静态审查符合 |
| 模拟执行器验证串联 | 原 `model_contract_tests`，未复制模型串联实现 | 公共测试通过 |
| 模拟组件驱动完整跟踪入口 | `platform_contract_tests` 链接真实 `hybrid_tracker` | 公共测试通过 |

## 源码审查细节

- 相机源分类仍使用 `video_source.substr(0, 5) == "video" && video_source.length() == 6`。设备路径仍增加 `/dev/` 前缀；帧尺寸仍取旧实现的查询结果。
- 相机队列容量 10、视频队列容量 30 未修改。队列满时仍丢弃新帧，生产者未增加出队操作。节点等待最多一秒后排空积压，只有原节点工作线程消费。
- 灰度/BGR 格式、微秒时间戳和原帧复制实现未修改。BGR 可创建不代表现有模型接受它，模型预处理继续拒绝非灰度输入。
- 三个模型创建期间执行可用的静态输入/输出检查。Backbone 动态输入范围必须接受 127 和 255；已知特征轴必须与 Head 输入相容。动态输出仍由真实执行边界验证。
- 跟踪器首先组装检测器、NanoTrack/DCF 和滤波器，然后启动检测线程。OFF/ON 仍保留模型并调用 reset；最终析构显式停止并等待检测线程。
- 两个 YAML、launch、ROS 话题、状态枚举及模型数值处理没有修改。

## 相机最小清理修复的静态审查

该修复已于 2026-09-10 获得范围确认。提案、设计 §5.1、任务 2.5 和平台工厂规格新增两个失败场景，均已同步。相机改动仅作静态审查，不以公共测试代替 Jetson 执行验证。

| 失败或停止路径 | 回收关系 | 静态审查结果 |
|---|---|---|
| 已初始化但尚未采集，随后模型创建失败 | 帧源析构释放相机；相机 stop 不再提前返回，关闭设备并释放已分配缓冲区 | 符合新增场景 |
| 设备打开、查询、格式设置或缓冲区分配中途失败 | init 包装原 initDevice 主体；任意失败返回均因未初始化而 cleanup | 已逐个检查原主体的提前返回 |
| 初始化抛出普通异常 | init 先清理后重新抛出；构造异常再释放 VPI stream 和 JPEG 编码器 | 资源不依赖构造失败后的析构 |
| VPI 或 JPEG 资源创建失败 | 立即释放已取得的原生资源；工厂通过 is_initialized 拒绝该相机并保留输入路径诊断 | 不返回半初始化帧源 |
| 开始设备采集失败 | STREAMON 失败后清理本轮设备和缓冲区，未启动线程 | 回收路径明确 |
| 触发线程或采集线程创建抛出异常 | run/run_async 调用 stop，等待已创建线程，再停止设备并回收资源；平台 start 转换普通异常为 false | 不遗留先创建的触发线程 |
| 重复 stop 或 cleanup | 按 joinable、有效文件描述符、非空指针逐项操作；释放后置空或清空容器 | 无基于 initialized 的清理遗漏 |
| 正常 OFF/ON | 原生 VPI/JPEG 资源保留，下一次 start 沿用原初始化流程 | 正常模型和录像语义不变 |

通过与修改前提交的函数主体逐字对照，确认原相机初始化主体（现名 `initDevice`）、`captureLoop` 和 `triggerLoop` 保持原样。队列容量、丢帧策略、时间戳及录像处理均未修改。清理时先关闭设备，再释放已排队的主机缓冲区；已启动线程在此之前结束。

新增规格“相机尚未采集时模型创建失败”和“相机部分初始化或线程启动失败”由以上源码路径审查覆盖。公共回归在修复后再次通过；Jetson 运行期失败注入仍在本变更范围外。

## 2026-09-10 帧源测试覆盖警告修复

核验发现，原 `testSourceAndFrames()` 在测试中重写取最新帧循环，只验证模拟对象，无法捕获节点公共调用逻辑的回归。该 WARNING 已修复，任务 5.5 的勾选状态现在有共享生产代码测试作为依据。

- 新增 `src/platform/frame_source_ops.h`，节点和测试共用 `waitForLatestFrame()` 与 `configureFrameSourceRecording()`。前者执行等待及排空，后者传递录像配置；两者均不创建线程或队列。
- `platform_contract_test.cpp` 不再重写消费循环。模拟源只提供可控队列和调用计数，实际消费顺序由公共函数决定。
- 新断言覆盖等待参数、超时返回、超时后不调用非阻塞取帧或首帧回调、多帧取最新、单帧及省略回调的路径。另检查首帧回调发生在排空之前，以保持节点计时位置。
- 录像测试通过共享配置函数检查开启和关闭两组参数，确认配置发生在模拟采集启动前。重复停止和重新启动用例保留。
- 在上述 Linux ARM64 镜像、GNU C++ 11.4.0、OpenCV 4.5.4 环境重新构建，6/6 个公共测试目标通过。公共头文件独立编译检查包含新增辅助头文件。

复现命令与“环境与复现”一节相同，可追加 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 生成编译数据库。容器未安装 `clang-tidy` 或 `clangd`，仓库未配置其他 C++ lint 命令，因此未运行这些检查。

本次测试覆盖节点使用的公共辅助函数，不覆盖 ROS 参数读取、触发回调或 Jetson 原生适配执行。节点调用点已静态核对；仍未配置或编译 Jetson 程序，不将公共测试结果表述为端到端或设备验证通过。

## 未执行项与限制（仍适用）

- 本次不配置、编译或运行 Jetson 生产目标，不运行真实相机、视频回放、压力程序或 `vpi_race_repro`。这些执行验证不属于本变更范围，也不据此给出通过结论。
- 未使用真实模型或设备，不声称模型等价、实际像素完全一致、性能达标或设备资源安全。
- 构建容器未提供 `clang-tidy` 和 `clangd`，仓库没有配置的 lint/语言服务器检查命令。宿主机工具没有该 Linux ARM64 编译数据库所需的头文件环境，因此未把宿主机检查当作等效验证。
- `git diff --check` 通过。未执行提交、同步主规格或归档。
