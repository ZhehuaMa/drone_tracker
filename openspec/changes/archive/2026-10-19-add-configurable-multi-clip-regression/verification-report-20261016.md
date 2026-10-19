# 实现核验报告：add-configurable-multi-clip-regression

核验日期：2026-10-16。工作树：`/home/mzh/nickluo/drone_tracker-worktrees/configurable-video-regression`。分支：`codex/configurable-video-regression`。

本报告由用户流程第三项的独立子代理完成，使用 `openspec-verify-change` 技能。核验包括实现完整性、需求与场景正确性、设计及代码模式一致性。本次只修改核验记录和任务 6.3 的状态，没有修复代码、重新运行录像、访问开发板、提交、推送、同步主规格或归档。

## 结论与计分表

未发现 CRITICAL（必须修复）、WARNING（建议修复）或 SUGGESTION（可选改进）问题。当前授权范围内的七项核验检查全部完成。32/32 项任务完成；15/15 条新增或修改需求找到实现；34/34 个场景已逐项检查代码和测试证据。没有删除或重命名需求。

| 维度 | 结果 |
| --- | --- |
| 完整性 | Task Completion：32/32；Spec Coverage：15/15；无不可读取的任务文件 |
| 正确性 | Requirement Implementation Mapping：15/15；Scenario Coverage：34/34；没有发现偏离当前需求的实现 |
| 一致性 | Design Adherence：8 项设计决定均有对应实现；Code Pattern Consistency：符合既有测试目录、平台边界及资源管理模式 |

这里的场景覆盖表示已检查实现路径及相应测试证据，不表示每个示例参数都进行了新的生产板运行。真实板端验收仅覆盖下文明确列出的三个 test-03 用例。测试通过不等于跟踪准确率或性能门槛通过。

## 核验依据与有效范围

已执行 `openspec list --json`，确认项目根为本工作树。没有选择外部 store。`status` 返回 schema 为 `spec-driven`，四种工件均存在，`actionContext` 为本仓库。`instructions apply` 的 `contextFiles` 指向本 change 的 `proposal.md`、`design.md`、`specs/rk3576-tracking-regression/spec.md` 及 `tasks.md`；已完整读取。任务跟踪已配置，无不可读取文件。开始时为 31/32，唯一未完成项是本次正在执行的 6.3；完成核验及本报告后才勾选它。

用户于 2026-10-16 明确豁免 test-02。本次运行验收要求为三个 test-03 区间，各三轮新参考、三轮新候选，共十八轮。准备预算 P=3600 秒，收尾预算 C=10 秒。供帧最大迟到限制仍为 50,000 微秒，即 50 毫秒。四项输入配置和整段能力继续保留，但 test-02 不在本次重新准备或回放范围内，不能标为本次实测通过。上述范围已写入 proposal、design、delta spec 及任务 6.2，属于明确授权的例外，不是遗漏实现。

已审阅 Git 已跟踪差异和新增实现文件。功能变更限于 `tests/tracking_regression/` 及 `package.xml` 的测试依赖声明。生产算法、ROS 话题、跟踪状态值、生产参数和部署入口没有代码修改。`python3-yaml` 的本机及目标环境检查已进入 CMake；普通构建没有联网安装依赖。

实际受测源码内容校验值为 `f99c20fb42eedb6c8109e715ab1481650687f2cb3d2903fd09d9e38c0c9a2966`。核验时重新计算当前工作树的源文件清单，得到相同值。回归程序校验值为 `12ad604c6c57d35093977759dfc710f5e0f5f0b5ec809d457b623d404c1780d9`。校验值为 SHA-256，即按文件内容计算的身份；未提交源码不能仅由 HEAD 提交号代表。

## 需求到实现映射

下表编号供后续场景表引用。路径均相对于本工作树根；冒号后为核验时的起始行号。

