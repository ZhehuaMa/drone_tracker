# 实施记录与实机验收阻塞

记录日期：2026-10-20。变更：`consume-yolo-result-before-request`。

## 当前结论

已完成 16 项任务中的 12 项。代码实现、确定性并发测试和完整构建检查已完成。实机验收在启动前被内存条件阻塞，任务 4.1–4.4 保持未完成。不得据此声称此变更已通过实机比较或完整实施。

用户指定只能通过 `ssh firefly-production` 接入 RK3576 板卡。用户已授权本轮临时停止生产服务，并要求工作结束后恢复。本次始终没有停止服务，也没有启动维护单元或恢复计时器。结束检查确认生产服务仍为 active/running/enabled。

## 已实现的调用顺序

Detecting（尚未初始化跟踪器的检测路径）与 Tracking（已有跟踪器的路径）都先调用一次 `get_result()`，把完整检测结果保存在当前更新的局部变量中。随后执行当前帧原有的 `request()` 或间隔跳过判定，最后处置该局部结果。

Tracking 在读取检测结果之前，先保存当前帧的 NanoTrack 观测。这样新请求保留当前帧的真实来源，不会绑定旧检测恢复后的状态。输入无效、时间重复和持续跟踪跳变的原有早退保持在结果读取之前。

读取为空后才完成的结果仍受 `UnreadResult` 保护，即未读结果可以拒绝当前提交。本次更新不会进行第二次读取。读取成功的 A 不会被后来完成的 B 替换。原有提交间隔、来源元数据、搜索级联、计数公式及严格看门狗边界保持原义。

仅测试构建提供 `snapshot_hook` 交接点，它在结果读取后、当前提交前执行测试回调。生产的非测试构建不编译这个入口。模拟推理和初始化的等待都有截止，并有失败时释放路径。没有增加生产配置、推理工作线程或设备锁顺序。

## 构建与测试证据

证据根目录：`/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020`。

使用 x86_64 宿主机和 amd64 镜像交叉编译 ARM64 程序。镜像为 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。ARM64 可移植测试通过项目指定的 QEMU 指令模拟器执行；本机操作系统内存上限检查单独原生执行。这里的通过结果不代表 NPU、相机或实机时序验收。

| 检查 | 结果 | 原始证据 |
|---|---|---|
| 最终 `./build.sh --test --artifacts-dir ...` | 57/57 CTest 通过，168.65 秒 | `backends/amd64/log/test-console.log` |
| 必需的原生内存上限检查 | 1/1 通过，5.81 秒 | `backends/amd64/log/native-resource-test.log` |
| `diagnostic_tracker_tests` | 通过，4.92 秒 | 同一最终 CTest 日志 |
| `recovery_regression_tests`、预处理契约与模型契约测试 | 全部通过 | 同一最终 CTest 日志 |
| 固定参考版本的回归工具检查 | 4/4 通过，87.07 秒；独立原生内存检查通过 | `reference-build/backends/amd64/log/test-console.log` |
| 候选与参考普通 Release 构建 | 均成功，输出 ARM64 | `candidate-build/backends/amd64/log/`、`reference-build/backends/amd64/log/` |
| `git diff --check` | 通过 | 实施时执行 |
| `openspec validate consume-yolo-result-before-request --strict` | 通过 | 实施时执行 |
| C++ 静态检查及语言服务器检查 | 未验证：项目没有配置可用的 clang-tidy/clangd 入口，当前环境也没有这些工具 | 本次环境检查 |

首次完整测试为 56/57。唯一失败是既有 `deployment_package_tests` 假定仓库下存在 `.docker-artifacts/install/drone_tracker`。新 worktree 没有这个被忽略的目录，打包入口按既有规则拒绝。补建这个仅供模拟 Docker 测试使用的空目录后，完整重跑得到 57/57；没有修改测试断言或打包默认路径。首次失败日志保留在 `first-build-test.log`。

参考的第一次本地共享克隆构建因 Git 对象 alternates 路径未挂载到容器而失败。将独立参考克隆补全为自包含对象库后，构建和四项回归工具检查均通过；源码仍固定为原提交。外部初始失败日志为 `/tmp/consume-yolo-reference-build.log`，成功记录在参考构建日志目录。

新增及扩展的模拟测试覆盖：正/负已就绪结果、工作线程阻塞时的空快照、Tracking 提交与非提交帧、来源身份、读取后发布导致的合法拒收、A 已保存而 B 随后完成、当前提交先于历史查找、初始化阻塞时下一次推理仍能完成、最新待执行替换只记账一次、更新返回后带执行中请求的 OFF（结束当前跟踪回合）、新回合帧号复用、来源图像淘汰、重复时间与原生跳变早退，以及原有未确认计数和看门狗边界。相应用例覆盖 Off、Basic、Detail 三种诊断模式。独立检测器的 `testUnreadResultAdmission()` 原断言保留。

## 实机比较计划与真实版本身份

在首轮之前建立了全新的外部证据根和 `control/{ref,cand,freeze,compare}.json`。只选择 test-03 的 `[20,35)`、`[100,130)`、`[165,end)` 三个区间。每侧每项三轮，原计划共 18 轮；test-02 没有准备或回放。

