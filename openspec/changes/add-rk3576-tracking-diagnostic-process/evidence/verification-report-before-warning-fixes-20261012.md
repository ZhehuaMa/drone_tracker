# 独立校验报告：add-rk3576-tracking-diagnostic-process

## 校验结论与范围

本变更尚不能归档。发现 **1 项 CRITICAL、5 项 WARNING、0 项 SUGGESTION**。CRITICAL 表示按校验技能必须在归档前处理的问题；WARNING 表示实现偏差或仍需补齐的场景证据。基础模式性能验收任务 7.3 未完成。现有任务框为 **39/40**，该数字不代表 39 项完成条件均得到无条件通过结论。

2026-10-12 按 `.agents/skills/openspec-verify-change/SKILL.md` 执行独立检查。仓库根为 `/Users/mazhehua/projects/nickluo/drone_tracker`，分支为 `rk3576`，HEAD 为 `fde4f350907f60c3c0d9e8c5ac023b6353c3dd62`。本次检查对象还包含工作区中尚未提交的实现。下文代码位置均相对该仓库根，行号对应本次检查的工作区。

检查已读取 `AGENTS.md`、`AGENTS.local.md`、`openspec/config.yaml`。`openspec list --json` 确认本地 OpenSpec 根存在。`status` 确认使用 `spec-driven` 流程，即通过提案、设计、规格和任务清单组织变更。`instructions apply` 返回的全部上下文文件均已读取：`proposal.md`、`design.md`、`tasks.md` 及两份 `specs/*/spec.md`。本变更全部为新增要求，没有删除或重命名要求。

本次只检查并保存报告。没有修改代码、任务勾选、主规格或其他 change，没有提交或归档。没有执行 SSH、停服、部署或新增实机测试。另一 `optimize-rk3576-nanotrack-window-preprocessing` 变更未触碰。

## Summary Scorecard

| Dimension | Status |
| --- | --- |
| Completeness（完整性） | Task Completion：39/40，任务 7.3 未完成，列为 C1。Spec Coverage：24/24 新增要求均找到实现位置；无整条缺失实现。找到实现不等于满足全部约束。 |
| Correctness（正确性） | Requirement Implementation Mapping：24/24 已映射。仲裁、外部截止和基础性能存在偏差，见 W1、W2、C1。Scenario Coverage：29 个场景逐项核对，27 个有受控测试或原始实机记录支撑，其中截止回收存在已复现反例。**Not verified（未完全验证）**：不兼容客户端接入后的实际服务行为、实际日志启动入口的亲和性链路；原因与建议见 W4、W5。 |
| Coherence（一致性） | Design Adherence：8 项设计决策均核对；独立进程、共享区所有权、双阶段同步和只读恢复具有对应实现，决策 5、8 存在 W1–W3 与 C1。实际启动入口链路的运行证据 **Not verified**，见 W5。Code Pattern Consistency：已检查新增模块、构建边界与相邻代码，未发现需要单列的显著风格偏离；静态分析与 Jetson 构建未执行，限制见后文。 |

这里的未验证表示报告覆盖范围有限，不另创归档条件。C1 是当前明确的归档阻塞。其他问题应按其具体影响处理。

## 问题清单

### CRITICAL：1 项

**C1．任务 7.3 的基础模式调用 P99 增量超过 0.2 ms。**

P99 指全部样本排序后第 `ceil(0.99×N)` 个值，用于观察较慢的调用。`tasks.md:62` 仍为 `[ ]`。规格 `specs/rk3576-tracking-diagnostics/spec.md:165` 要求三组关闭/基础成对对照达到门槛，未通过须保留未完成任务。

本次从原始 CSV 独立筛选实际测量窗口，复算全部调用和提交样本。第 2 组 off 为 1526 个调用，P99 为 **247.140985 ms**；basic 为 1635 个调用，P99 为 **264.384067 ms**。增量为 **17.243082 ms**，超过 **0.2 ms**。数值与 `evidence/production-acceptance-20261012/report.md:57` 及 `metrics.json` 一致。全部 P99 及以上慢样本与 CSV 对得上，未发现删样本。

