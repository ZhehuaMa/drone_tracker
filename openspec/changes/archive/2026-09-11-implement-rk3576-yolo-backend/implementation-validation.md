# 任务 1.1–5.6 实现与验证记录

验证日期：2026-09-10。变更：`implement-rk3576-yolo-backend`。

本记录是任务 1.1–5.6 完成时的历史快照，只覆盖这 19 项工作。在该阶段，第 6 组实机验收和第 7 组完整交付任务尚未勾选，也没有加载真实模型或进行真实图像、录像、Jetson 实机及性能验收。随后完成的 RK3576 实机与交付结果见 [board-validation.md](board-validation.md)，当前独立验证结论见 [verification.md](verification.md)。下文“本次”均指任务 1.1–5.6 的历史验证阶段。

## 实现范围

- 公共 YOLO 输入明确区分 Float32 NCHW 和 UINT8 NHWC。两者分别表示浮点数按批次/通道/高/宽存储，以及字节按批次/高/宽/通道存储。NanoTrack 原检查保持不变。
- 后端在检测线程启动前提供已校验的类别数。公共预测固定使用 `[1,4+C,N]`，其中 C 是类别数，N 是候选框数。
- 六输出适配器执行 DFL 距离期望和一次 Sigmoid。前者把每条边的 16 个离散分数转成距离，后者把类别原始分数转成概率。候选按 80、40、20 三个尺度的行优先顺序连接。
- 检测器只读取配置类别的分数，再执行原有坐标还原、大小过滤和 NMS。NMS 用于去掉高度重叠的重复框。其他类别不参与竞争。两个 YAML 的默认编号仍为 0。
- 新增 RKNN Runtime 封装、字节预处理器、自有灰度主机帧、正式 YOLO 后端和 `rk3576_yolo_probe` 工具。公共接口不包含 RKNN SDK 类型。
- 完整 RK3576 生产工厂仍未提供。顶层平台选择继续明确拒绝完整 RK3576 生产构建。

## 环境与依赖来源

构建使用 `AGENTS.local.md` 指定的 `roc-rk3576-ros2-build:humble-arm64` 镜像。编译器为 GCC 11.4.0，OpenCV 为 4.5.4。每次使用 `docker run --rm` 创建临时容器，仓库挂载到 `/work/ros2_ws/src/drone_tracker`，build/install/log 分别绑定到仓库的 `.docker-artifacts/`。

仅从既有开发板读取 `/usr/include/rknn_api.h` 和 `/usr/lib/librknnrt.so`，保存到忽略目录 `.docker-artifacts/rknn/` 供编译和链接。没有把 SDK、模型或构建产物加入源码。

| 文件 | SHA-256 |
| --- | --- |
| rknn_api.h | `340f16c14bc86d41bc5a64251bd1e747dd50899a4438d08707b48d8de384165a` |
| librknnrt.so | `73993ed4b440460825f21611731564503cc1d5a0c123746477da6cd574f34885` |

本次 Runtime/驱动版本查询的自动化测试使用模拟的 `2.3.0`/`0.9.8` 返回值。该结果不能替代任务 6.1 的真实 C++ 模型与版本查询。

## 构建命令及结果

下列命令在上述 ARM64 容器内执行。SDK 路径是前述只读副本。

```bash
cmake -S /work/ros2_ws/src/drone_tracker/src/platform/rk3576 \
  -B /work/ros2_ws/build/rk3576-yolo \
  -DRKNN_INCLUDE_DIR=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/include \
  -DRKNN_LIBRARY=/work/ros2_ws/src/drone_tracker/.docker-artifacts/rknn/lib/librknnrt.so
cmake --build /work/ros2_ws/build/rk3576-yolo -j4
ctest --test-dir /work/ros2_ws/build/rk3576-yolo --output-on-failure

cmake -S /work/ros2_ws/src/drone_tracker/src/hybrid_tracker \
  -B /work/ros2_ws/build/yolo-common \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON
cmake --build /work/ros2_ws/build/yolo-common -j4
ctest --test-dir /work/ros2_ws/build/yolo-common --output-on-failure
```

独立工程 **11/11** 测试通过。原无硬件公共入口 **8/8** 测试通过。

