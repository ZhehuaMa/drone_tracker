# 任务 1.1～5.4 实现与构建验证

日期：2026-09-11。变更：`implement-rk3576-nanotrack-backend`，流程：`spec-driven`。

本记录只覆盖通用会话、YOLO 迁移、NanoTrack 后端与预处理、独立入口和构建测试。第 6 组实机验证与第 7 组最终文档任务仍未完成。本记录不替代第 6.6 项要求的完整实机证据汇总。

## 实现结果

- `RknnSession` 管理一个模型的加载、属性查询、同步执行和资源回收。输入在设备提交前统一校验数量、索引、固定尺寸、类型、布局、字节数与存储所有权。
- `RknnRuntime` 保留原 YOLO 入口，成为通用会话的薄包装。YOLO 的 640×640 输入、INT8 量化参数、六输出结构与类别数检查留在 `rknn_yolo_contract.*`。
- NanoTrack 使用模板 Backbone、搜索 Backbone、Head 三个文件。Backbone 提取图像特征，Head 比较模板和搜索特征。三者全部加载且契约匹配后才返回组件。
- Head 输入和输出按唯一逻辑形状确定角色，不依赖名称或固定顺序。NCHW 表示批次、通道、高、宽；NHWC 表示批次、高、宽、通道。平台后端负责两种排列之间的转换，公共输出固定为 Float32 NCHW。
- Float32 是 32 位浮点。FP16 和 INT8 模型通过 Float32 特征交接，SDK 使用 `pass_through=0` 转换输入，使用 `want_float=1` 返回浮点输出。SDK 输出只复制一次，不再次反量化，也不额外归一化。
- 灰度裁剪复用公共几何与 `TargetMean`。先转 Float32，再线性缩放并复制成三平面。均值按现有规则取整；均值映射失败时回退为 114。缓存租约保证旧输入存活期间不会被覆盖。
- `setTemplate()` 开始时清除旧模板。普通搜索输入错误保留当前模板；SDK 会话失效后需要重新创建后端。原有公共跟踪算法、ROS 接口和完整平台选择未修改。

## 验证平台与版本

使用 `AGENTS.local.md` 指定的 `roc-rk3576-ros2-build:humble-arm64` 镜像，GCC 11.4.0、OpenCV 4.5.4。每次启动带 `--rm` 的临时容器，仓库挂载到 `/work/ros2_ws/src/drone_tracker`，build/install/log 按本地约定保存到 `.docker-artifacts/`。

本次按用户明确确认的 OpenSpec 基线使用 RKNN Runtime **2.3.0**。已有本地动态库的版本字符串为 `2.3.0 (c949ad889d@2024-11-07T11:35:33)`。工作区 `AGENTS.md` 中另有 2.3.2 修改，本次保留该修改，但没有执行或宣称 2.3.2 验证。

使用既有 SDK 副本，没有下载或更新 SDK，也没有把 SDK、模型或构建产物加入源码。

| 文件 | SHA-256 |
| --- | --- |
| `rknn_api.h` | `340f16c14bc86d41bc5a64251bd1e747dd50899a4438d08707b48d8de384165a` |
| `librknnrt.so` | `73993ed4b440460825f21611731564503cc1d5a0c123746477da6cd574f34885` |

SDK 注入测试报告的 Runtime 2.3.0 / 驱动 0.9.8 是模拟返回值。容器没有执行 NPU 推理，也没有查询当前开发板驱动，不能将其视为实机版本证据。

## 构建与测试命令

在上述容器内执行：

```bash
cmake -S /work/ros2_ws/src/drone_tracker/src/platform/rk3576 \
  -B /work/ros2_ws/build/nanotrack-contracts \
  -DRKNN_INCLUDE_DIR=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/include \
  -DRKNN_LIBRARY=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/lib/librknnrt.so \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /work/ros2_ws/build/nanotrack-contracts -j4
ctest --test-dir /work/ros2_ws/build/nanotrack-contracts --output-on-failure

cmake -S /work/ros2_ws/src/drone_tracker/src/hybrid_tracker \
  -B /work/ros2_ws/build/nanotrack-common \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /work/ros2_ws/build/nanotrack-common -j4
ctest --test-dir /work/ros2_ws/build/nanotrack-common --output-on-failure
```

独立工程 **16/16** 测试通过，原公共工程 **8/8** 测试通过。公共测试也包含在独立工程中，两组数量不能相加解释为 24 种不同测试。

