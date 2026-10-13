# 独立重新校验：add-rk3576-tracking-diagnostic-process

## 结论与范围

2026-10-12 独立重新校验当前工作区，结果为 **1 项 CRITICAL、1 项 WARNING、0 项 SUGGESTION**。CRITICAL 是归档前必须处理的关键问题；WARNING 是应修正的实现偏差或场景覆盖问题。任务仍为 **39/40**，唯一未完成项是 7.3。最终产物完整十组成对性能对照只有 **7/10** 达到全部单组门槛，低于至少八组的要求，不能归档为全部达标。

原报告五项警告 W1–W5 的修复及对应验证成立。本次另发现 **W6：EOF 补取和补跟踪事件填入帧时间，却遗漏“时间已知”有效位**。EOF 表示视频输入结束，补取路径用于取出仍在队列中的末帧；有效位用于说明数值字段是否可解释。本报告不实施修复。

仓库根为 `/Users/mazhehua/projects/nickluo/drone_tracker`，分支 `rk3576`。源码引用相对于仓库根，行号对应本次工作区。工件路径相对于 `openspec/changes/add-rk3576-tracking-diagnostic-process/`。本次只保存报告和复核结果，没有改代码、任务框、主规格或另外两个用户 change，没有提交、归档、部署或停服。

旧报告完整保存在 [verification-report-before-warning-fixes-20261012.md](verification-report-before-warning-fixes-20261012.md)。其 SHA-256 为 `6bd23a3d7279ce6513aa39518b5957722e19d65bf2fbe04eec9deb226b0e5d76`。SHA-256 是核对文件内容的指纹。旧三组数据和旧节点指纹保留为历史，未替代当前结果。

## 工作流及 Summary Scorecard

已完整读取 `.agents/skills/openspec-verify-change/SKILL.md`、`AGENTS.md`、`AGENTS.local.md`、`openspec/config.yaml`。CLI `list --json` 确认本地 OpenSpec 根存在。`status --json` 使用 `spec-driven`，即通过提案、设计、规格和任务组织变更。已读取 `instructions apply --json` 返回的全部 `contextFiles`：proposal、design、tasks 和两份 delta specs。delta specs 是本变更新增规格。没有额外链接上下文、store、删除或重命名要求。CLI 的规划完成标志不能代替实施验收。

| Dimension（维度） | 检查结果 |
| --- | --- |
| Completeness（完整性） | Task Completion：39/40，7.3 未完成，见 C1。Spec Coverage：24/24 新增要求找到实现及对应检查，没有整条缺失实现；不把找到代码计为全部满足。删除/重命名检查不适用。 |
| Correctness（正确性） | Requirement Implementation Mapping：24/24 已映射，性能要求失败，时间有效位存在偏差。Scenario Coverage：全部31场景逐项检查；原 W4/W5 已补齐，EOF 时间有效位缺少断言且补跟踪原件证实错误，见 W6。Not verified（未验证）：最终二进制全部相机/细分/存储注入实机组合、正式完整升级与新格式回退、实际八组成功结果、systemd日志启动失败组合分支、墙钟跳变及穷尽并发排列。 |
| Coherence（一致性） | Design Adherence：8项决策逐项检查，原W1–W5偏差已修复；决策4存在W6，决策8性能未达C1。Code Pattern Consistency：RK3576条件链接、定长字段、相邻代码风格及热路径边界已检查，没有模式建议。Not verified：Jetson构建/硬件回归、clang-tidy/clangd静态分析及未注入的墙钟跳变。 |

上表未验证项均没有计为通过，具体范围见末尾。场景可以由真实公共算法受控返回值、跨进程测试、代码核对和原始实机记录共同支撑。示例数值的同类行为证据不等于逐字复现0.42、388ms或800ms。

## 当前问题

### CRITICAL：1项

**C1．最终完整十组基础模式对照仅7/10通过，任务7.3未完成。**

`tasks.md:62`、`specs/rk3576-tracking-diagnostics/spec.md:165` 和设计决策8要求完整十组、至少八组各自同时满足全部单组门槛。第7–9组调用P99增量分别为 **58.941157、11.029482、5.285951 ms**，均超过 **0.2 ms**。P99按窗口内全部样本排序后第 `ceil(0.99×N)` 个值计算。其余指标通过不能抵消失败。

