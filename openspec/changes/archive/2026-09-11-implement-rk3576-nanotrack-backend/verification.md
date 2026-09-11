# implement-rk3576-nanotrack-backend 核验报告

核验日期：2026-09-11。对象为 `rk3576` 分支当前工作区，包括尚未提交的实现和工件。使用 `openspec-verify-change`，按 spec-driven 流程核对 proposal、design、tasks、两份增量规格，以及补充验证记录。首次核验仅新增本报告；随后按用户要求修复两项文档警告，更新提案、平台说明、迁移进度、验证记录及 CMake 说明文字。未修改算法、构建条件、任务勾选或执行归档。

| 维度 | 结果 |
| --- | --- |
| 完整性 | 29/29 任务已勾选；13/13 条要求找到实现或文档证据 |
| 正确性 | 26 个规格场景均有对应处理、测试或适用的验证记录；本次重跑 16/16 CTest 通过 |
| 一致性 | 职责划分符合设计；2 项文档警告已修复，当前无未解决警告 |

## CRITICAL：归档前必须修复

未发现未完成任务或缺失的要求实现。任务完成判断结合代码与现有证据，不仅依据勾选状态。

## WARNING：已解决

1. **目标兼容版本与历史构建版本已统一区分。** proposal.md、RK3576_REFACTORING_PLAN.md、src/platform/README.md 及 src/platform/rk3576/CMakeLists.txt 统一说明目标 Runtime 2.3.2、驱动 0.9.8。2.3.0 明确表示本次沿用的构建 SDK，原镜像与实际验证记录保留。design.md 已区分构建产物和目标版本，无需改写。
2. **当前迁移状态与历史阻塞记录已分开。** RK3576_REFACTORING_PLAN.md 的现状、阶段 4 和决策记录已更新；validation.md 新增当前任务状态表。修复前命令、查询失败和当时未完成项明确标为历史记录，并链接 FP16 修复与数值对照。当前状态不再依赖读者合并多份阶段结论。

修复复核：OpenSpec 严格校验和 `git diff --check` 通过。此次仅修改文档及 CMake 说明文字，没有新增测试或重复执行构建、硬件回放；下文 16/16 测试是首次核验阶段实际执行的结果。

## SUGGESTION：可选改进

无额外建议。短时共存测试和单视频评估的限制已经明确记录，不将范围外的长期验收列为本变更缺失任务。

## 要求与场景对应

下表路径以仓库根目录为起点。场景覆盖包含自动测试、源码分支和记录审查，不表示每个场景都在真实硬件上重新执行。

| 规格要求 | 实现证据 | 场景与检查证据 |
| --- | --- | --- |
| 单模型查询与多输入输出 | `src/platform/rk3576/rknn_session.cpp:75`、`:114` | rknn_session_test.cpp：双输入双输出、零维度及尺寸溢出拒绝 |
| 提交前校验全部输入 | `src/platform/rk3576/rknn_session.cpp:188` | 第二输入少一字节、重复索引、缺失输入、外来存储等均在 inputs_set 前拒绝 |
| 类型转换保持数据语义 | `src/platform/rk3576/rknn_session.cpp:75`、`:188` | 会话测试保留 1.0/128.0；floatingAffineMetadata 覆盖浮点 AFFINE 元数据；四组合固定输入记录 |
| 同步返回和独立所有权 | `src/platform/rk3576/rknn_session.cpp:188` | 多次推理后保留旧输出；销毁会话及后端后旧结果仍可读取 |
| 失败与设备停止边界 | `src/platform/rk3576/rknn_session.cpp:165`、`:174` | 会话故障注入；rknn_runtime_test.cpp 的子进程测试检查等待/销毁失败时终止 |
| 保持已有 YOLO 行为 | `src/platform/rk3576/rknn_yolo_contract.cpp:7`、rknn_runtime.cpp | 双输入拒绝、类别数、异步停止等待测试；yolo-regression.json 记录真实前后对照 |
| 兼容模型路径替换 | `src/platform/rk3576/rknn_nanotrack_backend.cpp:89`；`src/platform/README.md:87` | 三路径加载，无身份白名单；文档明确相同尺寸不能证明分类及特征语义兼容 |
| 三模型接口完整匹配 | `src/platform/rk3576/rknn_nanotrack_contract.cpp:14` | Head 输入输出换序、重复角色、额外输出、错误形状和第三文件失败回收 |
| 不同精度共用调用链 | `src/platform/rk3576/rknn_nanotrack_backend.cpp:21`、`:158` | checkCombinations 独立逐元素验证排列及实数传递；四组合真实数值对照 |
| 灰度预处理浮点语义 | `src/platform/rk3576/nanotrack_preprocessor.cpp:21` | 独立插值参考覆盖边缘、奇偶裁剪、非整数值；均值回退、租约及生命周期测试 |
| 模板与搜索失败语义 | `src/platform/rk3576/rknn_nanotrack_backend.cpp:141`、`:158` | 新模板失败清除旧目标；普通输入错误保留模板；会话失效返回未就绪；重复 reset |
| 独立查询与真实回放 | src/platform/rk3576/rk3576_nanotrack_probe.cpp、CMakeLists.txt | 真实 NanoTracker 固定框/置信度测试；四组真实视频；参数失败及完整平台选择测试；失败帧分支不输出框 |
| 区分实现正确性与模型质量 | numeric-validation.md、fp16-fix.md、validation.md | 同模型固定输入参考与灰度质量独立报告；历史仅有构建时明确未完成项；未改公共阈值掩盖差异 |

