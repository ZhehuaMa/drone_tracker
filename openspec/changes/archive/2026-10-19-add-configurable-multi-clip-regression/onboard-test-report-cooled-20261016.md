# 生产板测试报告：加装风扇后的 test-03 完整批次

任务 2 验收失败：18 轮全部执行并最终化，16 轮合格。两轮超过未改变的 50 毫秒供帧迟到限制。准备预算均为一小时，未发生准备或总截止超时。按用户要求，任务 3 的正式验证及任务 4 的 CRITICAL 修复阶段不启动。OpenSpec 保持 29/31，任务 6.2、6.3 均未勾选。

## 本次范围及设备

本次仅测试指定叠框录像 `test-03-20260929-104206.叠框.mp4` 的三个独立区间；每项三轮新参考、三轮新候选，共十八轮。`test-02-20260929-1605.叠框.mp4` 按用户明确豁免，未再次准备、解码或回放，不能记为通过。四项配置及旧证据保留。

设备为 HZ-RK3576_WuKong Board，ADB 序列号 `5ceb49407502303d`，machine ID `1fe0a397f2ae4343896c7cf9d253a608`，当前启动 ID `937279a6-69b1-4465-9c4c-c81adb81a00c`。内核为 `6.1.141-rt36`，实测系统内存 8,306,733,056 字节。所有板端读写均仅经 ADB；没有使用 SSH、SCP 或其他网络访问。

前次温度保护关机已由前一启动内核日志证实，详见 `kernel-investigation-cooled-20261016.md`。用户报告重启并加装风扇后，本次采用全新的比较组，没有混入前次快轮、失败轮或旧预算结果。前次中断批次恢复了七轮已开始记录，六轮最终化、四轮合格；原始文件另存于外部证据的 `recovered-interrupted-results/` 和 `recovered-interrupted-control/`。

## 固定条件

参考组为 `ref-test03-cooled-p3600`，候选组为 `cand-test03-cooled-p3600`，板端输出为 `/home/hzhy/cvr16/t3c/`。准备 P=3600 秒、收尾 C=10 秒，在首轮前固定。总截止为 `P + ceil(D) + C`，D 是片段回放长度，向上取整只用于截止计算。三个区间的总截止分别为 3625、3640、3653 秒。

保持整片预解码及缓存像素校验；模型在全部像素验证后加载，随后建立回放时钟。保留 50 毫秒供帧迟到上限、32 帧队列及 5000 微秒时钟跳变容差。未新增固定缓存上限、流式回退、截短、重试或挑选快轮，也未带入先前仅针对 test-02 的 10% 内存诊断停止规则。CPU 允许范围 0–7，既有 performance 频率策略只读核对，未修改。

同一隔离 ARM64 构建用于两组，因此本次是工具和重复性检查，不能证明算法性能改进。423 个实际源码文件与当前工作树实现逐项相同；源码 SHA-256 为 `892ce6584c7a191022ac88144f4e7b4da0e35fc4046ca06ad1803235425fa9da`，测试程序 SHA-256 为 `0ffeba304ae0b30c36242545418de0d4b9daf7b2e6b37d3ff8db522ccde9805f`。提交、未提交差异和逐文件摘要见 `evidence/source-export.json` 及逐轮 `identity.json`。

使用已有 YOLO、NanoTrackV3 Backbone、NanoTrackV3 Head 模型。Backbone 的模板和搜索分别使用现有导出文件，具体模型路径与四个文件校验值见 `board-control/environment-smoke.json`，所有校验均与 fixture（不可变输入文件集）的冻结记录一致。系统 RKNN Runtime 2.3.2 及 RKNPU 驱动 0.9.8 保持不变；`/usr/lib/librknnrt.so` SHA-256 仍为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。

## 用例结果

所有区间均保持 1920×1200 灰度像素及原时间间隔。三个 fixture 的帧数分别为 900、1801、2575；尾段保留源末帧 12478，数值终点为 207.916789 秒。真实数据及配套记录位于 `/home/mzh/乌兰察布测试数据`，完整来源路径保留在 `maintenance/{ref,cand}.json`。

| 用例 | 参考合格 | 候选合格 | 选中帧数 | 正式比较 |
|---|---:|---:|---:|---|
| `wlcb-104206-020-035` | 2/3 | 3/3 | 900 | incomparable / not_evaluated |
| `wlcb-104206-100-130` | 3/3 | 3/3 | 1801 | comparable / not_set |
| `wlcb-104206-165-end` | 3/3 | 2/3 | 2575 | incomparable / not_evaluated |
| test-02 全片 | 用户豁免 | 用户豁免 | 本次未准备 | 未测试，未通过 |

每个片段的三轮参考和三轮候选都正常到达选中输入结束。正常结束只表示读到末帧，不能抵消供帧时序失败。

## 两轮失败的准确证据

供帧迟到表示帧实际送出时间减去录像时钟规定的计划时间；限值保持 50 毫秒。下面数值来自本次组原始 `input.jsonl`，不是前次 59.502437 毫秒失败的复用。