本次依据每组 `case.json` 实际半开窗口 `[measurement_start_ns,measurement_end_ns)`，读取全部 `calls.csv` 和 `submissions.csv` 独立复算。同时核查全部P99及以上慢样本、采样溢出、健康缺口、完成标记、超时及实际控制组回收，得到7/10。未依赖现有汇总代替计算，没有选择性删组、删样或平均抵消。任务保持未勾选正确，独立结果见 [reverification-recompute-20261012.json](reverification-recompute-20261012.json)。

**建议：** 从第7–9组全部慢调用、来源帧与补跟踪工作量定位增量。修复后保留旧批和本批，对最终产物执行新的完整十组；至少八组各自同时满足原门槛才能勾选7.3，不能只补挑通过组。本次不实施。

### WARNING：1项

**W6．EOF补取及补跟踪事件的帧时间与有效位不一致。**

`src/drone_tracker_node.cpp:820` 的EOF `FrameSkip` 和 `:824` 的 `FrameSelect` 填入 `frame_time_us`，没有设置 `FrameTimeKnown`。正常路径 `src/platform/frame_source_ops.h:37` 会设置该位。`src/hybrid_tracker/hybrid_tracker.cpp:283` 的 `CatchupStep` 同样填时间，但只设置分数、成功和框有效位。`src/diagnostics/event.h:86` 定义 `FrameTimeKnown=4U`。

这与 `specs/rk3576-tracking-diagnostics/spec.md:25` 的时间已知/未知契约和设计决策4不一致。按有效位读取的消费者会把实际已取得的帧时间解释为未知。最新pair1的 `run_id=78472828113627`、生产者序号75、调用9的补帧事件，`frame_time_us=1791807217287916`，`flags=208`，没有4位。独立统计最新十组原件，共 **9522条**补跟踪步骤出现时间非零但该位未设。

`src/tests/ros_video_test.cpp:970` 的EOF测试只断言末帧发布数量；`diagnostic_tracker_test.cpp:339` 的补帧测试核查失败数与恢复结果，均没有核查这个字段。缺陷影响诊断解释，不改变模型、ROS消息或跟踪结果。

**建议：** 在上述EOF事件及补帧步骤取得有效时间时同步设置 `FrameTimeKnown`。增加数值/有效位断言，未知输入仍保持未知。无需改变时间算法、消息语义或协议版本。本次不修复。

### SUGGESTION：0项

没有另列纯风格建议。未验证限制不自动成为新的归档前置实验。

## 原W1–W5独立复核

下文 `R` 指仓库内 `.docker-artifacts/diagnostic-warning-fixes-20261012/raw-complete/`。`R/evidence/services/` 是保留的早期失败例，最终成功例是 `services-final/`，没有混用。

