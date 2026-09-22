## Context

RK3576 生产代码里,`rk3576::RknnNanoTrackBackend`(NanoTrackV3 单目标跟踪模型组合的生产后端)让"搜索 Backbone"(从当前帧搜索区域图像提取特征的模型)和"Head"(比较模板特征与搜索特征、输出分类和位置的模型)之间通过 CPU 主机内存交接特征:搜索 Backbone 执行完成后,`rk3576::RknnSession::execute()` 把设备输出复制并转换为 Float32(32 位浮点)主机数据,再重新提交给 Head。这次 CPU 往返在每一帧跟踪时都会发生。

已有的独立性能对照程序(`tests/nanotracev3_performance_benchmarks/search_direct/`,对应 `nanotrackv3-search-direct-benchmark` 能力)已经在真实 RK3576 板卡上验证:把这次交接改成"搜索 Backbone 输出直接以设备原生 FP16(16 位浮点)布局留在共享设备内存里,Head 直接绑定读取同一块内存,不经过 CPU",在数值上与现状完全一致,且端到端延迟降低约 8%~11%。该程序为此实现了一个与生产 `RknnSession` 并列的测试专用类 `search_direct::DirectSession`,包含 native 属性查询、部分提交、显式绑定、共享内存分配/导入/释放等生产会话目前没有的原子操作。

这次改造的目标是把这套已验证的机制从独立测试程序迁移进生产代码,而不是重新发明或重新验证它。选定的落地方式是**扩展生产 `RknnSession`**,而不是让 `DirectSession` 和 `RknnSession` 长期并存——后者会导致两套几乎相同的 RKNN 生命周期管理代码同时存在,增加维护和排障成本。

## Goals / Non-Goals

**Goals:**
- 在不改变 `RknnSession::execute()` 现有行为(YOLO 目标检测路径和 NanoTrack 模板/Head 路径都还在用)的前提下,给它新增支持共享设备内存直连所需的原子操作。
- 让 `RknnNanoTrackBackend` 在装配时就能判断"搜索特征能否直连",能则直连,不能则加载失败,不存在中间状态。
- 保持 `model::NanoTrackBackend` 对外接口(`setTemplate`/`search`/`reset`)、`NanoTracker` 的跟踪算法、ROS 话题和参数含义完全不变。

**Non-Goals:**
- 不改造 YOLO 路径,`RknnRuntime`/`RknnYoloBackend` 不需要感知这次改造。
- 不重新验证"直连在数值上是否等价"这件事本身——这是 `nanotrackv3-search-direct-benchmark` 已经完成并留下证据的工作。
- 不探索"绑定能否跨帧复用、从而进一步降低每帧重绑定开销"这一独立的优化方向;本次改造原样接受 benchmark 已经测得、且已经把重绑定开销计入的收益数字。
- 不给生产后端增加运行时开关在"直连"和"CPU 交接"之间切换。

## Decisions

### D1: 扩展 `RknnSession`,不新增并列的会话类

**选择**:把 `search_direct::DirectSession` 里的原子操作(native 属性查询、部分提交、显式绑定、仅确认完成、共享内存分配/导入/释放)回迁为 `RknnSession` 的新增公开方法,`execute()` 的实现改写为"提交全部输入 + 运行 + 取回全部输出"这三个新原子操作的组合,对外行为不变。

**原因**:通过代码调用面核查确认,`RknnSession` 是 `final` 类,不实现任何多态接口,生产中只有 `RknnRuntime`(YOLO)、`RknnNanoTrackBackend`(NanoTrack)、两个契约校验函数(`validateYoloSession`/`validateNanoTrackSessions`)这几个调用者,改造面可控。

**拒绝的替代方案**:保留 `DirectSession` 和 `RknnSession` 长期并存。拒绝原因是两者的加载、属性查询、销毁、失败处理逻辑几乎完全重复(对照两份源码,`initialize()`/`invalidate()`/`confirmStopped()`/输出复制逻辑基本相同),长期维护两份几乎相同的 RKNN 生命周期代码会持续产生"改一处忘改另一处"的风险。

**后果**:`RknnSession` 的职责边界从"只做同步整体执行"扩展为"同步整体执行 + 可选的零拷贝原子操作",类会变大,但生产只有一套会话实现。

### D2: 共享内存 API 作为独立可选扩展结构体,不并入现有 `RknnApi`