具体原件为 `.docker-artifacts/diagnostic-board-20261012/raw/evidence/pair2/case.json`、`pair2/0-basic/calls.csv`、`pair2/1-off/calls.csv` 及同组 `*-slow-samples.json`。窗口由 `case.json` 的 `measurement_start_ns`、`measurement_end_ns` 确定，筛选规则为起点落在半开区间内。业务测量边界在 `src/drone_tracker_node.cpp:848`，提交测量边界在 `src/diagnostics/recorder.cpp:44`。

**建议：** 保持 7.3 未勾选。先针对第 2 组慢样本定位工作量、帧选择和初始化/补跟踪差异，再在修复后按原门槛重新取得三组证据。不能用另外两组、平均值或异步写盘架构掩盖失败，也不能直接放宽门槛。当前证据不能确定失败的直接根因。

### WARNING：5 项

**W1．外部截止控制没有覆盖各个独立测试进程组，且可能错误声明资源已回收。**

`src/diagnostics/tests/bounded_run.py:37` 只为控制器建立新进程组，超时时在 `:43`、`:47` 终止这个组。`src/diagnostics/tests/board_acceptance.py:87` 又为业务、worker 和 bag 分别使用 `start_new_session=True`，它们不属于控制器的组。控制器能响应 SIGTERM 时会尝试 `Case.close()`，但如果控制器卡住或被后续 SIGKILL 终止，独立组仍可能存活。`bounded_run.py:57` 却仅凭控制器已有退出码设置 `resources_recovered=true`。

本次在 macOS、本机 conda base Python 上做了不涉及模型、ROS 或板卡的短复现：控制器启动一个新会话中的 `sleep 3`，随后等待；外部预算 0.5 秒，收尾 0.2 秒。控制器在 **0.306129292 秒**返回 `-15`，记录 `timeout=true`、`resources_recovered=true`，但独立子进程仍存在。本次已对该确切子进程发送 SIGKILL 清理。现有 `deployment/tests/test_diagnostics.py:48` 仅覆盖单进程睡眠，未覆盖这种结构。

该问题对应 `specs/rk3576-tracking-diagnostics/spec.md:157` 和 `design.md:178` 的全运行截止与有限回收要求。现有成功实机实例均在各自预算内结束，最终也无残留；这个反例不撤销这些已完成运行的时长事实，却说明超时保护尚不完整。

**建议：** 由外部控制器登记每个独立进程组，或使用专属 systemd scope/cgroup（由系统统一管理的一组测试进程）。截止时在同一总预算内终止全部登记实例，核对实际存活状态后才写资源回收结论。增加“控制器无法完成清理、独立业务组仍存在”的短故障测试。维护恢复守护的 30 分钟总窗口不能替代单项 120/150 秒截止。

**W2．部分仲裁事件不能准确重建实际分支和跳变计数。**

仲裁指结合检测结果和跟踪结果决定继续、重初始化或清框。`src/hybrid_tracker/hybrid_tracker.cpp:661` 的分支条件是 `!ok || current_iou < iou_reset_threshold`，但 `:663` 一律写 `LowIou`。若本次 NanoTrack 更新失败而两个框的 IoU 很高，日志仍会称“低 IoU”。IoU 是两框交集面积除以并集面积。普通跟踪更新的 `ok` 没有进入这条决策事件，因此读者无法从该事件辨认这两个原因。

同文件 `:487` 增加 NanoTrack 跳变计数，只在达到容忍次数的 `:502` 才记录决策。`:500` 已将实际计数清零，`:505` 却填入 `jump_tolerance-1` 和 `jump_tolerance`。这既不是实际重置后的计数，也不符合 `src/diagnostics/README.md:43` 对 `Decision.counts[0..3]` 的统一说明。未达到清框次数的跳变和随后恢复为零没有完整计数记录。NanoTrack 与 YOLO 跳变还使用 KF 预测框、前一框和位置/尺寸阈值判断，见 `:478`、`:620`；现有事件 `:503`、`:632` 没有保存全部实际依据。

