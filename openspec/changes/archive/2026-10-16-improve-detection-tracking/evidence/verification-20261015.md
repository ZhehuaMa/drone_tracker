# 验证报告：improve-detection-tracking

验证日期：2026-10-15。使用技能：`openspec-verify-change`。本报告是任务 2 的独立审查，先检查实现与规格，再判断是否需要修复。没有执行归档、提交、部署或开发板服务操作。

## 结论与计分表

未发现 CRITICAL（必须修复）问题。发现 2 项 WARNING（建议处理），没有 SUGGESTION（可选改进）。按用户要求，仅记录 WARNING，不开展修复。本轮没有修改正式 C++、测试或离线分析程序。

| 维度 | 检查结果 |
|---|---|
| 完整性 | 任务 22/22；20/20 条新增需求找到实现；没有修改、删除或重命名需求 |
| 正确性 | 20/20 条需求完成代码映射；31/31 个场景检查了处理路径及现有证据；部分边界缺少直接测试断言，见 WARNING 2 |
| 一致性 | 6 项设计决定均有对应实现；代码结构沿用现有边界；工件语言存在 WARNING 1 |

本技能要求的六项检查均已运行：Task Completion、Spec Coverage、Requirement Implementation Mapping、Scenario Coverage、Design Adherence、Code Pattern Consistency。没有因工件缺失或不可读而跳过的检查。场景的代码路径存在，不等于每个场景都有独立自动化测试；以下覆盖说明保留这一差别。

## 输入与检查范围

工作目录为 `/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker`，分支为 `codex/improve-detection-tracking`。`openspec list --json` 确认这是仓库内的 OpenSpec 根目录。`status` 返回 `spec-driven` 模式；`instructions apply` 返回 22 项完成任务，剩余 0 项，没有不可读取的任务文件。

已读取 `proposal.md`、`design.md`、`tasks.md` 和两个增量规格。增量规格是本变更新增的行为要求，分别包含仲裁需求 15 条、场景 24 个，以及回归需求 5 条、场景 7 个。审查同时读取了正式实现、受控输入测试、诊断协议、离线读取器、实现报告及原始运行证据。

比较对象是当前工作区相对于 `cb977c204615ade46a62d198d821ed93de449108` 的实现。没有将尚未执行的后续两组视频回归当成本轮新证据。

## 需求实现与场景证据

下表中的代码路径均相对仓库根目录。IoU 表示两个框的交集面积除以并集面积，用于判断同一来源帧上的跟踪与检测是否一致。来源元数据只保存时间、身份、框和有效标记，不持有图像像素。

