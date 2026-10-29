# RK3576 跟踪诊断

本组件把检测、历史帧、恢复跟踪和发布调用连接成可核查的记录。业务线程提交固定 256 字节数值事件。独立程序 `rk3576_diagnostic_worker` 收集并保存事件。`rk3576_diagnostic_inspect` 只读检查记录，不加载模型，也不修改原件。

The installed service and `/opt/drone_tracker/bin/run-diagnostics` pin only the collector to CPU 6. The shared CPU policy must allow CPU 6, and the launcher verifies exactly `{6}` before executing the worker with ordinary `SCHED_OTHER` scheduling. Failure stops diagnostic startup without a fallback core or changes to other processes or threads. CPU affinity limits where execution is allowed; CPU 6 remains shared with display work. Direct experimental execution of the bare worker binary retains its inherited affinity.

## 启动模式与身份

`diagnostics.mode` 在启动后固定。`off` 表示关闭，既不连接服务也不创建诊断文件。`basic` 保存完整基础因果链。`detail` 额外保存原有模型处理阶段的耗时。生产模板默认 `basic`，其他 ROS 入口默认 `off`。Jetson 入口只能关闭诊断。运行期修改诊断参数被拒绝。

事件身份由 `run_id`（一次业务进程运行）、`round_id`（一次 ON/OFF 轮次）、`producer_id`（提交线程）、`producer_seq`（该线程的记录尝试序号）组成。尝试序号包含满缓冲时未被接受的事件。普通事件和关键事件可能交错，分析时必须核对实际记录集合及缺口，不能只看最大序号。

帧的 `capture_id` 是轮内实际采集编号。`driver_sequence` 是驱动序号，单独保留以辨认驱动缺口及回绕。视频与参考后端的采集身份默认未知。`update_id` 在每次跟踪调用进入时递增，包括早退。`tracker_frame_id` 是通过输入检查后才递增的原算法帧号。`request_id` 是检测投递尝试编号。来源调用与来源算法帧号保存在 `source_update_id`、`source_tracker_frame_id`。

例如，请求 17 的来源算法帧为 100、来源调用为 105。结果读取事件仍保存请求 17 和来源 100，并使用读取线程的当前 `update_id`。它们不依靠时间接近程度猜测来源。

## 所有权与接入限制

创建方用 `memfd_create` 建立内存文件，以共享映射传递数值。内存文件不等于持久日志。创建方固定大小并禁止收缩，初始化及预触及共享原子对象。接收方先校验本机用户身份、版本、长度、描述符及共享区大小，然后映射；不得重新初始化共享区。

四个预注册生产者分别用于节点、相机、YOLO 和控制路径。每个生产者只有一个写线程。每个线程拥有普通及关键两个 SPSC 环；SPSC 表示单写者、单读者。关键容量占槽容量的三分之一。总业务诊断预分配预算小于 8 MiB。事件不得含指针、字符串、图像或额外帧引用。既有业务锁内只取得数值快照，在锁外提交。不允许热路径文件 I/O（文件读写）、动态分配或文本格式化。

Continuous camera preview uses the Control producer for the persistent frame dispatcher and the Node producer for tracking. Each remains a single writer. Client RunStart/RunEnd use Control before dispatcher startup and after its join. Capture and YOLO retain their separate producers. Tracking rounds update the shared round identity and emit RoundStart/RoundEnd without reopening capture. Dispatcher FrameSelect events describe source selection; replacing a pending tracking task emits FrameSkip/LatestSkip.


满缓冲立即拒收，不覆盖未读槽。共享区累计丢失总数、首末序号和最多 32 段连续范围。范围表耗尽后明确标记详情不完整。关键区同样可能满，因此不能保证故障事件必然留存。异常退出且没有结束事件时，尾部未知，不能当成已知缺口。

`eventfd` 是非阻塞唤醒计数器。通知合并且普通通知节流；通知失败不撤销事件。收集器以 10 ms 为主动轮询目标。这不是存储拥塞时的保证。业务不新增专用日志线程。连接准备最多 500 ms，正常业务退出最多等待 2 秒。既有控制定时器每 5 秒至多重连一次，仅在旧消费者的 `pidfd` 确认退出后接管。`pidfd` 代表确定进程实例，避免进程编号复用造成误认。收集器最多接收两份共享区，输出批次为 512 KiB。

## 帧与时间字段

