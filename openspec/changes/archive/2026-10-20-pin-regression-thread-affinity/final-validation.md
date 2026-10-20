# 最终实现与证据一致性记录

本文件由顺序工作 2 的实现者记录交付证据，文末另记顺序工作 3–5 的后续状态。任务清单完成 34/34；新计划十八轮全部合格；环境恢复已核验。独立验证报告见 [verification-1.md](/home/mzh/nickluo/drone_tracker-worktrees/regression-cpu-affinity/openspec/changes/archive/2026-10-20-pin-regression-thread-affinity/verification-1.md)。原失败记录及诊断记录保持原样。

## 最终身份和重建

证据根：`/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020`。算法提交 `4871442dda37d3d64087fda0b7756692c8f7e473`。参考标签为“算法基线 + 共同观测补丁 + 共同供帧支持修复”。源码身份基于实际构建输入文件清单；新 OpenSpec 报告不属于运行源码身份。

| 侧别 | 最终源码 SHA-256 | 最终二进制 SHA-256 |
|---|---|---|
| ref | `080cf382dc2b8306c8e75c14b8c21807968a69551bd9fc62f3d7848faaebb37a` | `dcba1fdc854fad8102cf930149d8bc5b19ec2f2542cdb8f2b3422d041f548799` |
| cand | `198c42f7a0173f3a980b9a54fa842c29f600ec7048b24a686087c46ad656186d` | `9810ee537e7120baafbba94b69c7f1414c4d6f2e705337f68820cefda72c1251` |

`final-source-consistency.json` 再次核对开发工作树、独立参考树及交付二进制。`{ref,cand}-source-export.json` 保存逐文件输入清单；源码压缩包及精确补丁均保留。参考按观测/工具补丁、共同支持补丁顺序从固定提交重建；候选由完整候选补丁重建。

CPU 亲和性表示线程允许执行的 CPU 集合；工作点 CPU 记录调用当时实际执行的位置。新增位置接收器默认关闭，仅回归入口安装。生产 `cpu-affinity.yaml`、公共角色实现、两个参数文件、launch 和 `HybridTracker` 算法文件均与固定提交逐字节相同。YOLO/节点公共源只加入共同且默认关闭的采样接入。

既有“检测结果快照 → 请求或跳过 → 仲裁”顺序位于未改动的 `src/hybrid_tracker/hybrid_tracker.cpp`。共同 HostFrame 支持修复只增加有所有权的只读保留入口，既有生产复制接口不变。原完整事件布局、CRC、请求与恢复口径、队列、预算及迟到门槛继续生效。

## 自动检查和平台边界

- 候选使用 `./build.sh --test --artifacts-dir <新证据根>/candidate-build`，57/57 CTest 与 native OS 地址空间限制检查 1/1 通过。
- 参考使用相同入口与独立缓存。首次 56/57，仅 packaging 测试缺被忽略的空 install 目录。添加与候选/旧诊断参考相同的既有测试环境目录后，受影响 CTest 1/1 通过。累计全部 57 项通过，首次失败日志完整保留。没有源码修复或测试删减。
- 两侧各有 88 个唯一 Python SDK 用例：87 本机与 1 实际 ARM 中间件。无跳过，包含 8 个亲和性/证据/实验测试和 6 个控制器失败、截止与回收测试。原 SDK 日志由全套 CTest 提取，没有无必要地重复运行。
- 默认 amd64 镜像固定为 `025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`，GCC 11.4.0/CMake 3.22.1，ARM64 输出。84 个 C++ 目标的编译器、编译参数与宏定义相同；未使用备用 ARM64 镜像。
- clang-tidy/clangd 不可用。未运行 Jetson、相机、HDMI、长时间负载、实飞或人工标注准确率测试；不以 x86/QEMU 结果替代板端时间性能。test-02 明确跳过。

完整构建及测试原始日志在证据根，编译/依赖及源文件清单另存 `*-compile-inputs.json`、`compiler-options-equality.json`、`project-library-artifacts.json`、`common-build-entrypoints.json` 和 `board/control/formal-preflight-identities.json`。

## 全部规格场景映射

下表覆盖本变更规格增量（delta spec，即此次新增或修改的要求）的全部 23 个场景。m0 为本轮单调时钟起点，e0 为系统时间起点，两者映射原帧的相对间隔。supplier 表示供帧线程，Tracking 表示跟踪更新，logger 表示独立日志进程，supervision 表示外部监督。自动负向用例用于证明错误仍被拒绝，不用一次正常板端运行冒充故障注入。二十步恢复的合成用例也不冒充某一正式轮实际发生了二十步。