| 测试 | 覆盖与结果 |
| --- | --- |
| `rknn_session_tests` | 双输入完整提交；缺失/重复/越界索引；第二输入少一字节；空存储；尺寸乘法溢出；类型/布局不匹配；非法量化与动态尺寸；独立 Float32 输出；提交、执行、获取、释放失败 |
| `rknn_runtime_tests` | YOLO 类别数 1、4、6、64；FP16/INT8/Float32 输出；双输入误用拒绝；部分加载回收；旧输出存活；运行中停止等待；无法确认停止和销毁失败时子进程终止 |
| `nanotrack_preprocess_tests` | 奇偶裁剪、图像四周填充、放大/缩小、非整数像素、三个通道一致；114 回退；缓存忙；重置和新模板失败使旧均值失效；非连续行、非法映射、帧和缓存生命周期 |
| `rknn_nanotrack_backend_tests` | 四种 Backbone/Head 精度组合；NCHW/NHWC 混合；Head 顺序交换；逐元素特征交接；错误特征形状、额外/重复/缺失输出、部分加载失败；旧模板失效和会话失效 |
| 真实 `NanoTracker` 集成样例 | 使用真实 RKNN 后端、真实灰度预处理和公共跟踪算法，仅替换 SDK 输出。固定中心候选对应置信度 0.9，框为 `(49.5,59.5,20,20)`；搜索失败不更新框、不改变既有初始化状态语义 |
| 命令参数测试 | NanoTrack 缺失参数和非法框失败退出；原 YOLO 参数测试继续通过 |
| 公共算法与平台测试 | 原模型契约、裁剪几何、YOLO 解码、状态逻辑、工厂、公共头文件编译和完整平台选择拒绝测试通过 |

独立浮点裁剪参考直接按像素中心计算双线性插值，不调用生产裁剪函数或 OpenCV 缩放。固定输入为 7×5 灰度图，像素 `25*x + 7*y`，均值为 89；覆盖裁剪尺寸 4、5、6、9、257，输出为 127 或 255。允许绝对误差小于 `5e-4` 灰度值；同时检查确有非整数输出。

布局转换使用通道与像素位置可区分的固定实数，逐元素误差小于 `1e-6`。公共跟踪样例的置信度误差小于 `1e-5`，框各分量误差小于 `1e-4` 像素。这些容差用于确定性软件测试，不是实机量化模型验收容差。

## 独立入口

新增 `rk3576_nanotrack_probe`，原 `rk3576_yolo_probe` 仍可构建。

查询示例中的路径是占位示例，本次没有加载这些文件：

```bash
./rk3576_nanotrack_probe \
  --template-model /models/template.rknn \
  --search-model /models/search.rknn \
  --head-model /models/head.rknn --mode query
```

查询输出三模型路径、全部输入输出属性、版本以及 Head 角色索引。回放使用相同三个路径，并增加 `--mode replay --video /data/clip.mp4 --bbox 50,60,20,20`。彩色帧明确转成灰度，再进入真实公共跟踪器。输出列为 `frame,success,x,y,w,h,confidence,elapsed_ms`；首帧只建立模板，置信度留空；普通失败帧的框和置信度留空。耗时包含灰度帧准备与跟踪，不包含视频解码。

本次完成入口编译及非法参数测试；真实模型查询、真实视频成功回放和模型语义等价性留在第 6 组验证。模型提供方仍需保证颜色、归一化、特征含义及 Head 原始分类/距离含义相符，形状匹配不证明语义匹配。

## 静态检查与隔离

- 本机采用 macOS SDK、既有 OpenCV/RKNN 头文件副本和独立编译数据库，检查 18 个平台及测试 C++ 翻译单元。`clang-tidy --checks=-*,clang-analyzer-*` 无诊断；`clangd --check --tweaks=none` 均为 0 errors。最后的量化检查修改对两个相关翻译单元重新检查并通过。
- `readelf -d` 显示 NanoTrack 工具直接依赖 RKNN、OpenCV videoio/imgproc/core 和通用 C/C++ 库。`ldd` 未发现 CUDA、TensorRT、VPI、Tegra 或 ROS 依赖。
- 公共头文件编译目标通过，根目录平台选择代码未修改。完整 `rk3576` 平台请求仍按既有规则拒绝。
- `git diff --check` 与 `openspec validate implement-rk3576-nanotrack-backend --strict` 通过。
- 构建日志、CTest 日志、静态检查日志和动态库依赖记录保存在忽略目录 `.docker-artifacts/nanotrack/`；ARM64 可执行文件位于 `.docker-artifacts/build/nanotrack-contracts/`。

## 未执行项

本次没有使用真实模型文件、录制视频或 RK3576 NPU。四种精度的实机查询、浮点特征对照、灰度回放质量/耗时、真实 YOLO 回归和长期资源/共存测试均未执行，相关任务保持未勾选。

当前本地没有 Jetson SDK 与硬件环境，未执行 Jetson 构建或跨平台实机对照。完整 RK3576 采集、平台工厂与 ROS 节点验收也不在本次范围。ARM64 容器构建及注入测试不替代这些检查。