配置固定 `predecode_all`（回放前把全部灰度帧解码并持有在内存）、供帧 CPU 4、准备期限 P=3600 秒、收尾期限 C=10 秒、供帧延迟上限 50000 微秒、同一输入、模型和算法参数。没有设置性能门槛，判定保持 `not_set`。两个回归程序都来自同一镜像且启用测试目标的构建，诊断工作进程固定使用同一程序。普通 Release 构建不会生成回归入口，因此没有把普通 Release 输出误当成实际回归程序。

| 身份 | 参考 | 候选 |
|---|---|---|
| 固定参考提交 | `7fbb0a2e006f12654a7db0f6d9d152244ec07e0d` | 同一基点加本次实际未提交实现 |
| 实际源码 SHA-256 | `ac6a9a144069dc14b34f2cbf15dee7ec7aafa5e0637882bcf2eba3b7300c3f23` | `cee0a286671250ee421a3e8864227d5b0daf99112d3d8d990cc5c5fd9c0e9a8f` |
| 实际回归程序 SHA-256 | `b8bb900d209feefe0518b53a3d1ac714a6bfe6b1b471e5429766811eb0eac781` | `0afd257edcc8a01f3cc99d99069ca3d9445d800d1f37496ab07c65025ff34f4d` |

`plan.json`、`ref-source-export.json`、`cand-source-export.json` 和两个源码归档保存上述绑定。参考与候选源码及二进制均不同。板端也复核了两个程序的二进制校验值及动态库解析；没有缺失动态库。一次 `--help` 探测发现该入口把参数当作配置路径，并返回 `bad file: --help`；这不属于合格回放或模型验证。

## 当前唯一授权板卡的内存阻塞

板卡访问：`ssh firefly-production`。型号 `HZ-RK3576_WuKong Board`，主机名 `hzhy`，序列号 `77646fe8a08c7fb8`，machine ID `1fe0a397f2ae4343896c7cf9d253a608`，内核 `6.1.141-rt36`。

本次 `/proc/meminfo` 为 `MemTotal=3997632 KiB`，即 4,093,575,168 字节；`SwapTotal=0`、`SwapFree=0`。Runtime 为系统 RKNN Runtime 2.3.2，系统库 SHA-256 为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，RKNPU 驱动为 0.9.8。原始记录为外部证据目录的 `board-preflight.txt`。

一个 Gray8 帧每像素占一字节。fixture 是包含不可变像素、时间、姿态和模型身份的输入目录。三个 fixture 的宽高都为 1920×1200，所以每帧独立像素矩阵需要 2,304,000 字节。`realtime_source.cpp` 的解码路径逐帧分配并填充 `cv::Mat`，然后把每个矩阵放入 `decoded_`，直到整个区间解码完毕；这不是文件映射或可按需释放的缓存。

| 区间 | 帧数 | 仅像素缓存的必需字节数 | 相对整板物理内存 |
|---|---:|---:|---|
| `[20,35)` | 900 | 2,073,600,000 | 小于总内存，但未进行回放 |
| `[100,130)` | 1801 | 4,149,504,000 | 已超过总内存，尚未计模型和解码开销 |
| `[165,end)` | 2575 | 5,932,800,000 | 已超过总内存，尚未计模型和解码开销 |

计算原始数据保存在 `board-memory-requirements.json`。中段与末段在分配全部像素前就已无法满足此板的物理内存条件。长准备期限不能增加可用内存。现有准备失败路径记录错误并清空缓存；它不会自动缩短录像或改为流式供帧。没有为尝试运行而修改这些规则，也没有制造 OOM（内存耗尽）事故。

历史成功记录使用相同型号名，但当时观测到 `MemTotal=8112044 KiB`。其中末段一轮确实持有 5,932,800,000 像素字节。历史 8 GiB 级环境不能替代本次唯一授权 SSH 目标的 4 GiB 级环境。

因此，本次实机参考轮数为 0，候选轮数为 0；没有新的冻结基线、比较报告、388/389 帧结论、有效框比例或性能结论。三项均不得使用历史结果补齐。根据用户的顺序失败门槛，后续正式 verify-fix 子任务不得启动。

## 生产状态与剩余工作

生产入口始终为 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。生产配置 SHA-256 保持 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`。服务保持原 PID 5067、active/running/enabled。上传到 `/home/hzhy/consume-yolo-20261020` 的本次临时测试资产已清理；清理后的状态见外部证据 `board-final-state.txt`，初始状态的小型归档见 `board-staging-initial-metadata.tar`。未部署候选、未替换模型或系统 Runtime、未更改 CPU 策略。

剩余任务 4.1–4.4 需要一个能满足冻结输入内存条件、且经用户明确允许使用的验收环境，或另行批准的实验设计修改。本次不擅自改用其他板卡、添加交换空间、缩短区间或更改供帧方式。未来恢复时须重新核对板卡身份和环境，保留本记录与失败门槛，按完整计划取得新的三轮参考和三轮候选证据。