`frame_time_us` 是帧源时间，单位微秒。`prediction_us` 保存调用前的 `ctx.now_us`，即本次预测采用的时刻。`mono_ns` 是单调时钟纳秒，只用于同机顺序和耗时。`duration_ns` 是实际执行耗时。框始终采用 `x,y,width,height`，即左上角与宽高；有效性由标志明确说明，无效框不能解释为正常零框。

举例：相机继续采集 200–224，跟踪调用耗时 388 ms。期间 201–223 已入队。下一次节点取最新帧时逐条记录 `FrameSkip`（跳过旧帧），最后记录 `FrameSelect` 224。只有实际提交给算法且通过检查的帧进入历史缓存。采集时间、预测时间和调用返回时间不同。`PublishEnd` 的正常返回只证明 ROS 发布调用结束，不能证明订阅端已收到或 bag（ROS 消息录制文件）已写盘。

预热、终止、驱动序号缺口、队列满和节点跳过分别用理由码说明。时间重复或倒退导致调用未消费时，记录 `RepeatedTime` 和 `PublishSkipped`，不会修改原发布规则。

## 决策字段

`Reason` 的关键值为 `NoTarget`（实际无目标）、`LowIou`（框重叠不足）、`NanoUpdateFailed`（本次跟踪更新失败而 IoU 未低于阈值）、`YoloJump`（检测跳变）、`NanoJump`（跟踪跳变）、`NoResultTimeout`（没有新结果超限）、`HistoryMiss`（来源帧已不在缓存）、`InitFailed`（初始化失败）。IoU 是两个框交集面积除以并集面积，用于判断框是否匹配。不能把这些原因统称检测漏检。

`RequestDecision.values[0..5]` 依次表示在途推理、待检测槽、未读结果、在途编号、待检测编号、结果编号。`Complete` 表示该尝试被接受。正在推理时待检测槽可能为空，原规则允许接受下一请求。

`DetectStage.values[0]` 表示阶段：0 区域、1 全图、2 分块。`values[1]` 是实际阶段编号。`counts[0..3]` 是阈值前候选数、通过分数阈值数、通过面积过滤数、NMS 后数。NMS 是抑制重叠重复框的步骤。`highest_score` 仅在原候选循环中计算目标类别阈值前最高分；`ScoreKnown` 区分不存在候选与不可观测，不增加遍历或推理。

`HistoryLookup.values[0..1]` 为当时缓存首末算法帧号。例：请求 17 成功，结果在调用 139 读取；来源 100，缓存 110–139。`HistoryMiss` 表明来源淘汰，不能报告成 NanoTrack 初始化失败。若检测结束事件缺失，则结果未知，不能断言检测从未成功。

`Decision` 的 YOLO 仲裁记录保存实际 IoU 比较框、重置阈值和确认阈值。`counts[0..1]` 是未确认计数前后值，`counts[2..3]` 是检测跳变计数前后值。`values[0..1]` 是状态前后值。带 `DecisionDetails` 有效位的新记录还保存 `values[2]` 本次 NanoTrack 更新返回值，`values[3]` 未确认上限和 `values[4]` 跳变容忍次数。低 IoU 与更新失败同时发生时保留低 IoU 理由，更新返回值仍为 0；IoU 较高且更新失败时用 `NanoUpdateFailed`。第五次低 IoU 清框不属于无目标检测。

带 `NanoCounter` 有效位的 `Decision` 专门记录跟踪跳变。`counts[0..3]` 分别是本次计数修改前值、增加后的值、全部分支执行后的值和容忍次数。没有跳变时第二列保持修改前值，最终值为 0。例如门槛 3 时连续两次跳变记录 0/1/1/3、1/2/2/3；下一次正常记录 2/2/0/3。若已有实际计数 5，下一次跳变并清框记录 5/6/0/3，不能根据阈值伪造为 2/3。记录里的前一框在清框之前取得。`values[1..2]` 保存 KF 与前一框各自跳变判断；KF 是预测运动的卡尔曼滤波器。`score` 和 `threshold` 在此类记录表示位置阈值和尺寸比阈值。

`JumpComparison` 是追加的事件类型，旧类型编号不变。`values[0]` 为 1 时比较 NanoTrack 与当前 KF；为 2 时比较 YOLO 与历史 KF；为 3 时比较 YOLO 与来源帧跟踪框；缺失时保持无效，不替换为当前或前一显示框。`box` 为实际参考框，`other_box` 为本次框；`BoxValid` 为假表示没有可用参考框，原算法采用默认跳变判断。YOLO 比较框已经按图像边界裁剪。`counts[0]` 保存原比较结果，`score` 保存位置阈值，`threshold` 保存尺寸比阈值。所有依据沿同一 `update_id`、`request_id` 和来源身份关联，不重新计算算法结果。