| 轮次 | 超限 fixture 帧号及迟到毫秒 | 全片覆盖 | 准备解码及验证 |
|---|---|---|---|
| `cand-test03-cooled-p3600/wlcb-104206-165-end/run-001` | 18: 70.493130；19: 56.645301 | read=2575，enqueued=2570，rejected=5，正常 EOF=True | 106.0530 秒，2575 帧验证通过 |
| `ref-test03-cooled-p3600/wlcb-104206-020-035/run-001` | 34: 107.088972；35: 91.886429；36: 76.580320；37: 61.727032 | read=900，enqueued=900，rejected=0，正常 EOF=True | 37.2286 秒，900 帧验证通过 |

EOF 表示已到所选输入末尾。短片失败轮全部 900 帧读入并入队；尾段失败轮读入 2575 帧，2570 帧入队、5 帧被队列拒绝。失败均保留为不合格，没有补跑。当前证据没有确定这些迟到的调度或驱动原因；不能把前次已证实的热关机原因套用到本次低温时序失败。

## 内存及温度观测

30 秒一次的连续采样共 77 次，六个热区的周期样本范围为 36.999–48.076°C。短片参考第 1 轮另有一次交互式状态采样，温度峰值 49.922°C；该采样保留在 `evidence/auxiliary-thermal-snapshot.json`。因此全部已观察采样的最高值为 49.922°C。这不是连续传感器的绝对峰值证明。新启动的测试前后 dmesg 没有临界温度关机、panic 或 OOM 记录，期间 ADB 未再中断。

| 热区 | 周期样本最低 °C | 周期样本最高 °C |
|---|---:|---:|
| `little-core-thermal` | 38.845 | 48.076 |
| `soc-thermal` | 36.999 | 45.307 |
| `gpu-thermal` | 38.845 | 48.076 |
| `ddr-thermal` | 36.999 | 46.230 |
| `bigcore-thermal` | 37.922 | 46.230 |
| `npu-thermal` | 36.999 | 45.307 |

周期样本的系统可用内存最低为 1,845,846,016 字节；测试进程 RSS（实际常驻物理内存）采样最高为 6,060,472 KiB。实际缓存像素字节数分别为 2,073,600,000、4,149,504,000、5,932,800,000；缓存不会跨轮借用。

## 冻结、比较与可重算性

短片参考只有两轮合格，冻结器正确拒绝该组。中片与尾段三轮参考分别生成新基线。基线是明确选定并冻结的参考文件集合；本次比较前后全部基线文件 SHA-256 一致。中片三轮候选可比较；没有配置性能门槛，判定保留 `not_set`，不是性能通过。尾段候选有一轮不合格，比较明确为 `incomparable`（缺乏满足正式条件的集合）。短片也明确为 `incomparable`，没有输出有效改进结论。

比较操作成功只表示报告生成成功，套件性能状态为 `not_passed`。报告不跨用例平均或以中片结果抵消另外两项失败。所有 18 轮指标均从原始二进制事件和输入事件重新计算，与留存指标 18/18 完全一致；16 个合格运行的完整文件校验及结构再次通过 `baseline.load_run`。见 `evidence/metrics-recomputed.json`、`evidence/qualified-checksum-audit.json` 和 `board-control/baseline-integrity.json`。

## 生产恢复与清理

独立 70 分钟恢复计时器在停止生产前已验证活动。板端维护 guard 仅在新计划轮次开始时续期，整体有 20 小时硬截止。用户此前明确批准的临时停产与恢复授权用于本轮，没有修改正式启动配置。

18 轮结束后先清理测试并恢复生产，再执行冻结、比较和主机重算。guard 报告 `resources_recovered=true`、`production_restored=true`，维护耗时 2277.7471 秒。最终服务为 active/running/enabled，PID 4888；维护服务及恢复计时器 inactive，无残留回归节点、worker、控制器或 guard。正式发布路径仍为 `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`；相机配置、CPU 配置、模型、系统 Runtime 及频率策略与运行前一致。`evidence/postflight-after-postprocessing.json` 通过。

## 证据位置及后续阶段

本报告对应的外部证据根目录为：`/home/mzh/nickluo/drone_tracker-validation/configurable-video-regression-task2-cooled-20261016`。

- `test-report.json`、`test-report.csv`：十八轮完整逐项结果及所有失败。
- `board-results/runs/`：独立逐轮原始二进制、输入事件、准备、身份、指标和截止证据。
- `board-results/baselines/`、`board-results/comparisons/`：逐用例冻结与比较 JSON/CSV/中文报告。
- `maintenance/`、`board-control/`：固定配置、维护守护、恢复、冻结、比较与环境记录。
- `kernel/`、`kernel-investigation-report.md`：前次热关机及当前启动日志证据。
- `recovered-interrupted-results/`、`recovered-interrupted-control/`：前次中断批次原样恢复记录。

任务 6.2 要求本次十八轮全部合格，本组只有十六轮合格，因此保持未完成。任务 6.3 与用户任务 3、4 均未执行。没有提交、推送、规格同步、归档或修改跟踪算法。