| 编号／类别 | 需求 | 实现证据 | 验证证据 |
| --- | --- | --- | --- |
| R1／ADDED | 单配置文件驱动回归套件 | `tests/tracking_regression/run_suite.py:199` 只接受配置路径；`configuration.py:105` 完整读取；旧业务入口给出迁移提示 | `test_configuration.py` 的命令行拒绝及两用例快照测试；`test_suite.py` 的运行计划测试 |
| R2／ADDED | 配置校验及快照 | `configuration.py:17` 拒绝重复键，`:65` 精确微秒，`:105` 校验全部用例，`:278` 保存快照；`run_suite.py:96` 先预检再创建输出 | `ConfigurationTests.test_rejections_point_to_fields`、`test_second_invalid_case_never_starts_first`、`test_snapshot_survives_external_change_and_no_cli_override` |
| R3／ADDED | 省略帧选择区间即选择全部帧 | `configuration.py:79` 区分省略、null 和 end；`prepare_fixture.py:168` 核对尾部，`:220` 解析半开区间，`:238` 解码全部源帧 | `VariableFixtureTests` 的边界、整段、end、空选择和真实合成像素测试；180 秒截止及窗口测试 |
| R4／ADDED | 指定外场四用例配置 | `configs/wlcb-overlay-four-cases.yaml` 保留四身份及精确配套路径；独立 fixture、进程、基线按身份隔离 | 历史四输入来源核验及逐帧清单；新十八轮三个区间均合格；test-02 明确豁免 |
| R5／ADDED | 多用例串行隔离 | `run_suite.py:96` 按用例／轮次循环；`run_batch.py:186` 回收进程树，`:311` 外部监督；回收失败列出未执行项 | `SuiteTests` 的普通失败继续、回收失败停止及全部轮次保留；`node_round_test.cpp` 的节点状态／帧释放检查 |
| R6／ADDED | 套件报告逐用例保留结论 | `run_suite.py:41` 同时保存操作和性能状态，`:96` 按 case_id 匹配；`compare.py:136` 保留不可比原因 | 缺少／额外用例测试，三项独立正式比较；无门槛保持 not_set，不跨录像平均 |
| R7／ADDED | 测试供帧线程放置可验证 | `identity.py:61` 检查允许／在线 CPU；`realtime_source.cpp:140` 等待线程就绪，`:191` 设置与回读；`collect.py:101` 检查时钟前证明 | `test_supplier_cpu.py`、C++ `supplierCpuIsolation`、CPU 改变不可比测试；固定竞争诊断及十八轮 CPU 4 证据 |
| R8／MODIFIED | 固定二十秒用例及帧选择 | `prepare_fixture.py:17` 按整数 pts 选帧；`:220` 每项范围；`verify_fixture.py:14` 保留旧参考专用断言 | 固定首末偏移／原帧号断言保留；不同范围、整段和尾段测试；真实三个区间帧映射 |
| R9／MODIFIED | 素材不可变及像素身份可验证 | `prepare_fixture.py:238` 保存并复核源校验、FFV1 亮度像素；`verify_fixture.py:14` 验证文件和帧；`main.cpp:54` 校验证书后才准备及加载模型 | 改变像素／源文件测试、32×24 原像素往返、C++ 缓存像素错误及预检拒绝测试 |
| R10／MODIFIED | 原帧间隔及姿态使用同一时间基准 | `prepare_fixture.py:17` 校验 epoch，`:71` 选择最近初始姿态；`realtime_source.cpp:330` 到期后供给帧和姿态 | 非零首帧、原间隔和姿态时间测试；缺初始姿态及重复／倒退时间拒绝测试 |
| R11／MODIFIED | 每轮从片段开头及清空状态开始 | 每轮新 node/worker、新 frame source 和模型；`metrics.py:192` 使用本项窗口；`verify_fixture.py:95` 保留参考 core | 节点两轮重建及资源释放测试；五秒／180 秒窗口没有继承 core；正式十八个独立进程身份 |
| R12／MODIFIED | 全部帧准备就绪后开始回放 | `realtime_source.cpp:121` 全量缓存及逐帧实际像素验证；`main.cpp` 准备成功后才构造节点／模型；`start` 最后建立时钟 | C++ 分配失败、真实 OS 内存限制、解码错误、像素错误、可变缓存和慢消费；正式每轮完整缓存计数／字节 |
| R13／MODIFIED | 基线显式冻结且候选不覆盖参考 | `baseline.py:208` 每侧三轮完整合格检查，`:233` 新目录冻结，`:253` 重读内容校验；`compare.py:136` 拒绝复用参考 | 不足三轮／重复身份／混合用例／篡改／已存在输出测试；三份正式冻结基线比较后不变 |
| R14／MODIFIED | 不同版本先检查可比条件 | `identity.py:75` 归一化本项内容，`run_batch.py:60` 记录板卡／依赖／CPU／负载；`compare.py:136` 检查集合差异 | 模型、时序、区间、窗口、CPU、版本和依赖变化测试；十八轮固定相同条件，三项 comparable |
| R15／MODIFIED | 目标平台与总截止时间明确 | `run_batch.py:60` 限定实际板型、系统 Runtime／驱动；`:253` 记录 P+ceil(D)+C；`:261` 与 `:311` 双层有界监督 | 控制器挂起／准备超时／收尾失败／脱离会话回收；45 秒准备加 20／180 秒回放；正式板卡十八轮截止证据 |

