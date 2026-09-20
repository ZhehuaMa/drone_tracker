# 实现核验：add-nanotrackv3-performance-baseline

核验日期：2026-09-20。使用 `openspec-verify-change`，按仓库本地 `spec-driven` 流程核对 proposal、design、delta spec、tasks、实现和验收证据。初次核验未修改实现。随后按用户要求先补齐 WARNING 2 的测试，再修复 WARNING 1 的构建身份更新机制；未同步或归档变更。

## 结论

未发现 CRITICAL（归档前必须修复的缺失任务或未实现需求）。原有 2 项 WARNING 均已修复并验证，当前没有未处理的严重问题或警告，可归档。

| 维度 | 结果 |
|---|---|
| 完整性 | 30/30 项任务已勾选（含 W2/W1 补充任务 5.6、5.7）；10/10 项需求找到实现证据 |
| 正确性 | 17 个规格场景均找到相关实现；中断及中间失败的数据保留断言已补齐 |
| 一致性 | 仅 NanoTrackV3、指定目录、生产亮度路径、唯一端到端指标与设计一致；源码快照更新机制已通过增量构建验证 |

## CRITICAL

无。没有仅凭任务勾选推断实机完成：本次读取了原始帧记录、区间记录、汇总、身份信息和历史验收记录，并重新运行独立数据核对程序。

## WARNING

无未处理警告。

## 已修复的 WARNING 1

`baseline/code_identity.cmake` 建立每次执行的构建目标，`nanotrack_baseline_runner` 明确依赖它。这里 `baseline/` 指 `tests/nanotracev3_performance_benchmarks/baseline/`。

`generate_code_identity.cmake` 在构建阶段查询提交号、差异及相关新增源码，包含新加的 `.cmake` 构建脚本。Git 命令失败立即阻断构建。生成头文件沿用 `configure_file()` 的内容比较行为，不写入检查时间，因此无改动不会更新头文件时间戳或触发编译。

新增 `baseline_code_identity_tests`，在临时 Git 仓库中构建并运行一个输出身份的 C++ 程序。测试只在首次执行配置，后续使用 `cmake --build`，覆盖已跟踪源码修改、新增文件创建/修改/删除、暂存、提交号变化及差异清空。无改动重复构建时核对头文件、目标文件、可执行文件的时间戳不变；破坏临时 Git 入口后检查构建明确失败。测试不修改用户仓库的提交记录。

仓库 ARM64 构建及 21/21 CTest 通过。随后独立入口全新构建及 17/17 CTest 通过，验证相同生成机制可供两个入口使用。W1 关闭。

## 已修复的 WARNING 2

`tests/nanotracev3_performance_benchmarks/baseline/tests/runner_test.cpp` 新增 `mid_interrupt` 和 `mid_model`。两者先完成一次预热更新及两次正式更新，再在第三次正式搜索提交时设置中断标志或注入取输出失败。使用既有 90 帧 `slow.mov` 合成素材，确保停止不是由于录像结束。

- 中断退出 130，状态为 `interrupted/signal`；当前第三次更新完成并保存，之后不再执行模型。
- 模型失败退出 1，状态为 `failed/model_error`；保留两次成功更新和一条失败更新，失败帧不进入成功统计。
- 两组均保存 6 条帧记录，包含预热初始化/更新、正式初始化和三条正式更新。断言帧身份、顺序、模型调用次数、错误身份、框有效性、已执行区间及完成标记。
- 失败帧的分数、框和后处理为空，未执行的 head、输出复制/释放/规范化没有区间记录。
- 每组均调用独立 `baseline_verify_records` 进程，从落盘 CSV/JSON 重算分组统计和处理速率，检查父子区间。CMake 仅补充测试程序对该核对工具的构建依赖，没有改变 W1 涉及的身份生成逻辑。

验证命令 `./build.sh --test` 通过，20/20 CTest 成功。W2 已在此前补测中关闭，W1 的后续修复见上文。本次仅改变受控测试、测试构建依赖及文档，没有修改生产处理逻辑。

## SUGGESTION

无额外建议。

## 需求与场景映射

以下路径均相对于仓库根目录。`baseline/` 在本表中特指 `tests/nanotracev3_performance_benchmarks/baseline/`。

