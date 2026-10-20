# 实施与实机验收记录

## 结论及范围

`consume-yolo-result-before-request` 的实现任务已完成。两个正常交互点均执行“结果快照 → 当前请求或间隔跳过 → 旧结果仲裁”。快照是从共享结果槽一次取走并保存在当前更新中的完整局部值。未修改检测器的未读保护、待执行替换规则、模型、生产参数、ROS 接口及看门狗公式。

本记录覆盖 tasks.md 的 16 项实施任务。最终版本二实验包含 9 轮参考及 9 轮候选，18 轮全部执行合格并完成资源回收。三个独立比较均为 `comparable`，性能门槛均为 `not_set`。套件汇总因此为 `not_passed`，不是性能通过结论。无人工目标标注，本记录不证明准确率提升，也不要求命中特定帧。未同步主规格、归档、提交或部署；用户要求的下一阶段独立核验尚未由本实施代理执行。

## 实现及受控验证

Detecting 路径在预测及历史加入后读取一次结果，再请求当前图像。Tracking 路径先完成当前 NanoTrack 观测及来源元数据，再读取一次结果，执行原提交间隔判定，最后仲裁旧结果。后续完成的结果不会替换局部快照，同一更新不进行第二次读取。输入无效、重复时间及跟踪跳变的原提前返回位置保持不变。

测试专用快照交接钩子只在 `DRONE_TRACKER_DIAGNOSTIC_TESTING` 下编译。它在取走快照后、检测器锁外运行。条件变量等待均有截止及释放路径；生产不增加配置、推理线程或锁顺序。

| 规格要求 | 实现及测试证据 | 实机证据 |
|---|---|---|
| 已完成结果先取走 | `hybrid_tracker.cpp` 两个交互点；`testDetectingSnapshotOrder`、`testTrackingSnapshotOrderAndEarlyReturns` | 候选 558 次成功读取均先于当前请求/跳过 |
| 当前提交先于旧结果仲裁 | 同两处局部 `has_result` 分支；`testRequestOverlapsRecoveryAndRoundCleanup` | 有历史查找/初始化的读取均先完成当前判定 |
| 每更新只处置保存结果一次 | `testCompletionAfterSnapshot` 的 A/B 身份、回合及清理用例 | 每次更新最多一次成功读取，旧请求与当前请求身份分离 |
| 读取与提交之间完成仍保护 | 空快照后完成用例；原 `testUnreadResultAdmission` 断言保留 | 实机不以拒收为零代替受控竞态证明 |
| 节奏、搜索及早退保持 | Tracking 非提交帧、来源快照、早退、看门狗边界；Off/Basic/Detail 模式及清理用例 | 当前来源更新先于晚到结果仲裁，保留 `RequestSkipped` |

测试源码在 `src/hybrid_tracker/tests/diagnostic_tracker_test.cpp`；仅测试入口在 `diagnostic_test_access.h`。耗时恢复测试证明阻塞初始化时工作线程仍能执行当前已提交请求。OFF 清理从更新返回后开始，不新增并发 reset 契约。已有图像淘汰及待执行替换测试仍检查身份和一次记账。

## 构建记录