R7 的省略值保持继承行为；非法请求及回读失败拒绝回放。设置只作用于供帧线程，没有给整个跟踪进程更换 CPU、提高调度优先级或修改生产策略。记录供给时刻的位置仍在像素副本创建及队列锁获取之后。队列帧仍持有独立像素，不借用缓存寿命。

## 逐场景覆盖

表中的“通过”指核验通过。便携测试包括合成数据或可控时钟，不冒充现场实测。历史 test-02 输入核验只读复用已有证据，没有新解码或回放。

| 场景 | 需求 | 代码／测试／证据 | 结果 |
| --- | --- | --- | --- |
| 一份配置列出两段录像 | R1 | `test_suite.py:test_run_keeps_all_rounds_and_uses_case_only_snapshots` | 通过 |
| 命令行覆盖被拒绝 | R1 | `test_configuration.py:test_snapshot_survives_external_change_and_no_cli_override` | 通过 |
| 后续用例配置无效 | R2 | `test_second_invalid_case_never_starts_first`，确认首项尚未运行 | 通过 |
| 执行期间原配置被修改 | R2 | `test_snapshot_survives_external_change_and_no_cli_override` | 通过 |
| 显式半开区间 | R3 | `test_resolved_selection_boundaries_whole_and_end` 包含精确 55／75 秒边界 | 通过 |
| 整段录像超过二十秒或一百二十秒 | R3 | `resolve_selection` 的 180 秒全帧计划；控制器 45+180 秒可控时钟测试；180 秒指标窗口 | 通过 |
| 每项独立处理省略值 | R3 | 每项新建配置对象；整段／显式区间解析测试及两用例运行计划 | 通过 |
| 从指定起点到录像结束 | R3 | end 与显式数值终点等价／空选择拒绝测试；正式尾段末帧 12478 | 通过 |
| test-02 全帧用例 | R4 | 四项配置省略区间；历史 fixture 4341 帧、原帧 0–4340；本次板端运行用户豁免 | 通过配置及既有输入证据；本次实测不适用 |
| test-03 第一段 | R4 | 新六轮各 900 帧，20≤pts<35，原帧 1201–2100 | 通过 |
| test-03 第二段 | R4 | 新六轮各 1801 帧，100≤pts<130，原帧 6002–7802 | 通过 |
| test-03 第三段 | R4 | 新六轮各 2575 帧，165≤pts 至正常结束，原帧 9904–12478 | 通过 |
| 两段录像不继承状态 | R5 | 独立子进程／每轮 factory；`node_round_test.cpp` 重建及旧帧释放；两用例顺序测试 | 通过 |
| 失败轮不隐藏后续计划 | R5 | `test_prepare_failure_continues_and_recovery_failure_stops` 和全部轮次保留测试 | 通过 |
| 一项改善而另一项缺少候选 | R6 | 缺少比较用例测试；`compare.py` 不足三轮拒绝；汇总逐项保留 | 通过 |
| 明确使用一个允许的隔离核心 | R7 | C++ affinity 回读／调用者掩码不变；正式十八轮 actual CPU=4 且 setup<m0 | 通过 |
| 请求不可用的 CPU | R7 | 布尔、负数、非整数、离线／不允许 CPU 拒绝；C++ 无回退且无时钟 | 通过 |
| 固定边界保留 | R8 | 原 1200 帧、3302–4501、15721／19992663 微秒断言仍在 | 通过 |
| 同一源录像配置不同区间 | R8 | 独立范围解析与正式三个独立 test-03 fixture／基线 | 通过 |
| 输入悄悄变化 | R9 | 改变像素、解码期间替换源、替换片段和预检证书拒绝测试 | 通过 |
| 非参考录像尺寸 | R9 | Python 32×24 像素往返及 C++ 可变尺寸测试；尺寸来自清单，没有固定缩放 | 通过 |
| 非零首帧与姿态同步 | R10 | 原首帧偏移及 C++ `slowConsumerAndEof` 中姿态／原始到期时间断言 | 通过 |
| 整段录像缺少必要姿态 | R10 | `test_initial_attitude_missing`；`extract_attitude` 不合成姿态 | 通过 |
| 连续两轮输入一致 | R11 | 独立 node/worker、节点轮次测试、正式每轮首帧及完整输入审计 | 通过 |
| 五秒片段没有核心窗口 | R11 | `test_short_and_long_full_windows_do_not_inherit_core` | 通过 |
| 解码慢于录像但准备成功 | R12 | `test_slow_preparation_and_long_playback_use_independent_budgets`；正式缓存准备 37–107 秒后才回放 | 通过 |
| 准备阶段失败 | R12 | C++ 分配／OS 限制／解码／像素失败测试；拒绝模型与有效时钟 | 通过 |
| 整段模式使用全部缓存 | R12 | 真实小视频全帧像素验证；C++ 可变计数／实际字节及采集器实际数量测试 | 通过 |
| 候选指标更好 | R13 | `test_frozen_baseline_unchanged_and_no_automatic_pass`；新格式基线不可变测试 | 通过 |
| 不同录像不能拼成三轮 | R13 | `test_cases_versions_and_ranges_cannot_mix` 及不足三轮测试 | 通过 |
| 新模型与旧模型比较 | R14 | `test_true_model_content_change_is_incomparable` | 通过 |
| 同名用例改变区间 | R14 | 参数／时序变化及可变长度比较测试；身份含数值区间与像素映射 | 通过 |
| 尚未完成的超时运行 | R15 | 控制器最终化挂起、准备超时、先写合格后挂起、会话外子进程回收 | 通过 |
| 选择二十秒不代表程序只能运行二十秒 | R15 | 45 秒准备后 20 秒回放可控时钟测试；P／D／C 分离 | 通过 |

