# WARNING 2 修复补充验证

日期：2026-10-16。变更：`improve-detection-tracking`。工作目录为 `/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker`，分支为 `codex/improve-detection-tracking`。

本补充关闭 [2026-10-15 验证报告](verification-20261015.md) 的第二项 WARNING（请求与快照边界缺少直接集成断言）。第一项 WARNING（规划工件语言不一致）保留。本轮只增加测试与此记录，没有修改规划工件、正式实现、原验证报告或两份开发板比较报告。既有任务清单保持 22/22 完成，没有归档或提交。

## 补充的直接断言

来源快照是一次检测请求所携带的原帧身份、时间、框及有效标记。有效标记 `tracker_valid` 表示 NanoTrack 在该来源帧上确实获得了跟踪结果；继续显示上一帧的框不代表本帧跟踪成功。

| 测试与位置 | 输入及断言 |
|---|---|
| `testSubmittedSnapshotAfterNanoFailure`，`src/hybrid_tracker/tests/diagnostic_tracker_test.cpp:739` | 使用真实 `HybridTracker::update` 提交请求，由已有模拟模型执行检测并返回结果。条件变量屏障将推理阻塞到来源更新结束，防止同帧提前消费。注入一次 NanoTrack 搜索失败时，断言仍显示原框，中心为 `(50,40)`、尺寸为 `20×20`。随后在下一次成功跟踪更新中消费实际检测结果，检查其来源仍为第 1 帧、采集时间为 `10000` 微秒，且 `tracker_valid=false`、未确认计数仍为 4。另以成功来源更新作为对照，要求其 `tracker_valid=true`。分别运行 Basic 和 Detail 两种诊断模式，共 4 种组合。 |
| `testUnreadResultAdmission`，`src/hybrid_tracker/tests/diagnostic_tracker_test.cpp:811` | 等待真实检测结果完成，保持未消费，且工作线程与排队槽均已空闲。断言新 `request()` 返回 false；随后读出的原结果、请求身份、来源身份、全部来源快照字段、检测框及分数均未改变。被拒请求没有执行推理。消费原结果后，再次提交成功，并读出新请求的结果。分别运行 Off、Basic、Detail 三种模式；启用诊断时还要求拒绝原因为 `UnreadResult`。 |

第一个测试没有手工构造来源快照或检测结果。它观察实际结果消费时产生的 `SourceContext` 事件；该事件的 `BoxValid` 位直接来自 `YoloResult.source.tracker_valid`，实现位于 `src/hybrid_tracker/hybrid_tracker.cpp:663` 和 `:683`。来源身份断言保证观察的是旧请求，下一帧的成功跟踪不能覆盖旧帧的失败标记。测试使用独立的重叠阈值设置，避免无关的重新初始化路径掩盖该边界。

第二个测试使用仅在测试翻译单元中定义的读取器 `HybridTrackerDiagnosticTestAccess::unreadResult`。它在已有检测互斥锁内复制已完成结果，不消费或注入结果，也不改变正式程序接口。定义位于 `diagnostic_tracker_test.cpp:26`，声明位于 `diagnostic_test_access.h:15`。完成状态通过锁内检查同步，没有通过固定睡眠猜测推理结束。

## 本轮实际验证

使用 x86_64 开发机和既有 SDK 镜像 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。镜像中的交叉编译器生成 ARM64 程序，ARM64 测试通过 QEMU 指令模拟器运行；独立内存限制测试运行于原生 Linux。本轮不需要模型或视频输入，模型行为由受控测试后端提供。

```bash
./build.sh --test --artifacts-dir .docker-artifacts/warning2-20261016/build
openspec validate improve-detection-tracking --strict
git diff --check
```

- 完整构建成功，输出确认是 ARM64。
- CTest **57/57 通过**，耗时 **155.55 秒**。包含新增断言的 `diagnostic_tracker_tests` 通过，耗时 **0.64 秒**。
- 独立原生 Linux 内存限制测试 **1/1 通过**，耗时 **5.66 秒**。
- OpenSpec 严格校验通过，Git 空白检查通过。
- SDK 中没有 `clang-tidy` 或 `clangd`，因此未执行这两项静态检查；没有将缺失工具记为检查通过。

日志和可复核记录保存在仓库的 `.docker-artifacts/warning2-20261016/`：`build-test.log` 为完整输出，`build/backends/amd64/log/test-console.log` 为 CTest 结果，`lint-availability.log` 为静态检查工具状态。`test-accessor.patch` 与 `diagnostic-tests.patch` 保存相对于本轮开始时的测试增量。

## 与既有开发板证据的兼容性

按原 `production-source-match.json` 重新计算 **187 个正式源码文件的 SHA-256，187/187 与实测候选快照相同**。逐项结果保存在本轮的 `production-source-recheck.json`。本轮修改的两个测试文件不属于该正式源码集合。

在独立输出目录重新构建后，回归程序、ROS 节点和诊断检查器与原候选构建逐字节相同，见 `binary-source-compatibility.json`。其中回归程序 SHA-256 仍为：

```text
07b48c8ef98018dcf097598cc7a1640656d9b62e7125e188f8c6543dc9748874
```

本轮没有访问开发板，也没有停止或修改服务。无需重新执行视频回归来验证此次纯测试补充。2026-10-15 的源码归档、程序身份、原始记录和比较结论保留原样；本次新增断言没有被追记为当时已运行的测试。开发板、真实模型、相机、HDMI、长期负载、线程消毒器和飞行验证均未在本轮执行。

## 当前问题状态

原 WARNING 2 的两个覆盖缺口均已有实际通过的集成断言，因此关闭。原 WARNING 1 保留。本补充没有发现需要修改正式行为的问题，也没有重新执行原报告的全部六项 OpenSpec 审查；它仅提供第二项 WARNING 的关闭证据。