对应 `specs/rk3576-tracking-diagnostics/spec.md:77`、`design.md:119`。第五次低 IoU 的受控案例在 `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp:380` 已覆盖，但不能代替高 IoU 下 `!ok`、未达门槛跳变及跳变归零等路径。

**建议：** 保持算法分支不变。将本次跟踪返回值和真实分支条件保存为数值字段，明确区分跟踪更新失败与低 IoU。对每次跳变/归零记录实际计数前后值，并保存实际参与比较的框及适用阈值。统一事件字段说明，增加上述定向场景的断言。

**W3．NanoTrack 初始化失败只保留阶段，没有保留已有数值错误码。**

`src/hybrid_tracker/nano_tracker.cpp:65` 只把阶段号写到 `values[0]`。模板准备失败 `:91` 有 `prepared.error`，模板推理失败 `:102` 有 `initialized.error.code`，但 `:92`、`:103` 都未保存这些数值。可用错误类型见 `src/hybrid_tracker/preprocess/model_input.h:27` 与 `src/hybrid_tracker/model/model_executor.h:24`。例如“输入仍被占用”与“平台准备失败”都会只留下阶段 2。

这是 `design.md:125` 要求保留第一层失败阶段和底层错误码的偏差。初始化起止、返回和补跟踪统计已有实现，本项不按整条需求缺失处理。当前测试 `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp:399` 只检查存在 `InitFailed`，没有断言错误码。

**建议：** 在固定数值字段保存可取得的 `PreprocessError` 或 `ModelError`，并标明错误域与有效性。异常没有数值错误码时显式标为未知。用两种不同错误验证导出的诊断可以区分它们，不在热路径拼接错误文本。

**W4．“不兼容客户端接入”场景只有局部检查证据。**

`src/diagnostics/worker.cpp:96` 有接入长度、版本、区域大小、描述符及用户身份检查。`src/diagnostics/tests/diagnostic_test.cpp:51` 仅直接调用 `valid(Control)` 验证版本/长度拒绝。进程测试从 `:246` 开始使用合法客户端；未看到真实套接字上传入未知版本或超大区域，并同时验证已有健康会话继续消费的测试。生产板 protocol 测试运行的也是这套程序。

因此 `specs/production-binary-deployment/spec.md:41` 场景为 **Not verified（部分验证）**。没有证据证明当前拒绝逻辑必然错误，但不能把纯函数检查当作完整接入场景通过。

**建议：** 在隔离 worker 测试中通过真实控制连接发送不兼容消息和区域，断言拒绝、描述符回收及健康会话继续工作。继续使用已有 120 秒外部预算，不需要操作正式服务。

**W5．实际日志启动入口的 CPU 链路没有运行证据。**

CPU 亲和性表示进程允许在哪些核心运行。`deployment/run-diagnostics:6` 读取已安装策略，`deployment/diagnostic-launch.py:31` 设置并读回范围，`:36` 设置普通调度，代码路径与设计相符。然而隔离 systemd 测试在 `src/diagnostics/tests/service_acceptance.py:52` 将 `ExecStart` 改为 `taskset` 直接运行 worker。它绕过了 `run-diagnostics`、`diagnostic-launch.py` 和生产 RuntimeDirectory 的创建路径。直接进程测试 `board_acceptance.py:143` 同样直接使用 `taskset`。

原始资源确实证明被测 worker 的实际 CPU 范围为 4–7、调度为 `SCHED_OTHER`。这证明被测 worker 的结果，不足以证明生产服务入口和直接入口的完整链路都已运行。`deployment/tests/test_diagnostics.py:18` 也只检查配置解析。因此 `specs/production-binary-deployment/spec.md:33` 场景为 **Not verified（部分验证）**。

**建议：** 用隔离安装根或受控替身执行真实入口，验证策略传入、设置、读回、普通调度和 `exec` 参数。另覆盖亲和性设置失败的退出结果。无需覆盖或启动正式入口即可补齐这条证据。

### SUGGESTION：0 项

没有另列纯风格或轻微改进建议。

## 逐需求与场景映射