使用项目默认 x86_64 容器交叉编译 ARM64，镜像 SHA-256 为 `025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。候选执行 `./build.sh --test --artifacts-dir /home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020`，57/57 CTest 通过，总耗时 168.65 秒；独立原生内存限制检查 1/1 通过，耗时 5.81 秒。诊断测试及恢复、预处理、模型契约检查均通过。完整记录：[final-build-test.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/final-build-test.log)。

首次完整构建中，既有部署打包测试需要新 worktree 尚无的忽略目录 `.docker-artifacts/install/drone_tracker`，因此失败。建立隔离的空测试夹具目录后完整重跑通过，没有修改断言、默认值或后端。首次失败保留在 [first-build-test.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/first-build-test.log)。

参考固定为提交 `7fbb0a2e006f12654a7db0f6d9d152244ec07e0d`，在独立克隆中以同一镜像和测试开关构建。其回归工具 4/4 检查及原生内存限制检查通过。首次克隆共享对象目录未挂载进容器，构建失败；将参考克隆对象完整化后重建通过。失败记录保留在 [reference-first-build-failure.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/reference-first-build-failure.log)，最终日志在 `reference-build` 目录。参考及候选实际回归程序均来自启用测试的构建；没有将不含回归入口的普通 release 输出冒充实测程序。

## 设备、授权与历史尝试

首次仅允许 SSH 的板卡序列号为 `77646fe8a08c7fb8`，系统内存为 3997632 KiB、无交换区。冻结预解码缓存中段需要 4149504000 字节、尾段需要 5932800000 字节，已超过该板系统总内存；未冒险执行 OOM 回放，也未改变缓存条件。它的正式服务从未停止，记录为 PID 5067、active/running/enabled。历史阻塞报告独立保留在 [implementation-report-ssh-blocked.md](/home/mzh/nickluo/drone_tracker-worktrees/consume-yolo-result-before-request/openspec/changes/archive/2026-10-20-consume-yolo-result-before-request/implementation-report-ssh-blocked.md)，没有删除或改写为成功。

用户随后明确允许另一块接近 8 GiB 板通过 ADB 执行。最终实验所有 ADB 操作均显式指定 `adb -s 5ceb49407502303d`，不连接其他 SSH 别名。该板型号为 HZ-RK3576_WuKong Board，内核 6.1.141-rt36，内存 8112044 KiB、无交换区。两板的主机名和克隆 machine-id 相同，因此以不同硬件序列号区分。最终证据属于这块 8 GiB 板，不能替代未回放的 4 GiB 板验收。其系统 Runtime 为 2.3.2、RKNPU 驱动为 0.9.8。

第一次 ADB 运行的公共环境包装遗漏了 `AMENT_PREFIX_PATH`。前两轮短段参考均完成预解码后在节点启动时报 `package 'drone_tracker' not found, searching: [/opt/ros/humble]`，均不合格；第三轮在准备阶段中断，未产生完整轮结论。该次没有候选或有效回放。先停止测试并恢复服务至 PID 5293、active/running/enabled，再修复公共环境为已有 `/home/hzhy/cvr16/install` 前缀，并以只读包查找确认。完整失败及中断数据保留在 [首次 ADB 证据目录](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-20261020/board-aborted)。

修复的是实验包装环境，没有修改程序、模型、输入、时间限制或判定公式。随后使用全新版本二套件 `consume-yolo-result-before-request-test03-environment-v2`，重新预声明完整 18 轮，未挑选替换失败轮。下文所有统计均来自这个完整的新套件；首次失败不混入其参考基线。

## 冻结条件与身份

最终证据根目录为 `/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020`，板端隔离目录为 `/home/hzhy/cyr20b`。首轮之前固定配置、源码、二进制、共享工作程序及输入身份；完整记录在 [frozen-plan.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/frozen-plan.json)。程序身份是对实际文件内容计算的 SHA-256 校验值，不只依据分支名。候选源码导出覆盖实际程序、测试及构建输入，不包含 OpenSpec 报告，因此后续报告编辑不改变实测源码身份。

三个区间为 test-03 的 20–35 秒、100–130 秒和 165 秒至结束，各侧各三轮。使用同一组已有已验证夹具（固定像素、帧日志、姿态和 PTS，PTS 为源视频中的显示时间）。每轮先完整预解码 Gray8 缓存，固定准备上限 P=3600 秒、收尾 C=10 秒、供帧 CPU 4、最大供帧迟到 50000 微秒、队列 32。全部 18 轮供帧 CPU 请求为 4，允许集合为 `[4]`；每轮迟到最大值均小于 50 毫秒。test-02 无准备、无回放。

控制器使用 CPU 0–7，其他服务保留，正式跟踪服务临时停止。没有增加争用负载或调度器采样器；未改变 CPU governor、系统 Runtime 或模型。公共环境先加载 `/opt/ros/humble/setup.bash`，再加入已有 ament 前缀、生产 bundled ROS 库和隔离工具目录。ffmpeg/ffprobe 为同一冻结的 4.4.2 工具；全部工具及算法参数校验值保留在逐轮 `identity.json` 和冻结配置中。

| 身份 | 参考 | 候选 |
|---|---|---|
| 实际源码 | `ac6a9a144069dc14b34f2cbf15dee7ec7aafa5e0637882bcf2eba3b7300c3f23` | `cee0a286671250ee421a3e8864227d5b0daf99112d3d8d990cc5c5fd9c0e9a8f` |
| 实际回归程序 | `b8bb900d209feefe0518b53a3d1ac714a6bfe6b1b471e5429766811eb0eac781` | `0afd257edcc8a01f3cc99d99069ca3d9445d800d1f37496ab07c65025ff34f4d` |
| 共享诊断工作程序 | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` |