| 原警告 | 当前代码与真实覆盖 | 本次结论 |
| --- | --- | --- |
| W1 外部截止遗漏独立进程 | `bounded_run.py:16` 扫父子关系，`:35` 核对启动时间后发信号，`:84` 开启Linux `PR_SET_CHILD_SUBREAPER`。子收割者接管已脱离会话的孤儿后代。`:95`读启动前登记systemd单元，`:115`核对控制组实际成员，`:135`停服务阻止重启，`:160`扫描实际残留，`:163`同时要求时间/回收达标。不支持平台先拒绝启动。 | `R/evidence/deadline-final/window.json` 为 **2.628961秒/3秒**，控制器-9、timeout=true、passed=false，后代和控制组为空、resources_recovered=true。探针预期超时失败，外层result通过表示回收成立，未混淆两层结果。`deployment/tests/test_diagnostics.py:103` 覆盖硬退出/独立会话，`:134`覆盖故意无法回收不得通过；最终Python6/6通过。120/150秒契约未放宽。**关闭W1。** |
| W2 仲裁理由和实际计数 | `hybrid_tracker.cpp:713` 高IoU更新失败用追加 `NanoUpdateFailed`；低IoU仍保留该理由，`:594`记录真实更新返回。`:487`清框前保存框与计数，`:514`保存增加值，`:552`保存最终值；`:500`、`:661`保存真实KF/历史/前一框比较依据和阈值。IoU为两框交集占并集比例；KF为预测运动的卡尔曼滤波器。 | `diagnostic_tracker_test.cpp:450` 真公共算法注入高IoU更新失败；`:470`核对0→1、1→2、2→0及真实5→6→0、清框前框。原第五次低IoU`:388`、来源淘汰/部分补帧`:339`及三模式`:655`仍执行。最终板端tracker-window **0.458997秒**通过并回收。**关闭W2。** |
| W3 初始化底层数值码 | `nano_tracker.cpp:64`保存阶段，`:99`复用实际PreprocessError，`:110`复用ModelError。错误域1为输入准备、2为模型执行；ErrorCodeKnown表示码可用，异常未知。`inspect.cpp:72` JSONL未知null，`:181`CSV未知空，`:195`中文未知。JSONL是每行JSON，CSV是逗号分列文本。 | `diagnostic_tracker_test.cpp:419`真实注入2个预处理码、2个模型码及1个无数值异常，验证域/码/有效位并经存储和inspect核对中文；`diagnostic_test.cpp:382`验证旧记录没有位则未知。最终测试程序指纹匹配。**关闭W3。** |
| W4 不兼容接入缺真实覆盖 | `worker.cpp:70`计实际收到的所有描述符，`:79`立即关闭额外项，`:99`要求恰为2个，检查截断、版本、区域大小、封印、eventfd类型。SCM_RIGHTS是本机socket传递已打开资源句柄的机制；原第三个被误接受的问题已改。 | `diagnostic_test.cpp:255`真socket连真实worker，在健康会话旁执行 **12类×2轮**拒绝，`:304`断言EPROTO（协议无效），`:306`区域未认领，`:312`fd数量回基线，`:318`健康消费继续。板端protocol-window **5.717710秒**通过，console确认全部24次及回收。**关闭W4。** |
| W5 真实启动入口被绕过 | `run-diagnostics:6`读已装CPU策略；`diagnostic-launch.py:32`设置/读回、`:36`普通调度、`:46`exec替换成worker。`diagnostic_entry_fixture.py:23`仅重定向固定路径并存原件/副本哈希。`entry_service_probe.py:58`保留真实模板入口和RuntimeDirectory；该目录由systemd在入口前创建、停止后回收。 | `R/evidence/entry/{direct,service}/result.json`确认CPU4–7、SCHED_OTHER普通调度及参数；亲和性失败非零且不exec，服务前置目录断言、0700读回、停止消失成立。`service_acceptance.py:55`日志ExecStart已使用真实包装器，最终services-final异常/重启/第二消费者/预检/旧回退通过，外部 **44.089296秒**且实际回收。**关闭W5。** |

协议仍为 **版本1、256字节、64字节对齐**。新增类型、理由和标志追加在既有编号之后。`event.h:137`、`protocol.h:63`及 `measurement.h:26`静态检查布局与业务共享区/验收数组总预算≤8MiB；MiB为1024×1024字节。旧记录没有新有效位时不解释成新字段，没有移动旧理由编号。

## 逐需求与全部31场景映射

诊断规格为20条新增要求、24场景；部署规格为4条、7场景。新八/七组场景使总场景数由旧报告29变为 **31**。表中行号来自对应delta spec；没有删除、重命名要求。

