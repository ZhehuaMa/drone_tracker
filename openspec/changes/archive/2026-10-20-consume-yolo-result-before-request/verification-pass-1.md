# 核验报告：consume-yolo-result-before-request，第 1 轮

## 核验对象与结论

2026-10-20 在工作树 `/home/mzh/nickluo/drone_tracker-worktrees/consume-yolo-result-before-request` 独立执行 `openspec-verify-change`。本轮先完成核验，再判断是否需要修复。未实施新的任务，未同步规格、归档、提交或部署。

初次核验结果为 **CRITICAL 0、WARNING 0、SUGGESTION 0**。五项要求和十三个场景均有实现及测试证据。六个规定检查均已完成，无跳过检查；不需要修复，也不需要因本轮发现而启动下一对核验与修复。

结果快照指从检测器的共享结果槽取走、保存在当前一次 `update()` 中的完整局部结果。仲裁指使用该结果确认跟踪、判丢或恢复；补跟踪指在检测来源图像上初始化后，重放其后的缓存帧。本次变更核验的是这些操作的顺序及原并发边界，不是准确率或性能通过结论。

## 汇总记分表

| 维度 | 状态 |
|---|---|
| 完整性 | 任务完成 16/16；规格覆盖 5/5，其中 ADDED 4 项、MODIFIED 1 项；无 REMOVED 或 RENAMED 项 |
| 正确性 | 要求实现映射 5/5；场景覆盖 13/13；源码、受控测试及独立实机记录相互一致 |
| 一致性 | 五项设计决策均遵循；代码模式检查通过；生产改动局限于两个调用点，测试入口受编译宏约束 |

六个已执行检查为任务完成、规格覆盖、要求实现映射、场景覆盖、设计遵循和代码模式一致性。`Not verified` 项为 0；`Not applicable` 项为 0。

## 工件与任务完成证据

执行 `openspec list --json` 确认本工作树有仓库级 OpenSpec 根目录。`status --change consume-yolo-result-before-request --json` 返回 `spec-driven`，proposal、specs、design、tasks 均可读。`instructions apply` 返回 `taskTrackingConfigured=true`、16/16 完成、0 项剩余，没有不可读的任务文件。以上状态只用于定位，完成判断来自下表的源码和原始记录。

已完整阅读 `proposal.md`、`design.md`、`specs/detection-tracking-arbitration/spec.md`、`tasks.md` 及 `implementation-report.md`。报告正文采用简体中文，遵循本仓库明确的工件语言约定；CLI 返回的英文通用上下文不覆盖该约定。

| 任务 | 独立核验的完成证据 |
|---|---|
| 1.1 | `src/hybrid_tracker/hybrid_tracker.cpp:404` 先保存 `get_result()` 的布尔值及完整结果，`:421` 请求当前帧，`:424` 才使用保存值进入原正结果分支。 |
| 1.2 | `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp:863` 的正结果、负结果及空结果用例检查接收、独立来源身份和不等待推理；`:1134` 的原未读保护断言保留。 |
| 1.3 | `hybrid_tracker.cpp:590` 建立当前实际观测，`:599` 读取一次，`:605` 保留间隔判定，`:624` 处理保存结果；`:831` 保留无结果计数和严格大于看门狗门槛。 |
| 1.4 | `diagnostic_tracker_test.cpp:933` 覆盖提交帧、非提交帧及早退；`:766` 检查真实提交的来源观测；`:637` 检查原仲裁、来源缺失和看门狗临界值。 |
| 1.5 | `hybrid_tracker.cpp:404`、`:596` 的英文注释解释先读、先请求及不补读的边界，没有承诺特定帧必执行。 |
| 2.1 | `diagnostic_test_access.h:8` 的线程局部测试钩子、`diagnostic_tracker_test.cpp:838` 的释放守卫及有截止的模型/初始化等待；未增加生产配置或锁顺序。 |
| 2.2 | `diagnostic_tracker_test.cpp:993` 在读取后释放真实工作线程，检查合法 `UnreadResult` 拒收、A 不被 B 替换、下一更新读取 B、A 不重复初始化。 |
| 2.3 | `diagnostic_tracker_test.cpp:1049` 在阻塞初始化期间证明当前请求已经完成推理，并检查当前请求判定先于历史查找。 |
| 2.4 | `diagnostic_tracker_test.cpp:725` 检查待执行替换一次记账；`:1049`、`:1108` 检查未读及执行中请求的 OFF、帧号复用、图像释放和资源析构；`:403` 检查来源图像淘汰。 |
| 3.1 | 最终 `control/frozen-plan.json` 预声明三段、每侧三轮、固定参考提交、CPU 4、P=3600 秒、C=10 秒及 50 毫秒限制；18 轮身份条件一致，真实源码和程序不同。 |
| 3.2 | `tests/tracking_regression/README.md:141` 解释三阶段顺序、两个真实版本、单配置入口、逐轮及跨轮差异、零框保留和非准确率结论。 |
| 3.3 | `final-build-test.log` 记录默认交叉构建 57/57 CTest 和原生内存限制 1/1 通过；诊断、恢复和预处理契约测试均在其中。当前 424 个候选源码文件全部与实测导出一致。 |
| 4.1 | 最终参考九轮 `completion.json` 全部正常结束且合格；访问身份为 ADB 序列号 `5ceb49407502303d`，Runtime 2.3.2、驱动 0.9.8；每轮清理完成。 |
| 4.2 | 最终候选九轮同样合格；每轮保留请求、执行、读取、初始化及发布数据；程序身份与参考不同。历史失败组独立保留，没有挑选替换失败轮。 |
| 4.3 | 三份独立冻结基线和比较报告均存在；逐轮值、中位数、范围、差值、相对变化、准备内存和中段 388/389 请求身份均有记录；门槛为 `not_set`。 |
| 4.4 | 本轮重新运行严格 OpenSpec 校验通过，独立核对下文的规格、设计、源码、测试和全部实机轮次；未提前同步或归档。 |