下面两表涵盖全部 **24 条新增要求、29 个场景**。场景支撑可来自受控模型测试、代码组合检查及现有原始实机记录。数值示例用于说明行为；对类似行为的证据不宣称逐字复现例子的所有输入值。表中“支持”也不豁免同一要求正文的其他偏差。

### rk3576-tracking-diagnostics：20 条要求、22 个场景

| 规格位置与要求 | 实现位置 | 场景、测试或原始证据与结论 |
| --- | --- | --- |
| `spec.md:9` 独立进程记录且业务不等待写盘 | `src/diagnostics/recorder.cpp:8`、`worker.cpp:289` | 写盘阻塞：`diagnostic_test.cpp:206` 在阻塞同步期间提交 5000 次并验证立即返回/缺口；raw `full/` 中暂停 worker 后业务继续。支持。 |
| `spec.md:17` 模式与业务契约兼容 | `src/drone_tracker_node.cpp:46`、`:114`；五份参数 YAML | 同输入三模式：`diagnostic_tracker_test.cpp:411`、`:561` 核对框、状态、分数、模型调用、像素哈希、帧引用；`ros_video_test.cpp:950` 核对拒绝运行期修改。off 的准备直接返回，见 `client.cpp:30`。支持。 |
| `spec.md:25` 身份及时间关联 | `event.h:103`、`recorder.cpp:53`、节点 `:906` | 消息时间早于发布：`diagnostic_tracker_test.cpp:524` 用延迟保持预测时间并验证单调耗时；发布另存 ROS 调用时刻。支持。 |
| `spec.md:33` 相机帧去向 | `rk3576_camera_capture_base.cpp:395`、`ar0234_camera_frame_source.cpp:172`、`frame_source_ops.h:33` | 补跟踪期间采集继续：`diagnostic_tracker_test.cpp:507` 逐一核对跳过帧；raw `lifecycle/` 有 3568 条采集、906 条节点跳过、2627 条选择。不能只把计数差当采集丢失，需按身份和处置核对。支持。 |
| `spec.md:41` 调用及历史处置 | `hybrid_tracker.cpp:304`、`:369`、`:423` | 来源淘汰：`diagnostic_tracker_test.cpp:331` 的小历史缓存产生真实 `HistoryMiss`，再经存储和中文 inspect 核对。支持。 |
| `spec.md:49` 接受与拒绝 | `yolo_detector.cpp:88`、`:101` | 推理中接受下一请求：`diagnostic_tracker_test.cpp:456` 验证在途 A、接受 B、待检测占用时拒绝 C。锁内快照、锁外提交。支持。 |
| `spec.md:57` 检测执行与结果生命周期 | `yolo_detector.cpp:115`、`:156`、`:225` | 未读覆盖：上述 overwrite 测试与中文导出。待执行取消：raw pair2 请求 344、pair3 请求 344 为已接受、未见 DetectStart、随后 Cleaning 取消；代码清理保存对应身份。两场景支持。 |
| `spec.md:69` 阶段与过滤 | `yolo_detector.cpp:344`、`:399` | 低于阈值仍有响应：原始各 `export-*/events.jsonl` 共 4814 条可观测阶段记录有候选、最高分低于阈值、通过阈值数为 0。原候选循环顺带统计，未新增遍历。支持；没有独立的精确 0.42 测试。 |
| `spec.md:77` 仲裁与清框 | `hybrid_tracker.cpp:565`、`:632`、`:715` | 第五次低 IoU：`diagnostic_tracker_test.cpp:380` 验证计数 4→5 和关键标志。该场景支持，但其他分支存在 W2。 |
| `spec.md:85` 初始化及逐步补跟踪 | `hybrid_tracker.cpp:250`、`:276`、`nano_tracker.cpp:64` | 部分补帧失败仍成功：`diagnostic_tracker_test.cpp:331` 保留 2 步失败与成功汇总，经中文导出核对。支持；失败数值错误码的设计偏差为 W3。 |
| `spec.md:93` 输出与发布 | `hybrid_tracker.cpp:19`、节点 `:864`、`publication.h:8` | 未消费：`ros_video_test.cpp:955` 核对重复时间仅发布一次、早退和跳过发布；异常发布测试 `diagnostic_tracker_test.cpp:545` 保留原异常传播。支持。 |
| `spec.md:101` 内存与缺口 | `protocol.h:12`、`:63`、`measurement.h:26`、`recorder.cpp:19` | 普通区满仍提交关键事件：`diagnostic_test.cpp:30` 覆盖普通/关键满、环回与序号。原始 full 有缺口 3849，worker-kill 有 1232。支持。 |
| `spec.md:109` 同步位置 | `storage.cpp:184`、`:204`、`worker.cpp:209` | 写入成功但同步失败：`diagnostic_test.cpp:158` 分别注入两次同步失败；raw `sync-after-data/` 有非零 written/synced 分离、恢复提交偏移。支持。 |
| `spec.md:117` 关键同步及实际落后 | `storage.cpp:194`、`:201`、`worker.cpp:194`、节点 `:90` | 100 ms 配置下拥塞落后：受控阻塞检查加 raw full 的超过 12 秒未同步年龄，健康事件不带 Complete；业务摘要报告退化。支持等价的积压行为，未单独复现“同步调用阻塞恰为 800 ms”。 |
| `spec.md:125` 尾部恢复 | `storage.cpp:235`、`inspect.cpp:119` | 最后批次截断：`diagnostic_test.cpp:131` 对小样本每个截断长度校验完整前缀；`:139` 覆盖 CRC、版本、缺提交。原件只读导出检查在 `:302`。支持。 |
| `spec.md:133` 存储保护及有界生命周期 | `storage.cpp:105`、`client.cpp:69`、`:144` | 空间不足：`diagnostic_test.cpp:239` 令总量仅 1 字节而触发 ENOSPC；未自动删除历史记录。缺服务准备与收尾预算检查在 `:362`。支持受控边界，未耗尽生产介质。 |
| `spec.md:141` 离线事实与不足 | `inspect.cpp:140`、`:176`、`:189` | 缺少结束：`diagnostic_test.cpp:289` 用真实存储/inspect 输出“结果未知”、重复和缺口并验证原件字节不变。支持。 |
| `spec.md:149` 复用细分观测 | `timing.h:8`、`:62` | 同过程 basic/detail：三模式受控测试核对调用次数；raw detail 有 38133 条阶段事件。未观察阶段保留 Observed=false，不能解读为耗时零。支持。 |
| `spec.md:157` 验证时限与截止 | `bounded_run.py:24`、`:37` | 到达截止：现有单进程测试返回失败；本次独立会话反例证明完整回收不成立，见 W1。实际各实机窗口均未超限。 |
| `spec.md:165` 实机开销与故障隔离 | `measurement.h:29`、节点 `:848`、`analyze_board_acceptance.py:84` | 性能退化必须失败：第 2 组未通过且任务未勾选，见 C1。故障隔离：raw business-kill、worker-kill、full、write-after-data、sync-after-data、services 原件证明业务继续、异常尾部/缺口和独立重连。故障场景支持；基础性能要求未通过。 |