**选择**:新增 `RknnSharedMemoryApi` 结构体,只包含 `rknn_create_mem`/`rknn_create_mem_from_fd`/`rknn_destroy_mem`/`rknn_set_io_mem`/`rknn_mem_sync` 五个函数指针。`RknnSession::load()` 新增一个可选重载,接受 `(path, RknnApi, RknnSharedMemoryApi)`;原有的 `(path, RknnApi)` 重载保留,内部把共享内存 API 置空。

**原因**:YOLO 路径(`RknnRuntime`)永远不会用到共享内存操作。如果把这 5 个函数塞进现有 `RknnApi`,YOLO 相关的测试 mock(例如 `rknn_yolo_backend_test.cpp` 里构造的假 API)也必须多填 5 个永远不会被调用的字段,污染其契约面。

**拒绝的替代方案**:直接扩充 `RknnApi`。拒绝原因如上。

**后果**:`RknnSession` 内部要维护"是否提供了共享内存扩展"这个状态,共享内存相关方法在缺少扩展时返回明确的能力缺失诊断(对应 delta spec 里"扩展能力缺失时不影响既有执行路径"这条需求),而不是让调用方误以为可以用。

### D3: 新增原子操作按"能力分层"设计,不是零散加方法

**选择**:新增方法划分为三层,和 delta spec `rk3576-model-session` 的四个 ADDED 需求一一对应:

1. **原生属性层**:`nativeInputs()`/`nativeOutputs()`,只读查询,任何会话都可调用(不依赖共享内存扩展),返回值与既有 `inputs()`/`outputs()` 并存、不互相替代。
2. **原子执行层**:`submit(vector<SessionInput>, allow_partial)`(允许只提交部分输入)、`bind(rknn_tensor_attr, rknn_tensor_mem*)`、`run()`、`confirmCompleted()`(确认完成但不取回输出)、`fetch()`(既有取回逻辑抽出来的独立方法)。`execute()` 保留,内部等价于 `submit(all) + run() + fetch()`。
3. **共享内存层**:`allocateShared(bytes)`/`importShared(rknn_tensor_mem&)`/`releaseShared(rknn_tensor_mem*)`/`syncFromDevice(...)`(仅供诊断读取使用,不用于正式路径)。这一层的方法只有在 `RknnSharedMemoryApi` 提供完整时才可调用。

**原因**:这个分层直接对应 delta spec 里的四条独立可测试需求,方便逐条写单元测试,也让 `RknnNanoTrackBackend` 只需要按需组合这几个原子操作,不用理解 RKNN SDK 细节。

**拒绝的替代方案**:只加一个大而全的 `executeDirect(...)` 方法,内部硬编码"搜索特征走共享内存,其余走普通提交"的固定顺序。拒绝原因是这会把 NanoTrack 特有的调用顺序(先提交搜索图像、再绑定输出、再运行、模板普通提交后再绑定 Head 的搜索输入)硬编码进平台无关的会话层,而这个顺序其实是 NanoTrack 后端的业务逻辑,不该下沉到通用会话。

### D4: 装配时一次性探测直连契约,不做运行时回退开关

**选择**:`RknnNanoTrackBackend::load()` 在三个模型都加载成功后,立即查询搜索 Backbone 输出和 Head 搜索输入的原生属性,用类似 benchmark 里 `matchSharedFeature()` 的规则做兼容性判断:

```
装配流程(输入: 三个模型路径 -> 输出: 可用后端或失败诊断)
1. 加载 template/search/head 三个会话(既有 RknnSession::load)
2. 校验三模型逻辑契约(既有 validateNanoTrackSessions,不变)
3. 查询 search 输出与 head 搜索输入的原生属性(新增 nativeOutputs()/nativeInputs())
4. 判断原生形状、FP16 类型、通道分组、有效跨度、安全容量是否一致
   - 一致 -> 分配共享内存、导入到 head、记录直连身份字段,返回可用后端
   - 不一致 -> 返回失败诊断,列出具体差异,不返回后端实例
```

**原因**:delta spec `rk3576-nanotrack-backend` 明确要求"不满足直连契约时创建后端 SHALL 失败,MUST NOT 静默退回既有的 CPU 特征交接方式"。这不是本次新发明的哲学,而是 benchmark spec 里"原生属性不支持...MUST NOT 静默回退为 CPU 交接"这条已有原则在生产语境下的延伸——静默回退会让"直连是否生效"变成一个不透明的、要看运行环境才知道的黑盒,排障成本远高于直接拒绝加载。