## 要求与场景映射

| 要求 | 实现证据 | 场景及测试证据 |
|---|---|---|
| Completed result is taken before current submission | `hybrid_tracker.cpp:404`、`:596`；`yolo_detector.cpp:155` 在锁内复制并清空结果槽 | Ready negative result does not block current input、Ready positive result is saved before submission、No ready result does not wait for inference：`testDetectingSnapshotOrder`；Tracking 同一路径由 `testTrackingSnapshotOrderAndEarlyReturns` 补充覆盖。 |
| Current submission precedes saved-result arbitration | `hybrid_tracker.cpp:421` 在 `:424` 之前，`:617` 在 `:624` 之前 | Expensive catch-up follows current request：`testRequestOverlapsRecoveryAndRoundCleanup`；Rejected current request preserves saved output：`testCompletionAfterSnapshot` 的保存正结果且新请求被拒绝分支。 |
| Saved result has one disposition per update | `hybrid_tracker.cpp:406`、`:598` 的按值局部结果及单次 `has_result` 分支；`:424`、`:636` 保留回合校验 | New result cannot replace local snapshot：`testCompletionAfterSnapshot`；Original provenance and cleanup rules remain applicable：`testHistoricalArbitrationAndWatchdog`、`testHistoryMissAndPartialCatchup`、`testRequestOverlapsRecoveryAndRoundCleanup`、`testOffWithExecutingCurrentRequest`。 |
| Completion between read and submission stays protected | `yolo_detector.cpp:113` 保留未读拒收，`:155` 读取与请求仍为独立操作；生产者覆盖策略未改 | Result arrives after empty snapshot：`testCompletionAfterSnapshot`；Pending work can still replace a briefly visible frame：`testOverwrite` 检查同构的执行中 A、待执行 B 被 C 替换及一次取消记账，`preprocess_contract_test.cpp:180` 一带补充像素、ROI 和来源快照关联。测试不依赖字面帧号 388/389/390。 |
| Submission cadence and inference search stay unchanged | `hybrid_tracker.cpp:497`、`:590`、`:605`；原输入/时间/跳变早退仍在读取之前；检测器搜索级联和线程实现无差异 | Continuous pending replacement：`preprocess_contract_test.cpp:229` 的 `checkContinuousReplacementProgress`；Tracking reads on an update without submission：`testTrackingSnapshotOrderAndEarlyReturns`；Current source observation precedes delayed arbitration：同测试的快照交接断言、`testSubmittedSnapshotAfterNanoFailure` 的真实来源元数据及源码顺序共同覆盖；Existing early return does not consume output：`testTrackingSnapshotOrderAndEarlyReturns` 的无效输入、重复时间及持续跳变分支。 |

共计十三个场景。已有直接检测器测试 `testUnreadResultAdmission` 仍逐项比较框、请求身份、回合身份、来源时间及比较元数据，没有因调用者顺序变更删除原断言。Off、Basic、Detail 为诊断关闭、基础记录和详细记录三种模式；新增顺序、交接和生命周期用例在三种模式执行。

## 设计与代码一致性

| 设计决策 | 核验结果 |
|---|---|
| 1. 调用者只保存一次结果再提交 | 两处分支均有独立局部 `YoloResult` 及 `const bool has_result`，没有第二次读取、等待或新原子接口。 |
| 2. 保留当前帧准备位置 | Detecting 仍在预测和历史加入后交互；Tracking 仍先执行 NanoTrack、原跳变检查和来源观测，再取结果。 |
| 3. 当前提交先于耗时恢复 | 源码顺序、阻塞初始化测试和候选实机事件均一致；读取返回后不持有检测器锁执行恢复。 |
| 4. 保留读取与提交之间的竞态边界 | 受控工作线程在读取后完成仍造成 `UnreadResult`；不清空新结果，不重试，不改变最新待执行替换。 |
| 5. 机制测试与真实版本比较分开 | 有界交接验证机制；不同源码及二进制的十八轮回放描述行为。报告保留零框、异步波动和未设置性能门槛。 |

