# extract-model-execution-backends 验证报告

日期：2026-09-10。对象：`rk3576` 分支当前工作区，包括尚未提交的新文件。采用 `openspec-verify-change` 流程，核对 proposal、design、tasks 和三份增量规格（本次新增或修改的行为要求）。同时阅读已有实施验证记录，但以下复跑结果来自本次检查。

## 结论

未发现阻止归档的缺失实现或设计偏差。按照本变更约定的公共测试和静态审查范围，可以归档。保留 1 项设备验证警告；本结论不代表 Jetson 部署验收。本次未修改生产代码，也未执行归档。

| 维度 | 状态 |
|---|---|
| 完整性 | 25/25 任务已完成；12/12 要求找到实现 |
| 正确性 | 核对全部 28 个场景；公共测试通过，设备场景只有静态或模拟证据 |
| 一致性 | 两层接口、所有权、模板状态、默认装配和公共算法符合设计 |

## 问题分级

### CRITICAL：归档前必须解决

无。未发现未完成任务或缺失的规格实现。

### WARNING：应补足的验证证据

1. **Jetson 生产执行路径没有目标环境验证。** `src/hybrid_tracker/model/jetson/tensor_rt_model_executor.cpp:202` 的提交、同步及失效处理与设计相符，但本次测试没有执行这些设备调用。`tests/model_contract_test.cpp` 使用模拟执行器；致命故障测试仅验证生产退出函数的非零退出与不执行栈展开。它不能验证 CUDA 同步行为。

   同一缺口涉及真实双输入描述、不支持的输出类型加载、平台存储拒绝、模型文件读取失败、同步完成后的缓存复用、提交失败后实例失效，以及默认生产创建。这些场景有静态实现证据，不能标记为设备实测通过。

   建议：在 Jetson SDK 环境编译生产目标，用现有三个引擎完成固定输入对照；覆盖错误模型路径、类型或平台不匹配、提交失败与同步失败。随后补充录像框和状态序列、长时间压力及设备竞态验证，并记录 SDK、模型和输入身份。设计第 7 节明确将这些检查留作后续证据，因此该警告不阻止本次归档。

### SUGGESTION：可选改进

无额外建议。

## 要求与实现映射

以下路径均相对于 `src/hybrid_tracker/`。张量是模型处理的多维数组；所有权引用负责保证底层数据在使用期间存活。

| 规格要求 | 实现与验证证据 |
|---|---|
| 查询平台无关模型描述 | `model/model_executor.h` 定义名称、顺序和动态范围；设备加载读取原生元数据；双输入模拟与独立头文件编译通过 |
| 像素变换和量化分离 | `model/tensor.h` 独立量化字段；`preprocess/model_input.h` 保留像素乘加；公共拒绝测试通过，真实类型读取路径静态核对 |
| 执行前校验输入和输出 | `model/validation.cpp` 检查数量、形状、类型、字节数和溢出；设备执行器补充原生存储、形状解析和步长检查 |
| 同步后输出可读、输入可释放 | 设备执行器在返回前同步；模拟租约测试通过；真实设备时序未执行 |
| 张量所有权贯穿串联 | `Tensor`、`HostTensorView` 持有共享存储；旧输出跨执行器生命周期、Head 特征消费和输入租约测试通过 |
| 明确诊断与资源边界 | `Diagnostic` 分类错误；加载成员自动回收资源；提交失败后失效，同步无法确认停止时立即退出；设备故障未实测 |
| YOLO 返回兼容原始预测 | `JetsonYoloBackend::infer` 和 `yoloPrediction` 验证首输出、批次、三维布局及属性数；合法和非法输出测试通过 |
| NanoTrack 模板建立与重置 | `setTemplate` 先清除旧模板；`reset` 可重复调用；新目标失败与重置后禁止搜索测试通过 |
| NanoTrack 具名分类和位置输出 | `search` 按模板、搜索顺序调用 Head；`nanoPrediction` 按完整形状分配唯一角色；颠倒、缺失、重复、额外输出及恢复测试通过 |
| 保持公共检测与跟踪行为 | `yolo_detector.cpp`、`nano_tracker.cpp` 保留公共公式；真实调用链覆盖 ROI 提前命中、搜索失败后状态和输出框保持 |
| 默认装配与无硬件验证 | `hybrid_tracker.cpp` 保持三个引擎路径和匹配预处理器；独立 CMake 目标编译真实模型串联及公共算法 |
| 正式后端驱动真实调用链 | `preprocess_contract_test.cpp` 覆盖搜索预处理失败、单帧会话、ROI 命中和输入租约；不复制生产算法 |

## 一致性核对

- 对照 `git show jetson:src/hybrid_tracker/nano_tracker.cpp`：模板和搜索均选择 Backbone 首输出；Head 仍按模板在前、搜索在后的顺序接收输入。
- 对照 Jetson 原执行器：保持映射存储与同步推理方式，增加类型、形状和故障处理。结果存储独立于执行器存活。
- 检查当前 diff：NanoTrack 公共后处理公式未改变；YOLO 后处理的修改限于只读指针限定。ROS 配置及状态定义不在本次改动内。
- YOLO 拒绝会进入旧不安全转置解码分支的输出布局。此限制已在实施记录解释，与设计的“不兼容输出拒绝”边界一致；没有真实引擎证据证明该限制影响现用模型。
- `src/` 和根 CMake 文件未发现旧 `model_calls` 或 `trt_engine` 路径引用。公共测试实际链接命令不含 CUDA、TensorRT、VPI 或 RKNN。

## 本次重新执行的检查

公共构建使用本地 Docker ARM64 Linux 镜像 `roc-rk3576-ros2-build:humble-arm64`，GNU C++ 11.4.0、OpenCV 4.5.4。以 `--rm` 临时容器挂载当前仓库。未使用真实模型或录像；测试输入为代码构造的灰度帧、特征与预测数组。

容器内从仓库根目录执行：

```bash
cmake -S src/hybrid_tracker -B .docker-artifacts/model-review/build \
  -DDRONE_TRACKER_PREPROCESS_TESTS_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build .docker-artifacts/model-review/build -j4
ctest --test-dir .docker-artifacts/model-review/build --output-on-failure
```

结果：`preprocess_common_tests`、`preprocess_contract_tests`、`model_contract_tests` 全部通过，3/3，无失败。

macOS 上使用 `clang++ -std=c++17 -Wall -Wextra -Werror -Isrc/hybrid_tracker -x c++ -fsyntax-only` 分别编译 `tensor.h`、`model_executor.h`、`model_backend.h`、`validation.h` 和 `jetson_model_backend.h`。五个头文件全部通过，未提供硬件 SDK 或 OpenCV 包含路径。

`git diff --check` 和 `openspec validate extract-model-execution-backends --strict` 均通过。

未跳过规划工件核对。因本地缺少 Jetson 工具链和运行环境，跳过生产目标编译、真实模型数值与录像对照、设备压力和故障注入。未执行 RK3576 实机推理；本变更不实现 RKNN 后端。