| 规格场景 | 自动证据 | 交付/板端证据及边界 |
|---|---|---|
| 新模型与旧模型比较 | `test_identity.test_true_model_content_change_is_incomparable`、普通比较模型负向用例 | 显式比较仍校验模型实际内容；三份比较身份一致 |
| 同名用例改变区间 | `test_variable_comparison.test_cases_versions_and_ranges_cannot_mix` | 三个独立区间和原始帧范围分别冻结 |
| 未声明亲和性差异 | `test_affinity.test_formal_comparison_ordinary_rejects_and_explicit_checks_builds` | 正式运行使用显式处理清单，普通路径没有放宽 |
| 已声明实验仍有其他变化 | `test_affinity.test_strict_leaves_and_common_driver` | 全部精确允许差异保留；其他组字段逐项一致 |
| 启用指定回归映射 | 配置、初始范围及外部策略测试；`tracking_regression_round_tests` | 九个候选实际读回与关键点：4/5/6/7/1，监督 0–3；正式策略字节不变 |
| 保留旧供帧配置 | `test_supplier_cpu.py` 与无新策略的 source/round CTest | 参考只约束 supplier 4，其他受管范围 0–7 |
| 外部策略不能覆盖配置 | `test_affinity.test_online_initial_cgroup_and_environment` | 私有策略由不可变运行配置生成；环境入口取消旧外部配置 |
| 跟踪与供帧使用不同核心 | `parkedSupplierLifecycle`，source CTest | 候选 supplier 4 在控制缩小前就绪，Tracking 5 在 m0 前登记 |
| 内核只允许 CPU 5 | 初始范围/cgroup 负向测试及 source 排除核心测试 | 未放宽内核集合；板端初始 0–7 与有效范围均记录 |
| 准备后模型构造失败 | source parked 生命周期与 node round 失败回收用例；控制器回收测试 | 异常路径检查不建立播放时钟，线程均可停止；正式十八轮无遗留 |
| 一次恢复包含二十个补跟踪步 | `test_affinity.test_evidence_requires_exact_points_masks_and_capacity` 构造二十步并删除必要点的负向检查 | 每轮原始 CatchupStep 与位置记录按 round/update/request/frame 身份逐条相等 |
| 观测缓存溢出 | 同一证据测试的 overflow=1 负向检查；C++ observer 数组用例 | 十八轮容量固定 262144/写者，全部 overflow=0 |
| 准备范围与已登记角色范围按时间区分 | 同一证据测试覆盖登记前、恰好登记、登记后、跨扫描及非法时间 | 保留 observed_mono_ns，登记后不符仍失败；新工具身份一致 |
| 推理库产生额外线程 | `test_affinity.test_unknown_threads_and_exit_races_are_not_managed_proof` | 每轮保留未知 TID、掩码与 last_cpu；未猜测归属或批量绑定 |
| 预解码线程结束 | source CTest 验证预解码缓存与放行；准备/时钟边界检查 | 准备线程清单与模型就绪/回放清单独立保留，不把解码线程当持续回放计算 |
| 工具身份差异被审计 | 严格叶路径与额外驱动文件负向测试 | 共同 15 个模块来源/哈希及控制清单在停服前和每轮前后复核 |
| 诊断记录损坏 | collect/compare 的 CRC、配对、完整结束及改写证据负向测试 | 原诊断格式与校验门槛未变；十八轮原始 .dtd/events.bin 哈希通过 |
| 比较三个既有区间 | 多区间配置、冻结及比较用例 | 每区间三份新参考 + 三份新候选，共 18/18 |
| 慢轮或零有效框 | `test_compare.test_slow_and_empty_rounds_never_dropped`、未合格运行负向用例 | 全部合法值保留；零有效框未过滤；guard 在任何失败后停止计划 |
| 绑定成功但结果分散 | 比较器无默认性能通过和显式门槛用例 | 各区间中位数、范围、差值、百分比及恢复工作量完整报告；未设置判定规则 |
| 已验证像素经过原时间轴供应 | `supplierReusesVerifiedPixels`、`retainedHostFrameOwnership` 及原 source 守恒检查 | 共同支持代码相同；原有 m0/e0 时间映射、全部帧数、队列处置和 50 ms 限制保留 |
| 源清理后仍持有帧或视图 | `retainedHostFrameOwnership` 与 retained supplier 生命周期测试 | 源/帧销毁后视图仍可读；生产 deep-copy API 仍独立 |
| 没有可保留所有权的外部缓冲 | `retainedHostFrameOwnership` 借用缓冲及非法 Gray8 负向检查 | 借用外部缓冲被拒绝，不把没有所有权的指针包装成帧 |

## 三个区间、环境恢复和结论边界

指定板卡仅通过 `adb -s 5ceb49407502303d` 访问。三个区间为 `[20,35)`、`[100,130)`、`[165,end)`；每区间按参考/候选交替各三轮，共十八轮。固定全帧预解码、supplier 4、P=3600 秒、C=10 秒、50000 微秒迟到界限和队列 32。