## 设计一致性与任务完成证据

八项设计决定均遵守：单配置及快照、明确帧选择、元数据贯穿各层、全量缓存及串行生命周期、独立截止、逐用例冻结与汇总、新旧版本边界、仅供帧线程的可验证 CPU 放置。

新 suite 为版本 1、逐轮配置版本 3，fixture／preparation／identity／completion／metrics 容器／baseline／comparison 为版本 2。旧结构约束没有被覆盖；未知版本拒绝。旧证据只读检查与新入口拒绝冻结旧格式的测试存在。指标公式和原始诊断事件版本未改。`core` 不再全局套用，原参考用例仍显式要求原窗口。

| 任务范围 | 核对内容与完成证据 |
| --- | --- |
| 1.1–1.5 | 版本化结构、安全配置解析、YAML 环境检查、错误／快照测试、README 和完整配置示例 |
| 2.1–2.5 | 独立 fixture、源时间选择、可信尾部、实际尺寸／像素、旧参考断言、准备文档及测试 |
| 2.6–2.7 | 四项模板及精确配套文件；既有真实来源／帧／姿态核验报告。未再次操作豁免视频 |
| 3.1–3.6 | 证书绑定、先缓存后模型再时钟、独立供帧／释放、双层截止、失败测试和资源说明 |
| 3.7 | 可选 supplier_cpu、身份／回读／拒绝路径、同二进制固定竞争对照、十八轮原限制全部合格 |
| 4.1–4.5 | 单配置入口、串行新进程、负载识别、本项身份、失败／回收行为、四项执行及输出说明 |
| 5.1–5.5 | 实际计数／窗口、三轮独立冻结、逐项汇总、版本与不可比检查、完整迁移流程 |
| 6.1 | 固定源码完整 `./build.sh --test`：57/57 CTest 和额外本机 OS 内存限制通过 |
| 6.2 | 三个 test-03 独立新组共十八轮完整合格；三份独立冻结基线和可比结果；test-02 用户豁免 |
| 6.3 | 本报告逐项检查全部 planning 工件／实现／需求／场景，列出范围及工具链限制；完成后勾选 |