| 诊断spec位置、要求 | 当前代码 | 场景支撑与限制 |
| --- | --- | --- |
| :9 独立进程、不等写盘 | recorder.cpp:8、worker.cpp:294 | 写盘阻塞：diagnostic_test.cpp:210阻塞同步时提交5000次并报告溢出；旧full暂停消费者业务仍继续。最终板端协议测试重跑受控存储用例。 |
| :17 模式兼容 | 节点:46、:114，五YAML | 同输入三模式：tracker_test:655核对框/分数/状态/调用/像素/引用；ros_video_test:950拒绝运行期变更。client.cpp:30 off不连接。 |
| :25 身份时间 | event.h:103、recorder.cpp:53、节点:906 | 消息早于发布：tracker_test:618保存预测时间和单调耗时，发布另存ROS时刻。该场景支持，其他时间有效位有W6。 |
| :33 相机帧去向 | capture_base:395、frame_source:172、frame_source_ops:33 | 补帧时采集继续：tracker_test:601逐个核对跳过/选择；旧相机原件有采集/入队/跳过身份。驱动缺口单列，归还顺序未改。最终相机组合未重跑。 |
| :41 调用/历史 | hybrid_tracker.cpp:304、:370、:423 | 来源淘汰：tracker_test:339小历史真实HistoryMiss，经中文inspect验证不误称初始化失败。 |
| :49 请求接受拒绝 | yolo_detector.cpp:88、:101 | 在途A仍接受B：tracker_test:550验证在途、待槽、未读快照及拒绝C，原规则不改，锁外提交。 |
| :57 执行与生命周期 | yolo_detector.cpp:115、:156、:225 | 未读覆盖：tracker_test:550实际A被B覆盖。待请求取消：旧pair2/3已接受未执行请求随后Cleaning取消；当前finishRound保存身份、不伪造DetectStart。两场景支持。 |
| :69 阶段/过滤 | yolo_detector.cpp:344、:399 | 低于阈值：原候选循环顺带计数/最高分，旧原件有低分且无通过候选，最新十组保留同类阶段。没有额外推理/遍历，不宣称精确0.42。 |
| :77 仲裁清框 | hybrid_tracker.cpp:487、:593、:713 | 第五次低IoU4→5：tracker_test:388与中文导出支持；高IoU更新失败/真实跳变见W2。 |
| :85 初始化补帧 | hybrid_tracker.cpp:250、:278、nano_tracker.cpp:64 | 部分补帧失败恢复成功：tracker_test:339注入2步失败并保留计数和继续规则；数值错误W3关闭。帧时间有效位有W6。 |
| :93 输出发布 | hybrid_tracker.cpp:19、节点:860、publication.h:8 | 未消费：ROS测试重复时间只发布一次，保存早退/跳过；tracker_test:618异常保持传播，正常返回不宣称订阅收到。 |
| :101 内存缺口 | protocol.h:12、measurement.h:26、recorder.cpp:19 | 普通满仍提交关键：diagnostic_test.cpp:34双区满/环回/序号；:65跨进程50000次可见性。关键容量512/1536=1/3。 |
| :109 同步位置 | storage.cpp:184、:204、worker.cpp:209 | 写成功同步失败：受控分别注入两次同步，旧sync-after-data written/synced分离。失败不推进确认位置。 |
| :117 关键同步/落后 | storage.cpp:194、:201、节点:90 | 100ms拥塞：阻塞同步用例及旧full>12秒未同步年龄，业务继续并报告退化；不承诺打断系统调用或固定掉电损失窗。 |
| :125 尾部恢复 | storage.cpp:235、inspect.cpp:119 | 最后批截断：diagnostic_test.cpp:140遍历小样本截断边界，并检查CRC/版本/缺提交；只读前缀不冒充同步位置。 |
| :133 保护/有界生命周期 | storage.cpp:105、client.cpp:69、:144 | 空间不足：受控总量1字节触发ENOSPC（无空间），旧文件保留；连接500ms、收尾≤2秒检查。不耗尽正式介质。 |
| :141 事实/不足 | inspect.cpp:140、:195 | 缺结束：真实存储/inspect报告未知、去重/缺口及原件未变；未知码按W3验证。 |
| :149 细分复用 | timing.h:8、:62 | 同过程basic/detail：三模式模型调用一致，固定数组无热路径文本/分配/图像复制或额外引用；旧detail38133阶段事件，未观察阶段不能解释为零耗时。 |
| :157 时限/截止 | bounded_run.py:84、:160、ten_round_window.py:50 | 到截止失败：独立后代及真实systemd3秒探针成立，W1关闭；120/150/180/600秒上限保留。 |
| :165 实机开销/隔离 | measurement.h:29、节点:848、analyze_board_acceptance.py:102、:123 | 八组通过：代码要求完整10且≥8，当前没有实机成功八组事实。七组通过：本次复算7/10且7.3未勾选。性能退化：任何门槛超限失败，第7–9组实际调用失败，未制造恰好2%下降。故障隔离：最终受控存储/真实服务，加旧business/worker/full/write/sync原件有支撑。四场景已检查；性能正文仍失败C1。 |

上表tracker_test指 `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp`；diagnostic_test指 `src/diagnostics/tests/diagnostic_test.cpp`；其他组件源码名可由仓库 `src/` 下同名文件定位。