### production-binary-deployment：4 条要求、7 个场景

| 规格位置与要求 | 实现位置 | 场景、测试或原始证据与结论 |
| --- | --- | --- |
| `spec.md:5` 完整打包与回退 | `deployment/package_core.py:122`、`:140`、`:244`；`deploy.sh:51`；`rollback.sh:9` | 缺 worker：raw services/missing-package 的真实 `deploy.sh --check` 非零。旧回退：`service_acceptance.py:130` 的隔离路径变体恢复旧入口，5 个 .dtd 原件保持相同。两场景支持。完整正式新包安装与匹配新版本回退未执行，见限制。 |
| `spec.md:17` 独立生命周期 | 两个 `.service`；`worker.cpp:306`；`client.cpp:118` | 主业务异常退出：raw business-kill 有 AbnormalEnd 且 worker 继续。日志不可用：`ros_video_test.cpp:950` 在无诊断服务环境仍执行并发布；`diagnostic_test.cpp:362` 验证准备预算，代码不新增日志线程。两场景有组合证据，实际 systemd 启动失败路径未单独注入。 |
| `spec.md:29` CPU 范围与入口 | `run-diagnostics:6`、`diagnostic-launch.py:31` | 实机 worker 读回 4–7、普通调度；测试重写 ExecStart 后绕过真实入口。因此入口链路仅部分验证，见 W5。 |
| `spec.md:37` 端点与历史保护 | `worker.cpp:53`、`:96`、`:267`；`inspect.cpp:86`；`deploy.sh:72` | 不兼容客户端：代码与 valid() 局部测试，完整场景未验证，见 W4。部署预检：修改服务的命令在只检查退出点之后；raw services/missing-package 有失败输出与状态快照。该预检场景支持。 |