新增文件沿用现有 Python 组件／unittest、C++ 测试源、schema 和 configs 目录模式。平台相关设置留在测试帧源及板端身份检查，不泄漏到 HybridTracker 或生产 ROS 编排。供帧线程在 start 中握手后使用冻结时钟，stop 唤醒并 join 后才释放本轮缓存；没有跨轮借用像素或新增生产线程策略。

## 本次独立复核与既有测试结果

证据根目录为 `/home/mzh/nickluo/drone_tracker-validation/configurable-video-regression-supply-fix-20261016`。

本核验重新执行了 `audit_formal.py` 中写出报告之前的全部审计逻辑。使用 conda base Python，只读取已保存结果，没有重新写入测试报告或运行证据。它重新读取原始二进制诊断，验证 CRC（每条记录的字节校验值），重做事件关联与合格判定，再重算所有指标。十八轮的结果与保存的 JSON 完全相同。每轮输入时间映射、数量、缓存字节、CPU 证明、逐帧 actual CPU、源码／程序身份及截止均符合要求。三组参考和候选均各三轮，三份冻结基线比较前后完全一致。

本核验另外重新执行 `openspec validate add-configurable-multi-clip-regression --strict` 和 `git diff --check`，均通过。当前源身份重算使用系统 Python 3.10，因为导入现有控制器需要 PyYAML，而 conda base 未安装该依赖；没有安装新依赖。没有必要重复已通过的硬件测试。

| 既有测试／实测 | 核验结果及证据 |
| --- | --- |
| 完整 ARM64 构建与 CTest | `build-fixed.log`：57/57，158.52 秒；目标为 ARM64，宿主 x86_64 镜像交叉构建 |
| 真正本机 OS 内存限制 | 同日志：1/1，5.60 秒；独立于 QEMU，不删除资源失败断言 |
| Python 架构划分 | `build-instrumented/backends/amd64/build/drone_tracker/Testing/Temporary/LastTest.log`：80 个唯一标识，79 项本机＋1 项 ARM Python，全部通过、无跳过 |
| 第一段 `[20,35)` | 新参考 3/3、候选 3/3，各 900 帧；最大供帧迟到 5.658222 毫秒 |
| 第二段 `[100,130)` | 新参考 3/3、候选 3/3，各 1801 帧；最大迟到 4.221814 毫秒 |
| 尾段 `[165,end)` | 新参考 3/3、候选 3/3，各 2575 帧，含原末帧 12478；最大迟到 13.300557 毫秒 |
| 供帧 CPU 证明 | 十八轮均请求 CPU 4，实际回读 `[4]`；策略 0、优先级 0；时钟建立前配置完成；逐帧采样均为 CPU 4 |
| 截止与回收 | P=3600、C=10；三个总截止 3625／3640／3653 秒；十八轮全部 normal EOF、未超时、进程回收 |
| 比较结论 | 三项均 comparable，decision=not_set；未给性能门槛，不能宣布性能通过或算法优化 |