| 需求／场景 | 实现和证据 |
|---|---|
| 独立入口；拒绝覆盖输出 | baseline/CMakeLists.txt、main.cpp、config.cpp、result_writer.cpp；帮助入口、配置和重复目录测试通过 |
| 生产亮度；首帧顺序；不支持输入 | benchmark_runner.cpp 直接复用 MjpegVideoReader 和 copyGrayFrame；读取器校验普通文件及首个视频流编码；baseline_video_observation 验证三帧像素和构造期首帧 |
| FP16 及主机数值；缺少证据拒绝 | model_preparation.cpp 核对角色、模型和导出记录指纹、精度声明及原始来源指纹；factory 查询实际浮点接口；runner 测试覆盖缺失证据和整数接口；006 保存真实导出记录 |
| 模板和更新执行频率 | runner 每轮 reset/init；后端缓存模板特征；受控模型计数及真实区间重算验证初始化一次、每更新搜索/head 各一次 |
| 唯一完整计时；首帧与等待层级 | timing_observer.h、timing_recorder.cpp、nano_tracker.cpp、rknn_session.cpp；可控时钟测试和原始区间父子关系核对通过 |
| 预热后重放 | runner 重开录像、重置目标；默认 30 次/3 轮在配置测试中验证；真实每组保留 30 次预热更新和 3 轮正式更新 |
| 原始数据；离线重算与中间失败 | result_writer.cpp 每帧写入并刷新；statistics.cpp 读取磁盘 CSV；重算通过；中间失败及中断部分报告的断言、独立重算已通过，见 W2 修复记录 |
| 双格式报告；20/30 ms 速率与空数据 | statistics.cpp 分组并使用排序取第 ceil(p×N) 项的分位数；受控两帧得到 40 帧/秒；空更新不成功 |
| 身份、状态、输出失败及容器边界 | result_writer.cpp、runner、/dev/full 受控测试、validation.md；身份更新的修复及验证见 W1；容器与实机证据明确区分 |
| 观测对照与开销 | 前处理/后端受控对照、006 两组各三轮数据；框和分数误差为 0；validation.md 保留分布差值和频率温度波动，未扣除估算开销 |

FP16 结论来自已保存的导出记录与文件指纹链。没有把浮点输入输出接口视作内部算子精度的独立证明，也没有在本次核验中重新转换模型。

## 本次实际执行的检查

- `openspec validate add-nanotrackv3-performance-baseline --strict`：通过。
- `git diff --check`：通过。
- `./build.sh --test`：使用既有 ARM64 镜像完成增量构建，20/20 CTest 通过。容器 SDK 为 2.3.0，仅用于构建及受控测试。
- 在同一 ARM64 容器中运行现有 `baseline_verify_records`，读取仓库 `evidence/board2-20260920/detailed-006` 和 `total-only-006`：完整观测组 934 帧、24,244 区间、364 项非空统计分布通过；仅总计时组 934 帧、934 区间、32 项非空统计分布通过。
- 重新比较两组逐帧框和分数：最大绝对差均为 0。
- 初次核验时用 `git diff HEAD` 和相关未跟踪源码内容核对 `detailed-006/code_snapshot.txt`：一致。该快照 SHA-256 为 `444c9d1ce4eb29fb179833d987e15da21d54452c6333a05c6ca13311847e0c57`。

## 实机证据及未重跑项目

本次没有重新部署或执行 NPU 性能测量，使用并复核已保存的开发板 2 实机证据。该证据记录 Runtime 2.3.2、驱动 0.9.8、模板/搜索/head 三个 FP16 模型，以及 640×512、301 帧的本地 MJPEG 输入。首框和模型、录像指纹详见 validation.md。每种观测模式均有三轮、每轮 300 次正式更新。

独立构建 16/16 测试及实机动态依赖检查采用已有日志，初次核验没有再做独立入口全新构建；W1 修复后已完成全新独立构建及 17/17 测试。没有运行 Jetson 构建、Jetson 数值比较、实时相机、质量评价、持续负载或 clang-tidy/clangd；前述实机质量项目不属于本性能变更的完成结论，静态检查工具与 Jetson 环境限制见 AGENTS.local.md。

W2 补测使用 ARM64 容器及模拟 RKNN API，不需要真实模型。本次未重新执行实机性能测试；`006` 快照是补测前版本，新增测试及测试构建依赖不在该历史快照中。

W1 修复只涉及构建身份及测试机制，未重新采集实机性能数据。构建期间须保持源码和 Git 状态稳定；身份检查不提供并发编辑时的原子源码快照。