## 设计与任务核对

| 设计决策 | 核对结果 |
| --- | --- |
| 1．独立日志服务 | worker 为独立单循环；无专用业务日志线程。主服务只有 Wants/After，没有 PartOf/BindsTo。真实 systemd 重启、异常退出、单独停止的 PID 证据见 raw services/case.json；主业务退出不终止 worker。实际生产启动包装器尚属 W5 的证据限制。 |
| 2．共享内存、通知只唤醒 | 创建方构造共享原子、预触及并加禁止缩放封印，见 `client.cpp:41`；worker 校验后映射；四个生产者分别写各自双环。release/acquire 保证事件填完再可见，见 `recorder.cpp:15`、`worker.cpp:139`。跨进程 50000 次可见性测试及通知遗漏测试已读取。 |
| 3．有界内存、关键容量、唯一重连 | 普通 1024 槽、关键 512 槽，共四组。关键容量为三分之一。共享区和验收采样数组有小于 8 MiB 静态检查；worker 2 会话、512 KiB 批次。pidfd 确认旧消费者退出，重用原共享区；唯一端点锁拒绝第二实例。 |
| 4．用身份连接阶段 | 可选 FrameIdentity 只含数值。现有帧对象沿原路径携带，无额外图像查找表。通用 Frame 返回未知，视频不伪造采集身份。调用、算法帧号、请求与来源分开。时钟对应记录只见轮次开始；人为墙钟跳变没有本次验证证据。 |
| 5．基础因果链与细分阶段 | 调用、历史、检测阶段/覆盖/取消、恢复、发布已有事件。受控三模式输出和调用一致。仲裁部分字段与失败底层码有 W2、W3。`DetailObserver` 固定数组，无动态字段；回调未加入文本或文件读写。 |
| 6．批量与双阶段同步 | 完整 Batch 写入后才释放环槽，见 `worker.cpp:209`。先 fdatasync 数据，再写 Commit，再 fdatasync 提交；仅两次成功后推进 synced，见 `storage.cpp:204`。创建目录/段均同步父目录。重连用 O_EXCL 新段，不续写异常尾部。 |
| 7．保护、异常恢复、只读工具 | 默认 64 MiB 分段、1 GiB 总量、5 GiB 空余，不删除旧记录。CRC/长度/版本前缀检查，原件不修改；去重按运行/生产者/序号。同步位置依 Commit，并提示须结合真实集合和缺口，不能把最大序号当连续前缀。 |
| 8．短时对照与隔离 | 三组和 detail 的原始采样/慢样本可复算；实际每组少于 150 秒，各故障实例少于 120 秒。基础门槛失败 C1；外部超时收尾结构有 W1。没有把短测提升为长期或掉电保证。 |

任务 1.1–2.6 的传输和存储实现与受控测试有对应位置。任务 3.1–5.6 的帧、检测、模式、导出有上述映射，但 W2、W3 指出勾选后的内容仍有偏差。任务 6.1–6.4 有部署脚本和真实隔离服务证据，入口运行与协议拒绝覆盖仍受 W4、W5 限制。任务 7.1–7.7 的原始窗口均存在；7.1 的超时结构存在 W1，7.3 保持未完成，其余实机勾选应仅解释为报告记录的短时范围。

## 实机证据独立复核