每次初始化有起止及失败阶段。底层失败事件的 `values[1]` 为错误域：1 是 `PreprocessError`（准备模型输入失败），2 是 `ModelError`（模型执行失败）。`values[2]` 为对应枚举数值，仅 `ErrorCodeKnown` 有效位存在时可解释。预处理错误 6 是输入缓存占用，7 是平台准备失败；模型错误 2 是契约不匹配，5 是执行失败。输入无效、异常或缺少可用底层码时错误域/码未知，不用 0 冒充成功。旧日志无该有效位一律未知。JSONL 输出 `error_code_known`，未知域/码为 `null`；CSV 对应列为空。只保存已有数值，不构造额外错误文本。NanoTrack 第一层失败阶段值为 1 输入、2 模板准备、3 模板推理返回失败、4 模板推理异常。每个 `CatchupStep` 保留来源、帧身份、返回标志、框、分数及耗时。`CatchupEnd.counts[0..2]` 为尝试、成功、失败数量。例如 24 步中 2 步失败，原规则仍可整体成功；汇总应为 24/22/2，而不能声称全部步骤成功。

细分阶段 `Timing.values[0]` 对应 `timing::Stage`。`Observed` 表示阶段实际执行，`Complete` 表示已观察的执行均正常完成，`counts[0..1]` 为执行/失败次数。未执行不等同于零耗时。

## 存储与恢复

默认目录为 `/var/lib/drone_tracker/diagnostics/<run_id>`。段上限 64 MiB、总量上限 1 GiB、最低空余 5 GiB；MiB/GiB 是以 1024 为单位的容量。达到保护阈值停止写入，不自动删除或覆盖旧记录。重连总是创建新段，禁止向异常尾部续写。

每条磁盘记录带版本、长度和 CRC（内容完整性校验值）。批次完整写入后才释放槽。`generated` 是尝试生成位置；`written` 是完整写入最大位置；`synced` 是双阶段同步确认位置。这三个位置均需联合真实事件集合与缺口核对。写入不是落盘。

默认每 100 ms 请求同步，可选 200 ms。关键事件促使收集器在可调度且存储可用时尽快同步。先同步数据，再写引用数据边界的提交记录，最后同步提交记录。两次均成功才公布确认位置。创建文件涉及的父目录也需同步。错误不推进成功位置。持续输入时最旧未同步记录年龄超过 500 ms 标记退化。实际同步系统调用阻塞时不能承诺 10 ms 轮询或 100 ms 最大损失窗。

突然断电可能丢失内存槽或操作系统尚未同步缓存中的记录。恢复只接受完整可校验前缀，不保证最后事件留存。离线提交位置仅来自恢复到的提交记录，不能从文件大小补出同步成功。此能力未宣称真实断电已验证。

`Health.values[0..6]` 依次为已生成、已写入、已确认同步、累计缺口、最旧未同步年龄、最近同步耗时和错误码。`Observed` 表示同步确认已知。业务控制定时器每秒最多输出一份包含全部生产者位置的摘要；它不逐条打印事件。收集器的健康记录也最多每秒一次，收尾另存最终快照。

## 只读排查

先备份原目录，再执行下面的命令。导出必须使用不存在的新目录，禁止在原日志目录内导出。

```bash
/opt/drone_tracker/current/lib/drone_tracker/rk3576_diagnostic_inspect \
  /var/lib/drone_tracker/diagnostics/123456 --output /tmp/diagnostic-export-123456
```

结果包括中文 `timeline.txt`、逐行 JSON 记录 `events.jsonl`、按列组织的 `events.csv` 和 `identities.txt` 身份清单。未知身份、重复事件、已知缺口、缺少结束标志及损坏尾部均显式报告。原件不修改。请求开始可见但结束缺失时只能报告“结果未知”，不能计为检测失败或断言推理仍在执行。

受控检查入口为 `diagnostic_tests`。外部截止入口 `tests/bounded_run.py` 将准备、预热、测量和收尾全部计入预算；超时返回失败，不延长窗口。性能或稳定性单次最多 600 秒，相机最多 180 秒，故障实例最多 120 秒。生产计划采用 150 秒窗口。Current builds use `./build.sh --test` in the default x86_64 compiler image to produce ARM64 programs. `--backend arm64` selects the preserved fallback. Native portable process checks and ARM/QEMU checks have separate roles; see `../../dockerfiles/README.amd64.md`. Results from either image prove only the checks actually run and do not replace production-board model, camera or performance acceptance.
## 实机验收的独立采样入口