参考和候选源码及回归程序校验值不同；诊断工作程序完全相同。三个模型含 YOLO、NanoTrack Backbone 和 Head；Backbone 的模板及搜索各有一个文件：

| 模型角色 | SHA-256 |
|---|---|
| `tracker.yolo_engine` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| `tracker.nano_template_backbone` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `tracker.nano_search_backbone` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `tracker.nano_head` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

系统 `/usr/lib/librknnrt.so` SHA-256 为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。未将容器 Runtime 复制到系统库。

## 正式服务恢复

版本二开始前正式服务为 active/running/enabled、PID 5293。每次停止前先建立并确认独立 `cyr20b-restore.timer`，超时将停止测试进程组并启动正式服务；主维护程序也在 finally 路径回收测试及恢复服务。最终 18 轮完成后先恢复服务，再冻结基线、生成比较及进行只读分析。

最终正式服务为 **active/running/enabled、PID 6869**，恢复定时器已 inactive，没有测试节点或工作进程遗留。生产入口仍为 `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`，生产配置 SHA-256 为 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`。入口、配置、Runtime、模型、governor 及 boot-id 与维护前均一致。证据：[postcheck.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/postcheck.json)、[finished.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/finished.json)、[postcheck-console.log](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/postcheck-console.log)。版本二维护及运行总耗时为 2280.636 秒。

实验早期父代理有短暂只读 ADB 状态查询，随后停止，改读主机本地控制日志。原先冻结的工作负载快照检查、事件完整性及供帧限制均未放宽，18 轮仍完整合格。没有连续进程清单采样，不能证明这些瞬时查询对时序绝对无影响；此观察条件随数据一并披露。

## 逐轮输出

有效发布指本程序发布的有效框，不是人工判定的正确目标。总发布数及有效比例按每轮规范单调时钟窗口 `[m0, m0+D)` 计算，D 为该夹具时长；不把窗口后收尾发布混入分母。表中耗时为更新和检测的均值，单位毫秒。superseded 表示尚未执行的待处理请求被更新请求替换。

| 区间 | 程序/轮 | 有效/总发布 | 有效比例 | 更新均值 ms | 检测均值 ms | 初始化 | 拒收 | 替换 | 最大供帧迟到 ms | 窗口外发布 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| wlcb-104206-020-035 | 参考/1 | 0/900 | 0.0000% | 0.0290437 | 462.281 | 0 | 32 | 834 | 2.348 | 0 |
| wlcb-104206-020-035 | 参考/2 | 0/894 | 0.0000% | 0.0295247 | 464.507 | 0 | 32 | 828 | 2.64347 | 0 |
| wlcb-104206-020-035 | 参考/3 | 0/900 | 0.0000% | 0.0290492 | 464.21 | 0 | 32 | 834 | 2.60036 | 0 |
| wlcb-104206-100-130 | 参考/1 | 0/1801 | 0.0000% | 0.0289792 | 460.902 | 0 | 65 | 1669 | 2.24949 | 0 |
| wlcb-104206-100-130 | 参考/2 | 0/1801 | 0.0000% | 0.0289729 | 461.183 | 0 | 65 | 1669 | 2.58484 | 0 |
| wlcb-104206-100-130 | 参考/3 | 0/1801 | 0.0000% | 0.0291784 | 460.915 | 0 | 65 | 1669 | 2.73321 | 0 |
| wlcb-104206-165-end | 参考/1 | 197/2516 | 7.8299% | 1.63815 | 461.153 | 3 | 84 | 2191 | 7.02997 | 2 |
| wlcb-104206-165-end | 参考/2 | 205/2530 | 8.1028% | 1.58919 | 465.812 | 2 | 85 | 2198 | 14.5946 | 1 |
| wlcb-104206-165-end | 参考/3 | 267/2530 | 10.5534% | 1.96574 | 444.093 | 2 | 82 | 2151 | 5.19014 | 1 |
| wlcb-104206-020-035 | 候选/1 | 0/900 | 0.0000% | 0.0301751 | 471.378 | 0 | 0 | 867 | 3.22918 | 0 |
| wlcb-104206-020-035 | 候选/2 | 0/900 | 0.0000% | 0.0302014 | 468.491 | 0 | 0 | 867 | 3.51138 | 0 |
| wlcb-104206-020-035 | 候选/3 | 0/900 | 0.0000% | 0.0302632 | 468.069 | 0 | 0 | 867 | 2.68864 | 0 |
| wlcb-104206-100-130 | 候选/1 | 0/1801 | 0.0000% | 0.0306501 | 467.183 | 0 | 0 | 1735 | 2.62983 | 0 |
| wlcb-104206-100-130 | 候选/2 | 0/1798 | 0.0000% | 0.0312687 | 462.956 | 0 | 0 | 1732 | 3.01624 | 0 |
| wlcb-104206-100-130 | 候选/3 | 0/1801 | 0.0000% | 0.0300411 | 460.859 | 0 | 0 | 1734 | 2.47152 | 0 |
| wlcb-104206-165-end | 候选/1 | 353/2495 | 14.1483% | 2.76744 | 487.542 | 3 | 0 | 2142 | 4.08776 | 1 |
| wlcb-104206-165-end | 候选/2 | 76/2549 | 2.9816% | 0.664393 | 465.553 | 1 | 0 | 2399 | 10.1231 | 1 |
| wlcb-104206-165-end | 候选/3 | 317/2459 | 12.8914% | 2.77872 | 466.209 | 3 | 0 | 2129 | 9.32305 | 1 |

全部逐轮数据及分位数在 [round-summary.csv](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/round-summary.csv)。尾段每轮收尾有 1–2 个窗口外发布，它们保留在原始记录和 `all_process_*` 补充统计中，不改变规范窗口公式。短段参考第 2 轮及中段候选第 2 轮发布数较少仍原样保留，未因结果挑选轮次。

## 每个区间的三轮比较

以下每格为“三轮中位数 [最小, 最大]”。差值为候选中位数减参考中位数；相对变化为差值除以参考中位数。参考为零时相对值不可计算，不用无穷大或零冒充。无初始化或补跟踪事件时耗时为空，不是零毫秒。最长无效输出及最大发布间隔均按现有指标公式计算。有效比例的绝对差使用比例单位，乘 100 后才是百分点。

完整逐轮数组、同序号轮差值、所有分位数的中位数/范围及相对变化保留在 [supplemental-comparisons.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/supplemental-comparisons.json)，不是将不同录像相加或平均。各轮是异步重复实验，同序号差值仅为描述，不表示配对的确定性因果效果。

### wlcb-104206-020-035

| 指标 | 参考 | 候选 | 差值 | 相对变化 % |
|---|---|---|---:|---:|
| 有效发布数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 总发布数 | 900 [894, 900] | 900 [900, 900] | 0 | 0 |
| 有效比例 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 最长无效输出 ms | 14985.7 [14985.5, 14985.7] | 14985.6 [14985.5, 14985.6] | -0.108697 | -0.000725338 |
| 最大发布间隔 ms | 18.1111 [18.073, 127.709] | 18.4801 [18.3506, 18.6422] | 0.369018 | 2.03753 |
| 更新均值 ms | 0.0290492 [0.0290437, 0.0295247] | 0.0302014 [0.0301751, 0.0302632] | 0.00115212 | 3.96609 |
| 更新 p95 ms | 0.038793 [0.038035, 0.040323] | 0.040752 [0.039623, 0.040846] | 0.001959 | 5.04988 |
| 检测均值 ms | 464.21 [462.281, 464.507] | 468.491 [468.069, 471.378] | 4.28093 | 0.922197 |
| 检测 p95 ms | 464.723 [462.553, 466.567] | 471.157 [470.956, 484.068] | 6.43361 | 1.3844 |
| 初始化次数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 初始化均值 ms | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 |
| 补跟踪尝试数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 补跟踪步数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 补跟踪均值 ms | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 |
| 队列等待均值 ms | 8.66828 [8.39593, 8.73373] | 7.24142 [6.86828, 7.25726] | -1.42686 | -16.4607 |
| 拒收数 | 32 [32, 32] | 0 [0, 0] | -32 | -100 |
| 未读结果拒收数 | 32 [32, 32] | 0 [0, 0] | -32 | -100 |
| 替换数 | 834 [828, 834] | 867 [867, 867] | 33 | 3.95683 |
| 准备耗时 ms | 37127.4 [37108.1, 37163.9] | 37438.4 [37403.6, 37553.7] | 311 | 0.837656 |
| 准备峰值 RSS MiB | 2064 [2063.98, 2064] | 2063.98 [2063.96, 2064] | -0.015625 | -0.000757027 |
| 准备完成系统可用 MiB | 5605.98 [5604.89, 5607.7] | 5608.5 [5608.35, 5608.54] | 2.52734 | 0.045083 |

### wlcb-104206-100-130

| 指标 | 参考 | 候选 | 差值 | 相对变化 % |
|---|---|---|---:|---:|
| 有效发布数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 总发布数 | 1801 [1801, 1801] | 1801 [1798, 1801] | 0 | 0 |
| 有效比例 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 最长无效输出 ms | 29995.3 [29994.8, 29995.6] | 29995.1 [29994.8, 29995.1] | -0.247773 | -0.000826039 |
| 最大发布间隔 ms | 18.0175 [17.9892, 18.263] | 18.2237 [18.0535, 72.1565] | 0.206277 | 1.14487 |
| 更新均值 ms | 0.0289792 [0.0289729, 0.0291784] | 0.0306501 [0.0300411, 0.0312687] | 0.00167096 | 5.76606 |
| 更新 p95 ms | 0.037652 [0.037047, 0.03907] | 0.039884 [0.038868, 0.042099] | 0.002232 | 5.92797 |
| 检测均值 ms | 460.915 [460.902, 461.183] | 462.956 [460.859, 467.183] | 2.04098 | 0.442811 |
| 检测 p95 ms | 463.987 [463.081, 464.09] | 465.588 [462.674, 470.393] | 1.60091 | 0.345034 |
| 初始化次数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 初始化均值 ms | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 |
| 补跟踪尝试数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 补跟踪步数 | 0 [0, 0] | 0 [0, 0] | 0 | 无事件/不可计算 |
| 补跟踪均值 ms | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 | 无事件/不可计算 |
| 队列等待均值 ms | 8.58091 [8.23703, 8.76913] | 8.24766 [7.5309, 8.29258] | -0.333256 | -3.88369 |
| 拒收数 | 65 [65, 65] | 0 [0, 0] | -65 | -100 |
| 未读结果拒收数 | 65 [65, 65] | 0 [0, 0] | -65 | -100 |
| 替换数 | 1669 [1669, 1669] | 1734 [1732, 1735] | 65 | 3.89455 |
| 准备耗时 ms | 73692.9 [73601.8, 73827.2] | 74125.9 [74066.2, 74164.1] | 433 | 0.587574 |
| 准备峰值 RSS MiB | 4050.37 [4049.17, 4050.39] | 4050.38 [4050.37, 4050.39] | 0.0078125 | 0.000192884 |
| 准备完成系统可用 MiB | 3613.96 [3612.41, 3614.54] | 3614.56 [3613.98, 3614.56] | 0.597656 | 0.0165374 |

### wlcb-104206-165-end

| 指标 | 参考 | 候选 | 差值 | 相对变化 % |
|---|---|---|---:|---:|
| 有效发布数 | 205 [197, 267] | 317 [76, 353] | 112 | 54.6341 |
| 总发布数 | 2530 [2516, 2530] | 2495 [2459, 2549] | -35 | -1.3834 |
| 有效比例 | 0.0810277 [0.0782989, 0.105534] | 0.128914 [0.0298156, 0.141483] | 0.0478865 | 59.099 |
| 最长无效输出 ms | 36800.9 [36369.9, 36888.4] | 29243.1 [22212.8, 41605.6] | -7557.8 | -20.537 |
| 最大发布间隔 ms | 475.755 [461.366, 480.43] | 491.396 [422.128, 539.138] | 15.6409 | 3.28758 |
| 更新均值 ms | 1.63815 [1.58919, 1.96574] | 2.76744 [0.664393, 2.77872] | 1.12929 | 68.9367 |
| 更新 p95 ms | 15.5651 [15.5167, 15.7302] | 15.9086 [0.048035, 16.2837] | 0.343476 | 2.2067 |
| 检测均值 ms | 461.153 [444.093, 465.812] | 466.209 [465.553, 487.542] | 5.0563 | 1.09645 |
| 检测 p95 ms | 539.941 [539.466, 542.767] | 506.076 [476.22, 543.947] | -33.8648 | -6.27194 |
| 初始化次数 | 2 [2, 3] | 3 [1, 3] | 1 | 50 |
| 初始化均值 ms | 7.13824 [7.01518, 7.70635] | 7.60055 [7.11845, 8.73013] | 0.462309 | 6.47651 |
| 补跟踪尝试数 | 2 [2, 3] | 3 [1, 3] | 1 | 50 |
| 补跟踪步数 | 42 [42, 58] | 63 [25, 75] | 21 | 50 |
| 补跟踪均值 ms | 331.315 [304.568, 333.897] | 395.323 [395.154, 397.383] | 64.0089 | 19.3197 |
| 队列等待均值 ms | 8.80394 [8.72116, 10.5532] | 18.0852 [9.10438, 25.0769] | 9.28128 | 105.422 |
| 拒收数 | 84 [82, 85] | 0 [0, 0] | -84 | -100 |
| 未读结果拒收数 | 84 [82, 85] | 0 [0, 0] | -84 | -100 |
| 替换数 | 2191 [2151, 2198] | 2142 [2129, 2399] | -49 | -2.23642 |
| 准备耗时 ms | 106655 [106598, 106714] | 106866 [106523, 107858] | 211 | 0.197834 |
| 准备峰值 RSS MiB | 5741.42 [5741.4, 5741.45] | 5741.46 [5722.55, 5741.46] | 0.0390625 | 0.000680363 |
| 准备完成系统可用 MiB | 1917.58 [1916.84, 1917.89] | 1917.12 [1916.03, 1918.12] | -0.453125 | -0.0236301 |

短段及中段两侧全部为零框、零初始化。尾段候选三轮有效比例为 14.1483%、2.9816%、12.8914%，参考为 7.8299%、8.1028%、10.5534%；候选第 2 轮低于同序号参考，且候选尾段更新耗时中位数更高。保留这些分散结果，不把拒收下降或中位数上升解释为跟踪质量通过。

准备内存中的 RSS 是 Linux 记录的该进程驻留物理内存。它与系统可用内存不同，也不是所有进程之和。缓存像素字节分别为 2073600000、4149504000、5932800000。4473 次维护期遥测中的系统可用内存最低为 1726.504 MiB；这是离散观测值，不能宣称连续运行峰值。逐轮准备内存和完整系统遥测均保留，未用压缩或按需解码改变实验条件。

## 中段 fixture 388/389 的请求身份

fixture_frame_id 为从零开始的夹具编号，不能直接当成内部 tracker_frame_id 或 request_id。此处 388 对应源视频帧 6390、PTS 106465935 微秒；389 对应源帧 6391、PTS 106482584 微秒。以下“替换”均为提交成功后、执行前被更新输入替换。

| 程序/轮 | fixture 388 | fixture 389 | 本轮初始化/有效框 |
|---|---|---|---|
| 参考 1 | request 389，接受、执行、NoTarget，阶段最高分 0.291989 | request 390，UnreadResult 拒收 | 0 / 0 |
| 参考 2 | request 389，接受、执行、NoTarget，阶段最高分 0.291989 | request 390，UnreadResult 拒收 | 0 / 0 |
| 参考 3 | request 389，UnreadResult 拒收 | request 390，接受后替换，未执行 | 0 / 0 |
| 候选 1 | request 389，接受后替换，未执行 | request 390，接受后替换，未执行 | 0 / 0 |
| 候选 2 | request 386，接受后替换，未执行 | request 387，接受后替换，未执行 | 0 / 0 |
| 候选 3 | request 389，接受、执行、NoTarget，阶段最高分 0.291989 | request 390，接受后替换，未执行 | 0 / 0 |

候选第 2 轮此前存在输入选择跳过，因此内部请求编号与 fixture 编号偏移不同。三轮候选均接受 389，但均未执行它；不能把接受请求写成命中目标。当前运行没有该帧的推理分数，不能引用历史约 0.537 的分数作为本轮结果。阈值保持 0.5。六轮中段初始化全为零，分布没有被中位数掩盖。

完整交叉关联见 [medium-388-389.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/medium-388-389.json)、[critical-events.csv](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/critical-events.csv)。每轮仍保留 `frames.csv`、`submissions.csv`、`calls.csv`、`messages.csv`、`input.jsonl` 和 `events.jsonl`，能从供帧、选择、请求判定、执行、完成、读取一直追踪到初始化及发布。

## 原始数据、顺序审计与基线

[call-order-audit.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/call-order-audit.json) 覆盖 571 次参考成功读取和 558 次候选成功读取。参考均为当前判定先于读取；候选均为读取先于当前请求/间隔跳过。比较同时使用单调时间及严格 producer_seq（同一生产线程的递增序号）。全部更新至多一次成功读取；有旧结果历史查找或初始化的情况均在当前判定之后；旧结果来源与当前请求来源身份保持分离。没有顺序违规。空读取不产生 `ResultRead` 事件，因此空快照边界由源码及受控测试证明，不能用实机成功读取事件替代。

逐轮完整性校验检查全部 completion 列出的文件校验值，结果在 [integrity.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/integrity.json)。另从原始 `.dtd` 二进制分段重新执行 CRC（分段内容校验）及解码，重新关联输入身份，再重算指标：18 轮共 327201 个事件，规范化事件与保留文件逐条一致，所有 18 份指标与原 JSON 精确一致。重算使用 Python 3.10.12，与板端一致；Python 内部 tuple 经 JSON 编码为数组后比较。只读重算不覆盖原文件。脚本为 [recompute_metrics.py](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/recompute_metrics.py)，结果为 [metric-recomputation.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/analysis/metric-recomputation.json)。

正式冻结/比较入口在恢复服务后各执行一次，退出值均为 0。各项基线只读且比较前后校验值一致，完整文件清单见 [baseline-integrity.json](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/control/baseline-integrity.json)。三个 `baseline.json` 的 SHA-256 为：

| 用例 | SHA-256 |
|---|---|
| wlcb-104206-020-035 | `d93f57307e3a5bed5e67e172fdaf745702dbac75034bee02e6e8483c08df3046` |
| wlcb-104206-100-130 | `8b5cb2515e90aa59edba0a7f3a3e7fc128a64df60d905f70c52ac59fae3f595d` |
| wlcb-104206-165-end | `422b2502a6d77ad6bf9754ab40474287679980b59eae843b20790da97de8fe1b` |

中央正式报告：[report.md](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/report.md)。三份独立报告：

- [wlcb-104206-020-035](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/wlcb-104206-020-035/report.md)；同目录有 comparison.json、comparison.csv。

- [wlcb-104206-100-130](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/wlcb-104206-100-130/report.md)；同目录有 comparison.json、comparison.csv。

- [wlcb-104206-165-end](/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-adb-b-20261020/board-final/results/comparisons/candidate-test03/wlcb-104206-165-end/report.md)；同目录有 comparison.json、comparison.csv。

## 一致性与未验证限制

已逐项核对 proposal 的行为范围、design 的五项决策、delta spec 的五项要求和 tasks.md：两个源码交互点、受控并发/生命周期测试、真实不同程序的 18 轮比较、逐轮及中段身份记录均有对应证据。严格 OpenSpec 校验通过（退出值 0，输出 `Change 'consume-yolo-result-before-request' is valid`），检查日志：`/home/mzh/nickluo/drone_tracker-validation/consume-yolo-result-before-request-20261020/openspec-final-validation.log`。所有任务勾选对应实际完成结果；历史失败继续保留。

未运行 Jetson 硬件验证、相机采集、飞行测试、带人工标注的准确率评价及长期生产负载测试。未配置或执行 clang-tidy/clangd 静态分析。主机交叉构建不替代板端；8 GiB 板实测也不替代 4 GiB 板的缓存可运行性验证。系统/环境观测来自有限快照，未证明长期无内存泄漏或无调度干扰。未设置性能门槛，因此不作性能通过、跨录像平均改善或必命中特定帧的结论。

最终原始证据、冻结配置、失败队列、服务恢复及限制已完整保留；正式入口仍为维护前版本。本报告不自动授权后续同步、归档、提交或生产发布。