指定板卡为 **HZ-RK3576_WuKong Board**，不是开发板 1/2。保存的现场与运行日志对应 Ubuntu 22.04.5 ARM64、`6.1.141-rt36`、RKNN Runtime **2.3.2**、RKNPU 驱动 **0.9.8**。系统 Runtime 指纹为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。

模型为既有气球 YOLO `balloon_best_zoo6_int8_fp16_cls.rknn`，以及 FP16 NanoTrack template/search backbone、head。YOLO 类别 0、阈值 0.5。性能输入为真实气球录像提取的 25 帧循环生成的固定 MJPEG 视频；它可以固定输入，不提供目标准确率真值。性能视频实例保持录像关闭、bag/HDMI 启用；真实 SC233HGS 相机录像、bag、HDMI 并行使用另一独立生命周期实例。两者不能混为同一输入验收。

本次逐窗口重新计算结果如下，提交 P99 用微秒，调用增量用毫秒：

| 组 | 总窗口秒 | 提交 P99 μs | off 调用 P99 ms | basic 调用 P99 ms | 增量 ms | 消费帧率下降 | 结论 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 106.935450 | 14.444 | 206.807560 | 188.736616 | -18.070944 | -6.362741% | 本组达到列出门槛 |
| 2 | 107.038023 | 14.053 | 247.140985 | 264.384067 | **17.243082** | -7.143331% | 调用增量失败 |
| 3 | 106.979598 | 13.524 | 240.400083 | 208.920116 | -31.479967 | -7.599280% | 本组达到列出门槛 |

负增量和负帧率下降保留原符号，不代表诊断提高性能。异步模型会改变实际选择帧及补跟踪次数。三组采样溢出均为 0，健康日志缺口均为 0。detail 单独窗口 110.053708 秒，basic/detail 调用 P99 为 212.433556/233.820586 ms，增量 21.387030 ms；不并入基础通过指标。

用独立 Python 解析器只读核对正式有效案例的 **19 份 `.dtd`** 原件。核对了记录头、长度、版本、CRC 与 Commit 引用偏移，未发现损坏记录。CRC 是内容完整性检查，不能证明所有业务事件都曾提交。各原件事件数与已有导出计数一致。健康模式没有缺口；故障模式中的缺口或未知尾部是预期退化，不得补出不存在的事件。

延后同步故障 raw `sync-after-data/` 的已恢复提交偏移为 **407568**，node 提交序号为 **1425**，完整可校验前缀为 **416000** 字节。这验证离线工具没有以更长文件前缀冒充成功同步位置。write-after-data 和 sync-after-data 在出现 EIO（文件输入输出错误）后，业务摘要中的 written/synced 保持原值而 generated/缺口继续增长。末观测不等于退出后的最终总数。

原始证据根为 `.docker-artifacts/diagnostic-board-20261012/raw/evidence/`。本次核对的指纹：

| 文件 | SHA-256 |
| --- | --- |
| `raw-evidence-final.tar.gz` | `82191b36ca2f3e282db8cf85a1c5bfee9e428582e2d7cfc42009a821db333f85` |
| 最新 `scripts.tar.gz` | `6c447128a163334fb466970b149f514591ee77396459e274dbf813cfd09e241b` |
| `fixed-balloon.avi` | `c25aec33208f5dfca315eb9c2c4fb86b04d98883ec5d74b68f5ac7746a669a95` |
| 新节点 | `f0699dcceedadd2237d323f6c5c097a2c122f941da6e993028dca4e08e727497` |
| worker | `2cd398014cd5991d36eb6f457e1bea44d2a2e919195fbdffa73dd539a6a75e38` |
| inspect | `06592db4db093c830c43634c672b8eb78342393bb3528206b4eb5f1aa5f72be6` |

SHA-256 用于核对文件内容。最新脚本包内 7 个验收脚本/注入源码与原始证据副本、当前仓库内容逐字节一致。没有使用早期 `scripts-initial.tar.gz` 代替最终脚本。`source.tar.gz` 中 C++ 实现与当前工作区一致；其中部分验收脚本和 README 属早期版本，应按最新脚本包审查。

## 正式服务恢复事实