**拒绝的替代方案**:
- 保留运行时开关,在直连失败时自动改用当前 CPU 交接逻辑。拒绝原因是这需要在 `RknnNanoTrackBackend` 内部同时维护两条完整的执行路径(共享内存版 + CPU 复制版),违反"只改搜索特征交接方式"的最小改动原则,两条路径都需要独立测试。
- 提供两个实现 `model::NanoTrackBackend` 接口的后端类,由工厂函数在装配时选择。拒绝原因是这仍然是两套长期共存的实现,和已经在会话层否决的"并行实现"是同一个问题在另一层重演。

**后果**:如果某个 RK3576 目标模型导出后的原生布局不满足当前契约(比如换了转换工具版本),整个 NanoTrack 后端会直接加载失败,而不是退化为可用但更慢的路径。这个代价是刻意接受的——详见"风险/权衡"。

### D5: 失败语义复用 `model-backends` 已批准的边界,不新造分类

**选择**:直连路径上的失败分两类:

- **设备执行提交之前的失败**(装配期契约不匹配、绑定被 SDK 拒绝且未提交 `run()`)→ 映射为现有的"单帧搜索失败"行为:`NanoTracker::update()` 打日志、返回 `-1.0f`、跟踪状态转为丢失,继续处理下一帧,不终止 ROS 节点。
- **设备执行已提交但无法确认停止、或共享内存释放本身失败**→ 沿用 `model-backends` 里已经批准的规则:"设备提交后无法确认设备停止访问存储时 SHALL 输出致命诊断并立即终止进程",调用既有的 `model::terminateDeviceFailure()`。

**原因**:这条边界不是本次改造发明的新哲学,而是 `model-backends` 和 `model-execution` 两个已批准 spec 里早就存在的通用规则,`RknnSession::confirmStopped()`/`invalidate()` 已经在用它。直连路径的绑定失败、释放失败本质上和现有的 `inputs_set`/`outputs_get` 失败是同一类问题("设备状态是否可信"),应该复用同一套判断标准,而不是给"共享内存相关的失败"单独发明一套更严格(或更宽松)的规则。

**拒绝的替代方案**:完全照搬 benchmark `DirectSession` 的"任何绑定/释放失败都终止进程"。拒绝原因是 benchmark 是一次性对照测试程序,追求"宁可整个测试挂掉也不能返回假数据";生产是长跑 ROS 节点,一次可恢复的绑定瞬时失败(设备已确认停止,只是这次没能建立绑定)不应该拖垮整条跟踪链路。

### D6: 共享内存生命周期归属"后端实例",不归属"模板"或"每一帧"

**选择**:共享内存的分配(`allocateShared`)与导入(`importShared`)在 `RknnNanoTrackBackend::load()` 成功装配三个会话后一次性完成,绑定关系在后端整个生命周期内固定;`reset()` 只清空 `template_feature_`,不触碰共享内存分配和导入句柄。

**原因**:共享内存的物理布局(大小、原生属性)只取决于搜索 Backbone 和 Head 这两个模型文件本身,和"当前在跟踪哪个目标"无关。`reset()` 对应的是"切换跟踪目标"这个业务动作,和"这两个模型之间怎么交接特征"是两个不同的生命周期维度,不应该被同一个方法触碰。

**拒绝的替代方案**:每次 `setTemplate()` 时重新分配共享内存。拒绝原因是没有必要——共享内存布局不随目标切换而变,重新分配只会引入更多失败点,不带来任何好处。

### D7: 接受每帧重新绑定的固定开销,不在本次改造内二次优化

**选择**:直接沿用 benchmark 已验证的结论——"绑定不会跨普通提交保持有效,必须每帧重新建立"——把这部分开销原样保留在生产实现里,不额外调研"绑定是否可以复用"。

**原因**:benchmark 报告(`.docker-artifacts/log/board2-nanotrackv3-20260921/nanotrackv3-performance-20260921/compare.md`)里的收益数字(约 7.6%~11%)是已经把每帧重绑定开销计入之后测得的净收益,不是理论上限。绑定复用是一个独立的、有自己收益边界的优化课题,混进这次改造会扩大范围、推迟交付,且目前没有证据支持"绑定可以复用"这个假设成立。

**后果**:如果将来要验证绑定复用能否进一步提升性能,应该开一个新的、同样独立的 benchmark change,不在这次生产化改造的范围内。