实际板卡是 `HZ-RK3576_WuKong Board`，adb 序列号 `5ceb49407502303d`，机器标识 `1fe0a397f2ae4343896c7cf9d253a608`。内核 `6.1.141-rt36`，系统 RKNN Runtime 2.3.2 的内容校验值为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，RKNPU 驱动 0.9.8。既有 YOLO、NanoTrack 模板／搜索 Backbone 和 Head 内容未变。

正式组无人工竞争进程或高频调度采样器。最高观测温度 45.307°C，最低可用内存 22.13%。维护证据记录生产服务恢复为 active/running/enabled、PID 7893，无测试残留。配置、部署、调频和系统 Runtime 未变。核验代理没有重新触碰生产板；上述是任务二保留的前后状态证据，不冒充核验时的新在线查询。

固定竞争诊断中，继承 0–7 允许集合的三轮均超限，峰值 121.73–123.12 毫秒；相同二进制仅把供帧线程放到 CPU 4 的三轮均合格，峰值 2.30–2.86 毫秒。它支持所修复的调度机制。旧两次失败没有分段时序证据，无法证明其唯一原因；本报告不做该推断。先前热保护断连和降温后供帧失败也没有被混为同一事件。

## 未执行项与范围限制

七项规定的核验检查没有因证据缺失而跳过，Not verified 数为 0。以下为明确豁免或本 change 之外的实验，不计作缺失需求，也不称为已经通过：

- test-02 新准备、解码、回放和合格参考／候选组：用户明确跳过；历史四项输入核验继续只读保留，不能替代本次运行通过。
- 真实 180 秒整段、1280×720 等所有示例组合的板端重测：本次板端范围只含上述三项。通用路径通过可变尺寸、合成像素、180 秒计划／窗口和可控时钟测试检查，没有声称穷尽所有媒体。
- Jetson、真实相机、HDMI、真实飞行、跟踪真值准确率、其他模型／输入组合：不在本次录像回归验收范围；主机／QEMU 结果没有替代实机性能结论。
- clang-tidy／clangd 静态检查：当前环境没有配置可用入口；不是本任务必需构建检查，也没有据此宣称完成静态分析。
- 性能门槛通过：用户没有提供门槛，所以保留 not_set；18/18 的“合格”是输入、诊断和执行完整性结论。
- 主规格同步、归档、提交及推送：用户没有要求，本次未执行。

历史 90 秒准备失败、test-02 内存诊断、热保护中断、降温后两次供帧超限及人工竞争组均保留为独立证据，没有补选到新组或重写结论。

## 按优先级列出的问题

### CRITICAL：0

未发现未完成实施任务或缺失需求实现。任务 6.3 在本核验完成后记录为完成，不把正在执行的核验本身误判为缺失实现。

### WARNING：0

未发现需要指出的设计偏离或未处理场景。当前授权的 test-02 豁免与未设置性能门槛已经明确体现在工件和报告中。

### SUGGESTION：0

未发现显著代码模式偏离。没有为生成建议而引入本次范围之外的风格修改。

## 最终判断

全部适用核验检查通过。在当前明确授权范围内，变更具备申请归档的条件；实际归档和主规格同步仍需用户另行指示。本次 CRITICAL 为 0，后续用户流程第四项应按条件不修改代码、不额外重测。

主要结果索引：本 change 的 `onboard-test-report-supply-fix-20261016.md`；外部证据根下的 `test-report.md`、`test-report.csv`、`test-report.json`、`formal-audit.log`、`formal-results/`、`formal-control-result/` 和 `supply-lateness-investigation.md`。本报告不替换任何原始证据。