原始十八轮均有完成、CRC/事件、工作点身份、CPU 掩码、外部线程快照、内存及退出回收证据。共同工具的实际模块来源及哈希在停服前和每轮前后校验。旧 preflight 导入顺序问题没有进入本次运行工具。没有额外 ADB 探针干扰计时。

最终正式服务 PID **11692**，active/running/enabled，独立恢复 timer inactive，无测试进程。91 份保护文件、模型、系统 Runtime、部署链接及 governors 均保持原值。恢复证明为 `/home/mzh/nickluo/drone_tracker-validation/pin-regression-thread-affinity-final-20261020/final-board-proof.json`。

逐区间每轮原值、三轮中位数和范围、候选减参考差值及百分比，见 [regression-report.md](/home/mzh/nickluo/drone_tracker-worktrees/regression-cpu-affinity/openspec/changes/archive/2026-10-20-pin-regression-thread-affinity/regression-report.md) 和外部 `analysis/` 的 JSON/CSV/中文明细。输出为零、没有恢复事件、慢轮和观测开销均没有删掉。未设置判定规则，不宣称性能通过、准确率提高或资源争用完全隔离。

原正式失败记录保留在 [validation.md](validation.md)；后续诊断与共同修复保留在 [failure-investigation.md](failure-investigation.md)。其旧进度、二进制和未执行结论仅对应当时版本，不能覆盖本次新计划。

首次本机分析因 ADB 复制尚未结束而缺失参考目录；失败尝试单独保留，完成复制后重新冻结及比较，未改写板端证据。OpenSpec 严格校验和 `git diff --check` 的最终结果保存在证据根。此处记录的是顺序工作 2 完成时的一致性核对。其后的独立验证和十八轮证据复用结论见下节。


## 后续流程状态（2026-10-20）

1. 顺序工作 3 由新的独立代理完成。初始及最终问题数量均为 CRITICAL 0、WARNING 0、SUGGESTION 0，表示无严重缺失、规格偏离或改进建议。本轮没有修复；仅新增 [verification-1.md](/home/mzh/nickluo/drone_tracker-worktrees/regression-cpu-affinity/openspec/changes/archive/2026-10-20-pin-regression-thread-affinity/verification-1.md)。验证者独立复核全部十八轮的原始诊断、CPU 位置、指标和三个比较，并确认候选 433 个构建源码输入、两侧二进制和实际工具身份一致。
2. 顺序工作 4 按用户明确允许的条件跳过重跑。理由是顺序工作 2 已完成最终源码的 18/18 新合格实测，顺序工作 3 没有修改影响构建或运行的文件，且独立确认全部证据有效。复用的是该最终计划，没有用历史失败、诊断轮或旧版本补足轮数；三个比较仍为 `comparable`，判定仍为 `not_set`（未设置性能门槛）。
3. 顺序工作 5 仅同步主规格、归档和更新流程文档，再按用户授权暂存相关文件并创建本地提交。此步骤不改变运行源码、工具、二进制、模型或部署，不连接板卡，也不推送。归档位置为 `/home/mzh/nickluo/drone_tracker-worktrees/regression-cpu-affinity/openspec/changes/archive/2026-10-20-pin-regression-thread-affinity/`；同步目标为 `openspec/specs/rk3576-tracking-regression/spec.md`。实际提交标识由 Git 历史记录，不以待执行操作冒充已完成结果。

独立验证的限制仍适用：未新建整套构建或板端运行，clang-tidy/clangd 不可用，未验证 Jetson、相机、HDMI、长期负载、实飞或人工标注准确率。板端状态引用保存的恢复证明，其 PID 不代表此文档编辑时重新读取的实时状态。外部 `handoff.json` 保留顺序工作 2 交接时的状态；其中“尚未独立验证”等字段是历史快照，后续状态以本节及独立报告为准。


顺序工作 5 的归档核查已完成：同步一项修改要求和八项新增要求，原主规格标题、Purpose（能力目的）及未涉及的要求逐字保留。归档前 `openspec validate pin-regression-thread-affinity --strict` 通过；同步后的 `openspec validate rk3576-tracking-regression --type spec --strict` 通过；归档后 `openspec validate --archived --strict --json` 为 37/37 通过。CLI 的具名 change 校验只查活动目录，不能直接用归档名称或含路径名称查找；因此另外直接调用同一已安装 OpenSpec 的 `Validator(true)`，对归档 `proposal.md` 及 `validateChangeDeltaSpecs` 执行严格校验，两者均为 0 错误、0 警告、0 提示。

全库 `openspec validate --specs` 为 25/25 通过，仅其他既有能力存在超过 500 字符的要求警告，未改动那些能力。相关 Markdown 的 36 个本地链接均可解析，没有指向旧活动目录的失效链接；历史路径文字保持历史含义。候选 433/433 构建源码输入仍与正式清单逐文件匹配。此次仅做文档、规格和归档核查，无需重复已通过的构建和实机测试。