| 规格需求 | 实现证据 | 场景或测试证据 |
|---|---|---|
| 来源比较数据独立于图像淘汰 | `src/hybrid_tracker/yolo_detector.h:39`，`yolo_detector.cpp:122`、`:181` | `preprocess_contract_test.cpp:189` 验证替换释放像素、结果保留数值快照 |
| 同一来源帧比较 | `src/hybrid_tracker/hybrid_tracker.cpp:657` | `diagnostic_tracker_test.cpp:623`、`:644` 使用与当前框不同的来源框；仲裁不回退当前框 |
| 缺失上下文不改变未确认计数 | `hybrid_tracker.cpp:721` | `diagnostic_tracker_test.cpp:639` 检查计数保持 4；看门狗独立复位 |
| 部分字段有明确资格 | `hybrid_tracker.cpp:663`、`:665`、`:670` | `diagnostic_tracker_test.cpp:639`、`:644` 覆盖只有预测、没有预测两种组合 |
| 恢复必须有来源像素 | `hybrid_tracker.cpp:444`、`:734`、`:759` | `diagnostic_tracker_test.cpp:381`、`:649` 检查历史缺失与未发生初始化 |
| 有效证据沿用原有阈值 | `hybrid_tracker.cpp:724`、`:753`、`:776`、`:789` | `diagnostic_tracker_test.cpp:644`、`:649`、`:661` 检查同意、低重叠和第五个无目标结果 |
| 无法恢复不能记为成功接受跳变 | `hybrid_tracker.cpp:731`、`:741` | `diagnostic_tracker_test.cpp:652` 连续跳变仍累加未确认计数，没有新增模板初始化 |
| 只替换尚未执行的请求 | `yolo_detector.cpp:101`、`:107`、`:113`、`:115` | `preprocess_contract_test.cpp:189`；`diagnostic_tracker_test.cpp:699` 检查执行输入保持原样；未读结果保护缺少直接断言，见 WARNING 2 |
| 请求输入关联完整 | `yolo_detector.cpp:122`、`:258` | `preprocess_contract_test.cpp:189`、`:229` 检查像素、时间戳、区域、来源身份及快照一致 |
| 提交时机与搜索级联保持一致 | `hybrid_tracker.cpp:398`、`:587`；`yolo_detector.cpp:244` 后的既有搜索路径 | `preprocess_contract_test.cpp:229`、`:344` 检查持续替换时仍能开始执行、区域/全图/切片顺序 |
| 回合清理释放资源并隔离旧结果 | `yolo_detector.cpp:133`；`hybrid_tracker.cpp:237`、`:619` | `preprocess_contract_test.cpp:275` 检查取消、等待、复用帧号；`diagnostic_tracker_test.cpp:632` 检查旧回合结果 |
| 看门狗计数与复位顺序不变 | `hybrid_tracker.cpp:618`、`:814` | `diagnostic_tracker_test.cpp:639`、`:685` 检查不可比较回复和无目标回复仍复位 |
| 看门狗阈值与超时动作不变 | `hybrid_tracker.cpp:818` | `diagnostic_tracker_test.cpp:668` 检查第 60/61 次更新、输出及下一帧 Detecting；`:690` 检查 Detecting 不启用看门狗 |
| 结果年龄仅用于观测 | `yolo_detector.cpp:98`、`:164`；`hybrid_tracker.cpp:619` | 延迟结果仍按来源上下文仲裁；`metrics.py:420` 独立计算排队时间和来源采集到消费时间；没有年龄拒绝规则 |
| 外部状态与恢复接受规则兼容 | `src/hybrid_tracker/hybrid_tracker.h:58`；`hybrid_tracker.cpp:252`、`:800` | `diagnostic_tracker_test.cpp:381`、`:527`、`:661`、`:668` 覆盖部分补帧失败、原生跳变、正常丢失及看门狗输出 |
| 被替换请求有独立终态 | `yolo_detector.cpp:115`；`tests/tracking_regression/collect.py:155` | `tests/tracking_regression/tests/test_collect.py:46` 覆盖连续替换、丢失终态、错误后继及被替换请求执行 |
| 元数据与恢复像素独立记录 | `hybrid_tracker.cpp:638`、`:675`、`:716`；`metrics.py:399` | `test_metrics.py:28` 保留历史图像缺失和来源 IoU；原始框与计数可按请求身份关联 |
| 排队、执行、消费年龄分开 | `metrics.py:413`、`:420` | `test_metrics.py:28` 保留 497 ms 排队与 73 ms 执行；时间未知的样本保持未知 |
| 新旧测量定义兼容 | `src/diagnostics/client.cpp` 中 RunStart 版本标记；`metrics.py:398`；`baseline.py:167`；`compare.py:207` | `test_compare.py:77` 验证历史扩展字段不可用，同时既有指标仍可比较 |
| 受控评估包含仲裁成本 | `docs/improve-detection-tracking-implementation-20261015.md:7`；本目录 `README.md` | 六次运行身份、完整/核心窗口、发布间隔、恢复工作、请求延迟、替换次数、准备阶段内存及看门狗限制均有记录 |

`diagnostic_tracker_test.cpp` 和 `preprocess_contract_test.cpp` 的完整位置为 `src/hybrid_tracker/tests/`；表内 `metrics.py`、`baseline.py`、`compare.py`、`test_metrics.py`、`test_compare.py` 分别位于 `tests/tracking_regression/` 和其 `tests/` 子目录。

## 独立复核结果

1. 本轮重新运行修改涉及的 Python 测试：收集器 6/6、指标 16/16、版本比较 15/15，共 37/37 通过。使用 conda base Python，没有访问开发板。
2. 本轮运行 `openspec validate improve-detection-tracking --strict`，结果通过。`git diff --check` 通过。
3. 对参考和候选各 3 次运行调用当前 `baseline.load_run`，六次均通过完整性与文件校验和检查，没有用成功退出代替证据合格。
4. 使用每次运行的 `events.jsonl`、`input.jsonl` 和原始时钟关系重新计算指标。转换为 JSON 的数组表示后，六份结果与留存的 `metrics.json` 全部一致。计算没有改写原始证据。
5. 逐个重新计算 `production-source-match.json` 中 187 个源码文件的 SHA-256。当前文件与记录指纹、实测快照指纹全部一致。该集合包含正式 C++、头文件和回归入口；后续仅增加此报告，不改变候选程序。
6. 核查已有最终集成日志 `candidate/build-delivery.log`：57/57 CTest 通过，耗时 154.68 秒；独立原生 Linux 内存限制检查通过，原有 Python 回归共 56 个用例。该完整构建由任务 1 执行，本轮没有冒充重新完整构建。