| 部署spec位置、要求 | 当前实现 | 场景支撑与限制 |
| --- | --- | --- |
| :5 完整打包回退 | package_core.py:122、:140、:244，deploy.sh:51，rollback.sh:9 | 缺worker/不匹配：最终真实--check缺件非零、服务快照不变；协议预检在变更服务之前。旧回退：隔离真实脚本恢复旧入口、5个DTD哈希不变。两场景支持，正式升级/新格式间回退未运行。 |
| :17 独立生命周期 | 两service、worker.cpp:306、client.cpp:118 | 主异常：最终真systemd杀业务日志PID不变；杀日志自行恢复、主PID不变。日志不可用：无服务ROS仍发布和500ms准备预算用例，模板仅Wants/After。两场景有组合支撑，启动失败组合分支未专门注入。 |
| :29 CPU范围/入口 | run-diagnostics:6、diagnostic-launch.py:30 | CPU4–7：真直接/服务包装器、实际读回、普通调度、exec参数、失败不exec、RuntimeDirectory支持，W5关闭。 |
| :37 端点/历史保护 | worker.cpp:53、:96、:267，inspect.cpp:86，deploy.sh:72 | 不兼容客户端：24次真实拒绝同时健康会话继续，W4关闭。--check：缺件失败服务不变，所有服务变更在check退出点后，独立日志路径不覆盖录像/bag。两场景支持。 |

## 设计一致性与算法/ROS兼容

| 设计决策 | 独立核对 |
| --- | --- |
| 1 独立服务 | worker单循环、业务无专用日志线程；主仅Wants/After，日志无PartOf/BindsTo；真隔离服务验证不联动终止。 |
| 2 共享内存与唤醒 | 创建方构造/预触及/封印；接收方校验，四个生产者双SPSC环。SPSC为一个写者一个读者；release/acquire规定先填记录再发布、先见位置再读。跨进程可见性/遗漏通知通过；通知不等于落盘。 |
| 3 有界容量与唯一重连 | ≤8MiB、关键1/3、worker两区512KiB批次；5秒重连，pidfd确定旧进程已退出才接管原区。pidfd引用确定实例以避免PID复用。第二消费者拒绝，没有额外共享区累积。 |
| 4 身份时间 | 采集/调用/算法帧/请求/来源独立，数值随原帧，无新图像查找表或引用；耗时单调、ROS原时间不变。W6是字段偏差；墙钟跳变未注入。 |
| 5 基础与细分 | 生命周期/每步补帧不采样，最高分原候选循环顺带取得。W2/W3仅已有数值快照与追加字段，真实分支和模型结果保持。 |
| 6 批量双同步 | 完整写批后释放槽，先fdatasync数据再写Commit并同步，双成功才推进synced；fdatasync向系统请求同步文件数据。目录同步，接管新段不续异常尾。 |
| 7 保护/只读恢复 | 默认64MiB段、1GiB总、5GiB空余，不删除旧记录；只读CRC/长度/版本前缀，位置来自Commit。CRC是内容完整性检查，不证明所有事件曾生成。 |
| 8 短时验收 | 最终十组保留/窗口规则满足，性能C1未满足；W1回收修复成立，30分钟维护守护不代替单项截止。旧细分/相机/故障不是最终产物新运行。 |

当前diff与相邻原实现核对显示：YOLO待槽/结果槽接受和覆盖、历史容量、跟踪计数修改顺序、补帧失败继续、模型处理及返回语义未改变。三模式受控测试进一步核对输出/调用/像素/引用。ROS仍保持 `/fpv/gimbal`、`/fpv/odom`、`/fpv/tracker_trigger` 输入和 `/fpv/target_bbox`、`/fpv/tracker_image`、`/fpv/tracker_state` 输出，四状态值仍0/1/2/3。

`CMakeLists.txt:75`、`src/hybrid_tracker/CMakeLists.txt:32`仅RK3576链接Linux诊断库，通用头只带数值/原子定义，Jetson关闭诊断。这不代替Jetson构建结果。

## 最终产物、原件与独立十组复算