| 检查 | 证据 |
| --- | --- |
| 两种 YOLO 输入与 NanoTrack 保持原契约 | `preprocess_contract_tests` 和公共头文件编译目标 |
| 公式、候选顺序、非法输出 | `yolo_output_adapter_tests` 覆盖 C=1、4、6、64、8401，完整逐元素参考使用 `atol=1e-4, rtol=1e-5`；另校验 C=9000 描述、错误数量/顺序/尺度、非有限数和溢出 |
| 单类别与三级级联 | `preprocess_contract_tests` 使用真实检测器，覆盖目标不是全类别最大分、其他类别重叠干扰、最高分选择、越界编号阻止启动、ROI 命中、全图和切片回退、C=9000 大属性轴 |
| 快照与缓存租约 | `rknn_preprocess_tests` 精确检查 0/114/255、159/160 奇数边距、非连续行、源帧释放、映射失败、非法区域与输入占用 |
| Runtime 契约与故障 | `rknn_runtime_tests` 注入部分初始化、提交/run/get/release 失败、错误输出字节数；检查 INT8/FP16 混合与 Float32 描述、旧输出独立存活、运行中 stop 和重复 stop |
| 致命错误 | `rknn_runtime_tests` 在隔离子进程内检查无法确认停止和销毁失败，避免测试主进程被终止 |
| 工具参数 | `rk3576_probe_missing_args` 与 `rk3576_probe_invalid_roi` 检查错误参数失败退出 |
| 原算法及工厂 | `model_contract_tests`、`preprocess_common_tests`、`hybrid_tracker_tests`、`platform_contract_tests`、`platform_selection_tests` 均通过 |

容差含义：每个元素满足 `|实际值-参考值| <= atol + rtol*|参考值|`。没有用量化模型输出误差替代适配器数学正确性。

## RKNN 失败与所有权边界

依据取得的 API 头文件：`rknn_outputs_get` 成功时等待推理完成；`rknn_wait` 用于等待模型执行；`rknn_outputs_release` 释放成功取得的非预分配输出。负错误码本身不作为设备已经停止的证据。

| 状态/失败 | 行为 |
| --- | --- |
| 初始化中途失败 | 通过自有 Runtime 对象回收已经取得的 context |
| inputs_set 失败 | 尚未调用 run，失效并销毁 context，不返回输出 |
| run/get 失败 | 先调用 wait；成功才失效并销毁，失败则通过既有致命诊断立即终止，不展开栈释放输入 |
| get 成功 | 设备已经完成，将每份逻辑连续 Float32 输出复制到自有存储，再 release |
| 复制/字节数/release 失败 | 不返回部分输出，不重复 release，失效并销毁 context |
| 工作线程停止 | stop 等待线程完成，随后对象销毁才释放 Runtime |

get 失败时不对未成功取得的 SDK 指针建立输出租约；确认停止后由 context 销毁回收 SDK 状态。所有设备故障在本次均为注入验证，实机故障和有限持续运行仍由任务 6.4 验收。

## 构建隔离与静态检查

- 使用 `readelf -d` 和 `ldd` 检查工具。直接依赖为 RKNN、OpenCV 和通用 C/C++ 系统库；没有 CUDA、TensorRT、VPI、Tegra 或 ROS 库。
- 使用新的 `rk3576-missing-sdk` 构建目录且不传 SDK 路径，配置按预期失败。诊断包含 Runtime 2.3.0、驱动基线 0.9.8、`RKNN_INCLUDE_DIR` 和 `RKNN_LIBRARY`。
- 容器内没有 clang-tidy/clangd。本机使用 macOS SDK、复制的同版本 OpenCV/RKNN 头文件以及独立静态检查编译数据库，对 18 个新增或修改的 C++ 翻译单元运行 clang-tidy 的 `clang-analyzer-*`，最终没有未抑制诊断。原测试中的非法枚举注入保留既有 NOLINT。
- 对同一组文件运行 clangd `--check`，禁用其自动重构自检（`--tweaks=none`）后均为 0 errors。原始自动重构自检出现工具内部替换重叠和提取函数错误；没有用这些错误替代代码诊断。
- 本机静态检查不作为 ARM64 构建或 Jetson 实机证明。ARM64 构建结果来自上述约定容器。
- `git diff --check` 和 `openspec validate implement-rk3576-yolo-backend --strict` 通过。

本地详细日志保留在 `.docker-artifacts/log/yolo-final-tests.log`、`.docker-artifacts/log/yolo-validation.log`、`.docker-artifacts/log/rk3576-missing-sdk.log` 和 `.docker-artifacts/rknn/lint/`。这些路径是本地验证产物，不随源码交付。