`evidence/production-acceptance-20261012/restore.json:3` 保存首次 armed 起点 `13178710780406 ns`，`:4` 保存恢复点 `14074965202240 ns`。两者相差 **896.254423533 秒**，包括中间恢复和修脚本期间，不是连续停服时长。`:17` 的恢复启动退出码为 0，`:19` 明确记录 PID **7405**、`active/running`、`enabled`。

原始 `protected-before.json` 与 `protected-after.json` 逐字节相同，正式 current 仍指向 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。配置、模型和系统 Runtime 未改变。`hdmi-after.txt` 记录 card0 显示线程 TID 7438、card1 正式 PID 7405 持有 DRM master，即显示控制权。`isolated-processes-after.json` 为 `[]`。这证明记录时已恢复正式服务并无隔离残留；本次未重新 SSH 查询之后的即时状态。

## 已执行检查与未验证限制

本次实际执行并通过 `openspec validate add-rk3576-tracking-diagnostic-process --strict` 和 `git diff --check`。严格 validate 只校验 OpenSpec 工件格式与关系，不能证明代码或性能正确。

已读取 ARM64 容器构建和 CTest 原始日志。最新 `.docker-artifacts/log/test-console.log` 为 **47/47、31.45 秒**；早期保存日志为 47/47、31.84 秒。CTest 是 CMake 的测试运行器。生产板 `board-protocol-window.json` 为 5.232 秒并通过。此处复核既有结果，没有重复构建或运行全套测试。

既有 OSD 测试失败已经在未修改 HEAD 上复现，`evidence/osd-head-release-results.txt:24` 为失败返回，`osd-head-release-failure.txt:1` 为 `unchanged video frame produced diagnostic log`。OSD 是屏幕叠加显示功能。本次没有改 OSD 代码，也没有反复扩测；根因仍未解决，不将最后一轮全通过解读为基线偶发问题已消失。

适用但仍未完全验证的内容如下：

- **Scenario Coverage：不兼容客户端完整接入场景。** 只有 `valid()` 与代码检查，缺少真实恶意/错误消息接入同时保持健康会话的证据，见 W4。
- **Scenario Coverage / Design Adherence：真实日志启动入口。** CPU 4–7 和普通调度已在被测 worker 上读回，但真实包装器被隔离服务测试绕过，见 W5。
- **完整正式打包/安装链路及新格式版本之间回退。** 本次有安装产物、缺件预检和旧版本隔离回退；没有执行正式新包升级。不能由这些结果宣称每个正式部署状态均已运行。
- **生命周期中录像子进程的运行期资源采样。** 旧实际脚本没有取得子进程明细，记录明确为未知；三个 OFF 边界的业务/日志/bag 计数一致，录像完成和最终扫描无残留只证明有限范围。不能宣称全部子进程在整个窗口都没有资源增长。
- **墙钟跳变以及所有异常并发排列。** 已检查固定身份、单写单读、跨进程可见性和已测重连；没有把这些样本扩展为穷尽所有调度排列。人为时钟跳变未注入。
- **细分包装阶段的精确耗时。** SearchBackbone 包装阶段缺少观察，底层 Run/内存绑定有执行记录。未观察的包装阶段不能解释为模型未执行或零耗时。
- **静态分析及 Jetson 构建/硬件回归。** 本地没有可运行的 clang-tidy/clangd 配置，也没有 Jetson SDK/硬件。已审查 RK3576 CMake 条件保护；不能替代参考平台构建结果。

长期稳定性、实飞准确率、真实断电、强制重启和介质损坏没有执行。前几项没有此次短测证据；后三项不是本 change 要求的实验，不能擅自新增为归档前置任务，也不能宣称已通过。

## Final Assessment

**发现 1 项关键问题，归档前须处理；另有 5 项警告，0 项建议。** 基础模式性能没有通过，任务进度保持 39/40。全部要求已找到实现并逐场景检查，但存在实现偏差和两项部分未验证场景，不能称全部检查通过。未完整验证的 Scenario Coverage / Design Adherence 子项及其他证据限制已逐项列出。本报告不执行修复、放宽门槛、修改勾选、正式升级或归档。