本次使用conda base `/opt/miniconda3/bin/python` 只读解析。`raw-files-manifest.json`的 **667个原件项**哈希/符号链接全部匹配；四个最终归档匹配manifest。`source-final.tar.gz`中 **363个可比源码/脚本文件**与当前逐字节相同，最终脚本包11个Python验收脚本匹配。`R/scripts/ten_round_window.py`保留早期副本，差异为全配置指纹覆盖和armed原子写入；实际 `R/ten-final/ten_round_window.py`与当前/最终脚本包一致，未混用早期脚本。

| 最终程序 | 当前构建、已测板端SHA-256 |
| --- | --- |
| drone_tracker_node | ec0391e9fb5783943ac2b857c18cfe9ca52acf53784a448fa561bef9a76585db |
| rk3576_diagnostic_worker | 780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5 |
| rk3576_diagnostic_inspect | bc00627919b9e60ea3f95b9ab17a9aad11103f6348c8bffd22850543179a8970 |
| diagnostic_tests | 5113b41948c0dc14d922ceaf7f83defe321cbd432b4401c1a4a3b238106a66d2 |
| diagnostic_tracker_tests | b8397c663e521ffe8e2e5cd093274ab84b072e25242ea6aaeeed2f442b96cf04 |

三个安装程序与两个当前构建测试程序均匹配，SSH再次核对板端三个程序也匹配。最终ARM64容器 `./build.sh --test` 原始日志 **47/47、37.28秒**。最后规范整理后的位级一致性连接当前产物与实机证据，没有只相信最初节点或scripts-initial，也没有重复整批构建。

独立解析最新10份DTD原件，记录头/版本/长度/CRC及Commit引用偏移正确，全部完成，共 **241922条事件**，与导出一致。DTD为本组件二进制诊断文件。20个模式窗口全部P99及以上慢样本逐项匹配保存文件。每组off/basic调用均>1500，采样溢出0、健康缺口0，控制组实际回收，无150秒超时。

| 组 | 外部总秒 | off/basic调用样本 | 提交P99 μs | off P99 ms | basic P99 ms | 增量ms | 帧率下降 | 结果 |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 108.609663 | 1696/1733 | 14.146 | 207.485888 | 189.098003 | -18.387885 | -2.294334% | 通过 |
| 2 | 108.444110 | 1569/1618 | 13.731 | 208.128106 | 198.399711 | -9.728395 | -3.109470% | 通过 |
| 3 | 108.752636 | 1581/1639 | 14.377 | 202.601851 | 198.512746 | -4.089105 | -3.908092% | 通过 |
| 4 | 108.554546 | 1583/1613 | 13.754 | 278.613049 | 253.550023 | -25.063026 | -2.033294% | 通过 |
| 5 | 108.525534 | 1547/1639 | 13.685 | 253.561811 | 225.184188 | -28.377623 | -6.110390% | 通过 |
| 6 | 108.572214 | 1582/1696 | 13.685 | 201.659807 | 200.479514 | -1.180293 | -7.229785% | 通过 |
| 7 | 108.519944 | 1537/1614 | 13.847 | 203.680321 | 262.621478 | **58.941157** | -5.107293% | 调用失败 |
| 8 | 108.790143 | 1661/1667 | 14.123 | 201.949763 | 212.979245 | **11.029482** | -0.532480% | 调用失败 |
| 9 | 108.740743 | 1565/1615 | 14.261 | 215.987702 | 221.273653 | **5.285951** | -3.154718% | 调用失败 |
| 10 | 108.484107 | 1538/1593 | 13.386 | 239.585568 | 234.033556 | -5.552012 | -3.408827% | 通过 |

消费帧率=窗口实际消费数/实际秒数；下降=`100×(1-basic_fps/off_fps)`。负值保留，不代表诊断提高性能。真实异步模型会改变来源帧及补帧工作量，未扣除其耗时。全部提交数、慢样本数和边界指标见独立复算JSON。

板卡为 **HZ-RK3576_WuKong Board**，Ubuntu22.04.5 ARM64、`6.1.141-rt36`，不是开发板1/2。RKNN Runtime **2.3.2**、RKNPU驱动 **0.9.8**，系统库指纹 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。模型为既有气球YOLO、FP16 NanoTrack template/search backbone和head；最终acceptance-manifest及各identity包含程序/模型/配置/板卡身份。