`DRONE_TRACKER_ACCEPTANCE_DIRECTORY` 是验收脚本专用环境变量。未设置时不启用测量数组。设置时须指向外部控制器刚创建、没有既有输出的新目录。它在模型运行前分配并预触及定长数组。业务停止、节点销毁后才导出 CSV，不在跟踪热路径写文件。

`measurement.h` 直接包围 `Recorder::submit()` 测量，包括通知系统调用。提交样本保存在独立的每生产者数组中，不把测量再次提交给被测日志。节点在 `tracker_->update()` 两侧采样完整调用耗时及消费标志。关闭、基础与细分对照都启用相同的调用采样。测量本身的时钟读取与数组写入仍有成本，完整调用对照包含该成本；不能据此宣称零开销。

每个生产者最多保存 65536 个提交样本，调用数组最多保存 8192 个样本。数组、业务共享区域和临时事件总量通过静态检查限制在 8 MiB 内。超出容量会在 `sampling-status.txt` 累计溢出，验收不能使用已溢出的数组声称覆盖全部运行。提交序号和轮次的测量副本为 32 位，仅用于这些有界短测；正式事件协议仍使用原 64 位身份。

例如，控制器以 `off` 和 `basic` 各运行 45 秒，并记录两个单调时间边界。`calls.csv` 保存每次调用的起点、耗时、轮次、帧时间与消费标志。`submissions.csv` 保存真实提交区间的起点、耗时、轮次、生产者和序号。分析器选择起点落在各自完整测量窗口内的全部样本，按排序后第 `ceil(0.99×N)` 个样本计算 P99，同时保存所有不低于 P99 的慢样本。关闭模式不创建结构化诊断日志；此处 CSV 是显式启用的验收测量输出。

任务 7.3 执行完整十组成对对照，每组总时间不超过 150 秒。至少八组各自同时满足提交 P99 ≤20 μs、调用 P99 增量 ≤0.2 ms、消费帧率下降 ≤1%及健康无缺口门槛才完成。所有失败组和原始数据均保留。可复用入口、真实业务环境预检及独立恢复步骤见 [十组验收说明](tests/ten_round_acceptance.md)。修改业务二进制后，应对新产物重新取得完整批次证据。

`tests/board_acceptance.py` 为指定生产板创建隔离实例。`bounded_run.py` 提供每项测试的外部截止。`maintenance_window.py` 只在用户明确批准临时停服后执行。板端独立 `restore_guard.py` 读取首次 `armed` 的单调起点，1720 秒后开始有限清理，并在总 1800 秒截止内尝试恢复原正式服务。失败重试不能重置该起点。恢复守护不依赖 SSH 连接，清理异常仍会进入正式服务恢复路径。


## 外部截止与实际入口验证

`tests/bounded_run.py` 在 Linux 启用 `PR_SET_CHILD_SUBREAPER`，使控制器退出后仍存活的后代由外部截止进程接管。新 session 不会逃出这条父子关系。外部进程按 `/proc` 的父子关系与进程启动时间追踪，并在同一总预算内先 TERM、后 KILL、回收退出进程。控制器返回码不能代替存活检查。无法实际回收时 `resources_recovered=false` 且失败。不能启用子收割者的平台会在启动测试前拒绝执行。

由 systemd 启动的实例不属于调用控制器的后代，必须在启动前把独有的 `drone-diag-*.service` 名写入 `DRONE_DIAGNOSTIC_BOUNDED_UNITS` 指定的登记文件。截止进程会停止服务以阻止自动重启，并对登记控制组有限终止、读回成员。登记项无法读取时结果未知并失败。十轮入口另有每组独立控制组；30 分钟恢复守护只负责正式服务恢复，不能代替单组 120/150 秒截止。

`tests/diagnostic_entry_fixture.py` 为测试复制已安装入口，只重定向固定安装、运行及存储路径。每份副本保存原脚本和副本指纹及路径映射。`entry_service_probe.py` 实际执行 `run-diagnostics → cpu-policy.py → diagnostic-launch.py → exec`；替身 worker 保存参数、实际 CPU 与普通调度。服务入口保留 systemd 的 `RuntimeDirectory` 创建和回收，并在入口执行前检查目录已经存在。亲和性错误注入验证非零退出且未执行 worker。该隔离证据不更改正式入口。