### D8: 生产验收分两层,不重复 benchmark 的独立参照验证

**选择**:
1. **契约/单元层**(编译期可跑,不需要板卡):在 `src/platform/rk3576/tests/` 下扩展针对 `RknnSession` 新增原子操作和 `RknnNanoTrackBackend` 直连路径的 mock 测试——绑定失败、原生属性不兼容、`importShared` 失败等场景下返回值正确。
2. **实机对比层**(需要板卡,一次性但要留痕):用现有真实回放录像跑一遍改造后的生产 `RknnNanoTrackBackend`,把逐帧框和分数与 benchmark 已经产出的 `search_direct` 侧数值结果对比,容差沿用 `nanotrackv3-search-direct-benchmark` spec 里的 1e-4 像素 / 1e-6 分数标准。

**原因**:"直连在数值上等价于 CPU 交接"这件事本身已经由 `nanotrackv3-search-direct-benchmark` 的独立参照验证完成,不需要重复;这次改造只需要验证"胶水代码"(`RknnNanoTrackBackend` 如何调用扩展后的 `RknnSession`)本身没有引入新 bug。

**拒绝的替代方案**:要求这次改造重新走一遍完整的 benchmark 级独立参照 + 多轮 A/B/A 交替验证。拒绝原因是重复劳动——benchmark 的验收标准存在的意义就是让下游改造可以直接引用其结论。

## Risks / Trade-offs

- **[风险] 装配时拒绝直连契约会让某些模型文件的 NanoTrack 后端完全无法加载,而不是退化为可用但更慢** → **权衡**:这是刻意接受的代价(见 D4),因为静默降级带来的排障成本更高;缓解方式是失败诊断必须清楚列出原生属性的具体差异,而不是一句笼统的"不兼容"。
- **[风险] `RknnSession` 新增原子操作后类的职责变宽,增加理解成本** → **缓解**:通过 D3 的分层设计和 delta spec 里独立可测试的四条需求,把新增能力限制在"原生属性查询/部分提交与绑定/仅确认完成/共享内存生命周期"这四个边界清晰的组,而不是无结构地堆方法。
- **[风险] 只有 board2 一块板卡跑出的 benchmark 数据,尚未在其他 RK3576 硬件批次上复现** → **权衡**:本次改造把这一点列为已知限制,不阻塞生产化,但生产验收报告需要明确标注所用板卡编号,不能把单板结果泛化为"所有 RK3576 硬件都有此收益"。
- **[风险] 共享内存释放失败会终止整个 ROS 节点(而非单帧降级)** → **权衡**:这是复用 `model-backends` 已批准的致命诊断策略(D5),不是本次改造引入的新风险;它反映了"内存安全边界一旦不确定就不能继续跑"这一更高优先级的安全约束。

## Migration Plan

1. 扩展 `RknnApi`/新增 `RknnSharedMemoryApi`,给 `RknnSession` 增加 D3 描述的三层原子操作,`execute()` 保持行为不变;单独提交并跑通现有 `rknn_session_test.cpp` 和 YOLO 测试,确认零回归。
2. 给 `rknn_nanotrack_contract.{h,cpp}` 增加原生属性兼容性校验函数(逻辑参照 benchmark 的 `matchSharedFeature()`)。
3. 改造 `RknnNanoTrackBackend::load()`,加入 D4 描述的装配期探测流程;`search()` 改为按 D3/D5 描述的顺序调用新原子操作。
4. 补齐单元测试(D8 第一层),覆盖 delta spec 里全部新增/修改需求对应的场景。
5. 在 RK3576 板卡上跑一遍实机对比(D8 第二层),产出对比报告作为验收证据。
6. **回滚策略**:因为 `execute()` 行为未变、旧的 CPU 交接代码路径在这次改造中被直连路径替换而非并存,如果实机验收发现直连路径有问题,回滚方式是回退 `RknnNanoTrackBackend` 相关的两次提交(装配探测 + `search()` 改造),`RknnSession` 新增的原子操作可以保留(它们是纯新增,不影响回滚)。

## Open Questions

- Jetson 侧对 VPI(Jetson 的图像处理库)资源失败的处理策略是否与本次复用的"设备停止确认失败即终止进程"边界完全一致?这不影响本次 RK3576 改造的 specs、方案或任务拆分,但值得后续单独核实,以确认两个平台在"资源安全失败"上的语义确实对齐。