固定输入为真实气球图像循环构成的3900帧、60fps、1920×1200 MJPEG视频，SHA-256 `c25aec33208f5dfca315eb9c2c4fb86b04d98883ec5d74b68f5ac7746a669a95`。MJPEG为逐帧独立JPEG编码。各模式预热4秒、测量45秒、顺序交替，模型阈值相同；录像关闭，bag/HDMI开启。bag是ROS消息录制文件。没有将旧真实相机录像组合代替这批视频对照。旧完整十组3/10及更早三组均保留，未拼接。

## 正式恢复与即时只读核查

`R/ten-final/evidence/restore.json` 起点 **21990677309847ns**，恢复完成 **23079724999762ns**，总维护 **1089.047711秒**，小于1800秒，包含恢复。正式PID **27312**、active/running/enabled。protected-before/after及final-state全部正式配置/模型/Runtime一致，current仍 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。HDMI原件card0线程27345、card1正式PID27312持有显示控制权，隔离进程为空。

本次只读SSH再次确认上述服务/PID/current、全部配置模型和Runtime指纹、HDMI27345/27312及无隔离diagnostic/guard/ten_round进程。每条SSH及sudo只读子shell均加载ROS环境。结果保存 [reverification-readonly-current-20261012.txt](reverification-readonly-current-20261012.txt)，文件首行给出查询准确时间。即时恢复核查不代表长期稳定。

## 已执行检查与全部适用未验证限制

本次实际通过 `openspec validate add-rk3576-tracking-diagnostic-process --strict` 和 `git diff --check`。strict validate只检查工件格式/关系，不证明性能。已读最终47项CTest和板端协议/跟踪/Python/入口/服务/截止原件；CTest是CMake测试运行器。没有再次停服或重复全套测试。

适用但未完全验证的内容如下，均未计为通过，也不自动成为新增归档前置实验：

- **最终二进制全部实机组合。** 7.4细分、7.5真实相机录像/bag/HDMI并行及7.6部分存储注入仍是较早二进制证据，没有对最终产物重跑整套组合。最终产物范围是47项自动检查、板端定向协议/跟踪/入口/服务/截止和完整新十组；受控存储不冒充实机全组合。
- **正式完整打包/安装及新格式版本间回退。** 已审实现、安装产物、缺件预检和旧版隔离回退；没有正式新包升级，现网保持旧发布。
- **实际systemd日志启动失败组合分支。** 无服务ROS、连接预算、入口亲和性失败及服务依赖代码有组合支撑，未单独注入systemd启动失败同时观察真实模型业务启动。
- **八组达标的实机成功场景。** 代码准确要求10组且≥8，七组失败实际验证，尚无当前产物八组实机通过事实，不能据静态阈值关闭C1。
- **录像子进程运行期细粒度资源。** 旧相机只有OFF边界业务/日志/bag资源，录像完成与最终扫描仅证明有限退出。旧内核children接口未知，后加脚本未用于该轮，不能声称所有子进程全窗口无增长。
- **墙钟跳变及所有异常并发排列。** 已检查身份、单写单读、可见性、唯一消费者和已测重连；未人为改墙钟或穷尽调度。
- **细分包装阶段精确耗时。** 旧SearchBackbone包装无观察，底层Run/绑定有记录；未知位不表示未执行或零耗时，未补测包装精确成本。
- **静态分析、Jetson构建和硬件回归。** 无可用clang-tidy/clangd配置、Jetson SDK及硬件，RK3576条件链接审查不代替参考平台结果。

既有OSD基线偶发测试失败已在未修改HEAD复现，历史原件保留，本轮没有定位根因。OSD是屏幕图像叠加。最终一次47/47不意味着基线问题消失。

长期稳定、实飞准确率、真实断电、强制重启及介质损坏均未执行。短测不支持前两项结论；后三项不是本变更要求的实验，不擅自添加为归档前提。

## Final Assessment

**1项关键问题、1项警告、0项建议；任务39/40。** 原W1–W5关闭，新W6须修正；C1最终十组7/10仍未达到至少八组。全部要求/场景已映射，但存在确认偏差和上述部分未验证范围，不能宣布全部通过或可归档。本次独立校验已完成，不改变实现、任务框或正式服务状态。