设计一致性方面，数值快照保留在既有请求/结果槽位中；来源比较与图像恢复分开；替换与领取共用既有互斥锁；看门狗复位仍在仲裁之前；外部状态和同步补帧规则保持原样；诊断枚举只追加。没有引入第二个推理线程、无界请求队列或结果年龄阈值。

## 按优先级列出的问题

### CRITICAL

0 项。没有未完成勾选项，也没有找到缺失的新增需求实现。本轮无需进入修复阶段。

### WARNING

1. **规划工件语言与直接仓库指令不一致。** `proposal.md:1`、`design.md:1`、`tasks.md:1` 和两个 `specs/*/spec.md:1` 使用英文。用户提供的适用仓库指令要求 OpenSpec 工件使用简体中文；当前 `openspec/config.yaml:2` 却要求英文。本轮报告按用户的直接指令使用简体中文。建议在另行授权的文档整理中统一这些工件及配置，保留需求名称、规范关键词和命令。按本次“忽略 WARNING”要求，不翻译或改写现有规划。
2. **请求与快照边界缺少直接集成断言。** `diagnostic_tracker_test.cpp:623` 手工构造结果快照，`preprocess_contract_test.cpp:201` 手工构造请求快照。因此这些测试没有直接断言 `hybrid_tracker.cpp:580` 在 NanoTrack 更新失败但保留显示框时，实际提交的 `tracker_valid` 必须为 false。`yolo_detector.cpp:113` 的未读结果拒绝分支也没有专门的提交拒绝断言。实现路径存在且清晰，属于测试覆盖不足。建议增加受控失败更新的请求快照观察，以及“结果已完成未消费时提交新请求”的直接断言；按本次要求不补测试。

### SUGGESTION

0 项。

## 证据适用边界

实机记录来自同一块 `HZ-RK3576_WuKong Board`，ADB 序列号为 `5ceb49407502303d`，内核为 `6.1.141-rt36`，RKNN Runtime 2.3.2，RKNPU 驱动 0.9.8。输入为固定 1,200 帧、1920×1200 Gray8 视频片段，使用既有气球 YOLO 和 FP16 NanoTrack 模型。六次运行均保持 120 秒总时限和 50 ms 供帧迟到预算。

该板实际报告约 7.7 GiB 系统内存。准备阶段 RSS 是 Linux 报告的驻留物理内存，不能替代整个回放过程的内存曲线，也不能证明仓库所述 3.8 GiB 配置的内存验收。视频没有人工真值标注，连续有效框不能证明准确率或真实丢失识别延迟。视频未出现“有来源比较框但像素已经淘汰”的实测样本，也未触发看门狗超时；这些路径依靠受控测试与代码审查验证。

没有执行 Jetson、相机、HDMI、长期负载、线程消毒器或飞行验证。clang-tidy 和 clangd 未配置或不可用。这些是本次变更以外的验收或验证强度限制，没有将其计作已通过，也没有据此虚构缺失实现。

已有 `service-before.json`、`service-after.json` 及 `final-service-status.txt` 记录正式跟踪和拦截服务恢复为 active/running，诊断服务保持 inactive。本轮审查没有连接或修改开发板，服务状态没有被本轮操作改变。

## 最终评估与后续交接

没有 CRITICAL。2 项 WARNING 保留，0 项 SUGGESTION。所有适用的技能检查均已运行；按技能标准，可在保留上述改进项的条件下进入归档流程。本轮没有执行或授权归档。

后续视频比较可以继续使用现有候选回归程序，SHA-256 为 `07b48c8ef98018dcf097598cc7a1640656d9b62e7125e188f8c6543dc9748874`。本轮未修改正式实现，不需要为本轮重建候选生产二进制。后续任务仍须分别保留新的运行身份、原始记录和测试报告，不能将任务 1 的六次运行标记为任务 3 或任务 4 的新运行。