生产差异仅调整 `hybrid_tracker.cpp` 的两个调用点。检测器、ROS 编排、模型契约、默认参数、搜索级联和状态数值没有修改。测试钩子为 `thread_local`，仅在 `DRONE_TRACKER_DIAGNOSTIC_TESTING` 下编译，不引入跨线程共享回调；原图像和模型所有权保持不变。`git diff --check` 通过，未发现明显代码模式偏离。

## 独立实机证据审计

最终证据根目录：`/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020`。以下数字来自本轮用 Conda base Python 重新读取和断言原始文件，不直接采用实现报告的结论。

1. 当前候选 424 个源码文件逐个匹配导出清单。参考 424 个源码文件逐个匹配导出清单，并逐个与固定提交 `7fbb0a2e006f12654a7db0f6d9d152244ec07e0d` 的 Git 内容比较一致。
2. 18 轮完成记录列出的 648 个文件全部重新计算 SHA-256，一致。SHA-256 是文件内容的校验值，用于确认报告对应实际保存的数据。两侧各九轮的源码身份分别一致，实际源码与程序校验值在两侧之间均不同。
3. 每个区间六轮的完整 `comparison_group` 相同。板卡序列号、Runtime、驱动、模型、工具、算法和输入比较条件均一致；每轮供帧 CPU 请求为 4、允许集合为 `[4]`，最大供帧迟到小于 50 毫秒，清理完成且没有超时。
4. 重新遍历全部 327201 个规范化事件。候选 558 次成功读取均先于同更新的唯一当前请求或间隔跳过，参考 571 次均为相反顺序；同时核对单调时间和同线程递增事件序号。每次更新最多一次成功读取；历史查找和初始化均在当前判定后并绑定旧结果身份。
5. 重新从 `messages.csv` 按每轮 `[m0, m0+D)` 窗口筛选发布，核对总数、有效框数和有效性条件，均与指标文件一致。十二轮零框记录完整保留。
6. 三个当前 `baseline.json` 校验值分别为 `d93f57307e3a5bed5e67e172fdaf745702dbac75034bee02e6e8483c08df3046`、`8b5cb2515e90aa59edba0a7f3a3e7fc128a64df60d905f70c52ac59fae3f595d`、`422b2502a6d77ad6bf9754ab40474287679980b59eae843b20790da97de8fe1b`，与冻结记录一致。三个正式比较状态均为 `comparable`，门槛为 `not_set`；套件 `not_passed` 不被改写成性能通过。
7. 最终 `control/postcheck.json` 记录正式服务 active/running/enabled、PID 6869，无测试进程遗留，恢复定时器 inactive；入口、配置、Runtime、模型、governor 和 boot-id 与维护前均相同。本轮核验没有连接板卡或停止服务。

候选中段三轮均接受 fixture 389，但均在执行前被后续帧替换。中段六轮均无初始化、无有效框。尾段候选有效比例分散，其中一轮低于同序号参考；报告同时呈现较高的候选更新耗时中位数。上述结果符合设计声明的边界，不构成要求缺失，也不能证明准确率改善。

原始二进制分段重新解码和指标重算的既有证据位于 `analysis/metric-recomputation.json`，18 份指标均精确一致。本轮独立重新核对的是全部文件完整性、规范化事件顺序、来源身份及发布统计；没有重复十八轮板端实验，也没有改写原始数据或历史失败组。

## 构建、限制及最终评估

既有完整验证来自默认 x86_64 镜像交叉编译 ARM64，镜像为 `025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。日志 `/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/final-build-test.log` 显示 57/57 CTest 及原生内存限制 1/1 通过。相关测试分别为 `diagnostic_tracker_tests`、`preprocess_contract_tests`、`recovery_regression_tests`。本轮确认当前受测源码完全一致，因此没有无变化地重复完整构建。另重新执行 `openspec validate consume-yolo-result-before-request --strict` 和 `git diff --check`，均通过。

Jetson 硬件、相机采集、飞行、带人工标注的准确率、长期负载和 clang-tidy/clangd 检查仍未执行。这些限制已在实现记录披露，未作为本次五项要求的通过证据；六个技能规定检查均有可用证据，并未跳过。8 GiB ADB 板回放不证明另一块 4 GiB SSH 板能容纳同样预解码缓存。有限的服务快照不证明长期无泄漏或完全无调度干扰。

### CRITICAL

无，0 项。

### WARNING

无，0 项。

### SUGGESTION

无，0 项。

### 修复阶段

初次核验没有 CRITICAL 或 WARNING，因此无需修复。未修改实现、测试、规划工件、任务状态或历史证据；本轮仅新增本核验报告。

### 最终评估

所有规定检查通过，可以进入后续归档流程（Ready for archive）。本报告不执行归档，也不等同于性能、准确率或生产发布验收。无需因本轮发现再启动独立核验与修复对。