设计中三文件装配、Float32 特征交接、NCHW/NHWC 排列转换、先浮点后缩放、独立输出存储、失败先确认设备停止、保持完整平台未启用均有对应实现。`RknnRuntime` 已成为通用会话的薄包装，没有第二份 SDK 执行流程。公共算法和 ROS 配置不在本次实现差异内。另用 `git show jetson:src/hybrid_tracker/nano_tracker.cpp` 核对了参考裁剪流程；这不构成跨硬件数值等价证据。

## 本次执行与已有证据

本次运行 `openspec validate implement-rk3576-nanotrack-backend --strict` 和 `git diff --check`，均通过。

按 AGENTS.local.md 的四个目录挂载约定，使用现有 `roc-rk3576-ros2-build:humble-arm64` 镜像启动带 `--rm` 的临时容器，执行 `.docker-artifacts/nanotrack/test-all.sh`。该脚本重新构建 nanotrack-contracts，然后运行 CTest，16/16 通过，测试耗时 1.79 秒。检查独立 NanoTrack 程序动态依赖，未发现 ROS、CUDA、TensorRT 或 VPI 依赖。容器命令退出 0。测试日志保留在 `.docker-artifacts/build/nanotrack-contracts/Testing/Temporary/LastTest.log`。

现有实机证据的平台为 RK3576、Linux 6.1.141、Runtime 2.3.2、驱动 0.9.8。模型为板上现有 NanoTrackV3 FP16、INT8 和专属混合精度 Head；哈希见 numeric 各组合的 comparison.json。输入为 V_DRONE_051.mp4 的 309 帧灰度回放，以及同视频首帧的固定浮点裁剪。

- numeric-validation.md 和比较脚本记录：每组合 5 次执行、50 项比较，四组最大绝对差均为 0，容差为绝对和相对各 1e-5。RKNNLite 参考独立完成排列和模型串联，但共用同一硬件 Runtime，不是对 RKNN 内核或模型转换前 ONNX 的独立证明。
- fp16-fix.md 及 evidence/fp16-fix 记录：四组合各完成 308 次搜索，并报告框交并比、中心误差、置信度和延迟。有效框数量不能代表准确率。
- evidence/yolo-regression.json 记录：309 帧提取前后输出一致，固定图像六个原始输出一致，五类失败行为一致。
- evidence/coexistence.log 记录：20 轮、约 18.1 秒，200 次模板、搜索及 YOLO 结果；停止后线程数回到 1，热身后内存未持续上升。此记录不证明长期稳定或 NPU 设备内存无泄漏。

本次没有重新执行 RK3576 模型推理、回放或共存负载，以上硬件结论来自已有记录与脚本核对。Jetson 构建、VPI/OpenCV 实机插值差异、完整 RK3576 平台/相机/ROS 验收、长期性能测试未运行；这些边界已在设计及验证记录中明确。完整性、正确性和设计一致性三个维度均已检查，没有因缺少规划工件跳过维度。

## 归档判断

未发现 CRITICAL 问题，两项文档 WARNING 已解决。按本变更的独立后端范围，核验通过，可进入归档流程。此结论不代表完整 RK3576 跟踪平台或 Jetson 等价性验收通过；本次未执行归档。
