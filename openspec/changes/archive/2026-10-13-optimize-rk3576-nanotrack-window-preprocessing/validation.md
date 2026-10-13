# RK3576 NanoTrack 局部窗口预处理验收记录

历史测量记录日期：2026-10-12（开发机日期）；W2 修复补充日期：2026-10-13。历史指定 25 帧重建输入的新旧模型输入逐元素精确一致，恢复输出与模型工作量一致，性能门槛通过。W2 指独立验收发现的性能失败自动化负例缺口，本轮补充内容见本文末尾。优化版本没有部署到正式服务。

## 结果与适用范围

A/B 对照仅切换预处理器，输入、模型、跟踪参数、CPU 核心与 OpenCV 线程设置相同。正式测量包含每条路径两批预热，以及十对交替顺序的完整恢复。每批执行一次初始化和 24 次按原时间戳顺序的更新。预热保留在原始记录中，但不进入统计。

| 一批恢复的指标 | 旧整帧路径 | 新局部窗口路径 |
|---|---:|---:|
| 预处理中位数 | 367.115402 ms | 7.278238 ms |
| 图像准备中位数 | 367.080902 ms | 7.177129 ms |
| 完整恢复中位数 | 573.712983 ms | 193.304605 ms |
| 完整恢复 P95 | 580.295609 ms | 197.284198 ms |
| 初始化中位数 | 17.864808 ms | 3.088349 ms |
| 24 次更新合计中位数 | 555.843505 ms | 190.034577 ms |

预处理比例为 **1.982548%**，低于要求的 20%；降幅为 98.017452%。完整恢复中位数降低约 66.31%，P95 未增加。各项中位数分别计算，初始化与更新的中位数不能相加来还原完整恢复中位数。

中位数对偶数样本取中间两项均值。P95 指约 95% 样本以内的耗时，本次采用排序后第 `ceil(0.95×N)` 项；十个正式样本下就是最大值。`Preprocess` 包含几何与图像准备；`ImagePrepare` 是图像准备子区间。父子区间不能相加当作总耗时。

原始数据为 [frames.csv](evidence/production-20261012/board-results/frames.csv)、[intervals.csv](evidence/production-20261012/board-results/intervals.csv) 和 [report.txt](evidence/production-20261012/board-results/report.txt)。共有 600 行逐帧结果、24 批恢复和 11256 个完整计时区间。`frames.csv` 的 `batch_ns` 在每行重复，复算时每批只取一次。

这项通过结论仅覆盖指定重建输入及本轮环境。录像包含有损压缩；原记录没有保存每次算法输入的相机帧身份，因此不能证明重建图像与现场未压缩相机帧逐像素相同。该结果没有还原当日完整并发负载，也不能替代现场约 388 ms 的分项测量或把它换算成优化后的绝对耗时。

## 板卡、模型、输入和代码身份

本次仅经 adb USB 访问序列号 `5ceb49407502303d`。设备树为 `HZ-RK3576_WuKong Board`，hostname 为 `hzhy`。系统为 Ubuntu 22.04.5 LTS ARM64，内核 `6.1.141-rt36`。编译器为 GCC 11.4.0，CMake 3.22.1，OpenCV 4.5.4，原生 Release 选项为 `-O3 -DNDEBUG` 和 C++17。

本轮系统可见内存为 7.7 GiB（`MemTotal=8112044 kB`），与 `AGENTS.md` 中记录的 3.8 GiB 不同。差异尚未解释；本轮保留实际读数，没有修改固定规格。设备树、正式服务及部署身份见 [测试前记录](evidence/production-20261012/before-current.txt) 和 [测试后记录](evidence/production-20261012/after-current.txt)。

RKNN Runtime 是 CPU 调用 NPU 的系统运行库。本轮使用 Runtime 2.3.2、RKNPU 驱动 0.9.8。系统 `/usr/lib/librknnrt.so` 的 SHA-256 为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。进程实际加载 `/lib/librknnrt.so`，工具确认其与 `/usr/lib/librknnrt.so` 为同一文件。没有传输或覆盖 Runtime；只从既有 2.3.2 SDK 提取 `rknn_api.h` 用于原生构建。

本次 NanoTrack 诊断实际加载三个 RKNN 文件。两份 Backbone 提取模板或搜索图像的特征，Head 结合两组特征输出预测。仓库“三个模型”的算法划分仍是 YOLO、NanoTrackV3 Backbone 和 NanoTrackV3 Head。

| 文件 | SHA-256 |
|---|---|
| `template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

指定输入只读复用既有恢复计时包的 `frames/000.png`～`024.png`，没有重新解码录像。原重建源码为 `09006db9ea11bd4fa137efc47ecb117660e0fad2`，录像 SHA-256 为 `11d45885dc32859c5aba16541ecc83f769ad47ba9ae66b54908b19d805d4c152`，视频索引为 4146～4170。初始框固定为 `[756.689,461.019,39.712,36.7552]`。时间戳沿用原 `fixture.csv` 的 `capture_timestamp_us`，没有按帧率推造。原 `image_sha256` 经历史分析脚本确认是解码后的 Gray8 像素指纹；25 张全部匹配。Gray8 表示每像素一个 8 位灰度值。

完整身份见 [原输入身份](evidence/production-20261012/input-identity-original.json)、[正式输入清单](evidence/production-20261012/board-results/input-manifest.yaml)、[配置](evidence/production-20261012/board-results/config.yaml) 和 [运行身份](evidence/production-20261012/board-results/identity.txt)。DCF（灰度相关滤波跟踪）关闭，但使用真实 NanoDcfTracker 的中心卡尔曼滤波；滤波按帧时间戳平滑中心。完整参数均保存在配置中。

实施基点为 `ea9cd822b9717b2759ece8e251bb977c3f88449b`，worktree 分支为 `codex/optimize-rk3576-nanotrack-window-preprocessing`。修改尚未提交。2026-10-12 的板端隔离源码由该 HEAD 的 Git bundle 加当时工作区覆盖得到。该轮测量诊断程序 SHA-256 为 `f5dfec99ccad0dd59ba70bed18dcf2fe261c0d06348bb030fc33ee1ddea2cdee`。程序内源码快照见 [code-snapshot.txt](evidence/production-20261012/board-results/code-snapshot.txt)；当时工作区与板端 289 个源码、头文件、CMake 和模板文件指纹全部匹配，见 [源码指纹](evidence/production-20261012/source-hashes.json) 和 [复核结果](evidence/production-20261012/source-hash-check.txt)。这些身份对应历史性能数据。本轮 W2 修复已修改诊断源码，当前代码身份另行保存，不能声明它与历史诊断二进制或全部源码指纹相同。

## 数值、生命周期和工作量证明

Float32 表示 32 位浮点，模型张量是带形状的数组。模板及搜索保持 `[1,3,127,127]` 和 `[1,3,255,255]` 的 Float32 NCHW 数据；NCHW 表示批次、通道、高度、宽度顺序。三个通道仍复制相同灰度，保留 `scale=1`、`offset=0` 和现有浮点线性缩放。

固定旧参考仅更改命名空间、类名及 FloatInput 引用；还原命名后，与基点 `.cpp` 逐字节相同，见 [参考身份复核](evidence/production-20261012/reference-identity.txt)。旧参考只链接测试和诊断，不进入生产节点或运行时回退。

生产板数值测试覆盖奇偶窗口、分数中心、四侧/四角、全图外、大于原图、边长 1、上/下采样、渐变、常量、棋盘格、固定随机图和非连续行。旧路径逐元素精确对照通过。原双精度四邻点参考及 `5e-4` 门槛仍独立运行；四邻点指缩放使用的相邻四个源像素，预期裁剪坐标直接固定，不由生产几何函数生成。

本次棋盘格测试使用 0/127 灰度，固定随机图覆盖 0～255。先前本地实施额外尝试的 511 边长随机图和 257 边长 0/255 高反差棋盘格，在旧 OpenCV 路径也超出理想双精度参考的 `5e-4` 舍入门槛；这些未作为独立参考验收用例保留。没有改变生产缩放或放宽新旧精确对照。本轮完整数值矩阵及真实恢复张量对照通过，但不能证明任意高反差、大尺寸输入都满足理想双精度误差门槛。

生命周期测试覆盖工作区旧像素、目标均值变化、114 回退、非法映射、InputBusy、源帧释放和预处理器销毁。InputBusy 表示消费者仍持有输出时拒绝覆盖；输出租约保证最后一个消费者释放前数据有效。模板失败仍使旧均值失效，搜索失败仍保留已提交均值，全图外窗口仍校验映射。真实 HybridTracker 的受控回归覆盖初始化失败、中间补帧失败后继续和恢复成功。

正式测量前，额外运行一次真实恢复。每次准备同时比较新路径与旧参考的实际模型输入，全部 25 个张量、**4730187 个元素精确一致**，见 [exact-inputs.txt](evidence/production-20261012/board-results/exact-inputs.txt)。原始窗口边长为模板 76；搜索依次为 154、151、148、147、146、146、145、144、144、144、144、144、145、146、145、145、145、146、146、148、149、150、150、149。该比较及其模型执行位于正式预热/测量之外。

24 批恢复各帧成功标记、初始化状态、框、分数和时间戳新旧精确相同。每批模板执行 1 次、搜索及 Head 各 24 次。模型次数由实际 RKNN `Run` 区间核对，Run 是后端调用 NPU 执行的区间。模板与 Head 分别由其祖先阶段识别；现有搜索特征直通路径中的搜索 Run 直接属于 `Inference`。没有更改生产后端的区间含义，也没有把推定的 24 次填入报告。独立复算脚本再次从 1176 个真实 Run 事件核对 1/24/24。

## 构建与失败复验

| 检查 | 本轮证据 |
|---|---|
| ARM64 容器完整 ROS 构建和 CTest | 既有 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 镜像，`./build.sh --test`；最终 47/47 通过，见 [local-build-final3.log](evidence/production-20261012/local-build-final3.log) |
| 生产板原生诊断构建 | 使用系统 RKNN 库；构建成功，首次 CTest 19/20，见 [native-build-retry.log](evidence/production-20261012/native-build-retry.log) |
| 源码身份增量测试复验 | 原断言未改，限定未来时间系统依赖的递归 Make 包装器后 1/1 通过，见 [复验日志](evidence/production-20261012/native-identity-retest-recursive.log) |
| 最终诊断修改后的板端相关复验 | 刷新源码身份并按 CMake 原编译和链接命令原生重编译；数值/租约、恢复回归、计数适配和入口 4/4 通过，见 [native-diagnostic-final-pass.log](evidence/production-20261012/native-diagnostic-final-pass.log) |
| OpenSpec 严格验证与 diff | 最终执行 `openspec validate optimize-rk3576-nanotrack-window-preprocessing --strict` 和 `git diff --check`，结果见最终审查记录 |
| clang-tidy / clangd | 镜像未提供，仓库未配置对应命令；未执行，不宣称静态检查完成 |

生产板墙钟为 2026-08-11，落后于部分 `/usr` 头文件和库的修改时间。Make 因此在无源码改动时仍重编译，导致源码身份增量测试的时间戳断言失败。没有修改系统时钟或测试断言。首先直接传入 Make 的 `--assume-old` 参数复验仍失败，因为该选项不传给递归 Make。最终在测试的隔离构建中使用 [immutable-system-make](evidence/production-20261012/immutable-system-make)，每层仅把列出的未来时间系统依赖视作未改变。精确参数见 [系统依赖列表](evidence/production-20261012/identity-test-immutable-future-deps.txt)。源码和生成身份文件未被排除；修改、新增、删除、暂存、提交及 Git 失败等原断言继续运行。首次 19/20 与复验 1/1 分开记录，不能称为一次无条件 20/20。

保留的失败过程还包括：首次隔离源码解包保留宿主 uid，触发 Git 安全所有权检查；只修正隔离目录所有者后继续。第一次运行脚本在 ROS 环境加载阶段因 `set -u` 失败，此时尚未停服务。诊断增量编译出现的变量重名和静态归档重复成员问题均已修正，失败与最终通过日志保留。

首次真实恢复 `run01` 的张量和逐帧框/分数均一致，但报告失败。原因是诊断误把 `SearchBackbone` 外层区间数当作模型次数；现有特征直通路径没有这个外层区间。失败数据见 [failed-run-01](evidence/production-20261012/failed-run-01)。修正仅在诊断统计层，改为计实际 Run 并加入回归，再以新输出目录完整重跑 `run02`。失败轮不混入最终性能统计。

## 其他窗口与环境

其他窗口使用第一张真实 1920×1200 Gray8 图、固定中心 `(776.045,478.8966)`，模板与搜索分别输出 127×127 和 255×255。每条路径各两次预热、十对交替测量，得到 192 份原始记录。它只测预处理，不执行模型，不改变指定恢复的初始框。全部新旧张量精确对照通过。

| 原始边长 | 旧模板中位数 ms | 新模板中位数 ms | 旧搜索中位数 ms | 新搜索中位数 ms |
|---:|---:|---:|---:|---:|
| 1 | 15.385199 | 0.385480 | 15.266219 | 0.126569 |
| 32 | 15.559722 | 0.400879 | 15.220197 | 0.151605 |
| 77 | 15.494113 | 0.423959 | 15.251706 | 0.185196 |
| 154 | 15.751705 | 0.474398 | 15.585012 | 0.241144 |
| 255 | 15.679322 | 0.556566 | 15.548773 | 0.176801 |
| 511 | 15.601203 | 0.786646 | 15.752668 | 0.774134 |
| 1024 | 3.597132 | 2.366344 | 3.642373 | 2.430387 |
| 2048 | 4.334615 | 5.913070 | 4.577415 | 6.027726 |

2048 边长的新路径比旧路径慢。指定小窗收益不能推广到所有窗口；原始参数和每对数据见 [other-windows.csv](evidence/production-20261012/board-results/other-windows.csv)。

测量固定 CPU 4 和 OpenCV 单线程，没有修改调频策略。51 份环境快照中 CPU 4 为 2208 MHz，CPU 0 策略组为 2016 MHz，NPU 为 950 MHz；thermal_zone0 为 47.153～49.922℃。采样不能证明两个采样点之间没有瞬时变化。每批前后 CPU/NPU 频率、温度、调频策略、CPU 计数和负载见 [环境快照](evidence/production-20261012/board-results/environment-snapshots.txt)，并发进程见 [concurrent-processes.txt](evidence/production-20261012/concurrent-processes.txt)。APM 桥接和拦截服务继续运行。

## 正式服务恢复及未运行检查

依据本轮授权，测量前临时停止 `drone-tracker.service`。外层脚本设置 EXIT/信号恢复，并设板端十分钟独立恢复定时器，以应对 USB 或 shell 异常。正常恢复后取消该定时器。实际失败轮退出后服务恢复为 active/running、PID 5736；最终成功轮退出后恢复为 **active/running、PID 5959**，收尾再次核实。证据见 [保护脚本](evidence/production-20261012/run-guarded.sh)、[停止记录](evidence/production-20261012/service-stopped.txt)、[恢复记录](evidence/production-20261012/service-restored.txt) 和 [收尾记录](evidence/production-20261012/after-current.txt)。

正式 current 仍为 `/opt/drone_tracker/releases/bundle-20260927T075028Z-3213`。正式节点 SHA-256 仍为 `3e58d3b61932d31771c2f12c196d55b96638317f48ca26e083122726f4e1f034`，配置仍为 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`。三个模型和系统 Runtime 指纹测试前后相同。没有切换正式二进制、修改配置或发送相机 ON；无诊断测试或编译进程遗留。

未执行：Jetson 实机等价性、现场未压缩相机输入与完整负载重现、长时间压力、NPU 设备内存测量、正式部署、规格同步及归档。以上不属于本次窗口优化的通过范围。

## 独立复算入口

完整可携带证据位于本变更的 `evidence/production-20261012/`，不包含模型和大图二进制。本地输入和构建产物额外保存在 worktree 的 `.docker-artifacts/production-window-20261012/`。隔离板端目录为 `/tmp/nanotrack-window-20261012/`；正式部署位于原 `/opt/drone_tracker/`。

从本仓库根目录运行以下命令，使用标准 Python 库即可核对逐帧顺序、输出、调用数、原始 Run 事件、区间完整性、预热排除、中位数和 P95，并生成 `analysis.json`：

```bash
python evidence目录/analyze-results.py
```

其中 `evidence目录` 替换为 `openspec/changes/optimize-rk3576-nanotrack-window-preprocessing/evidence/production-20261012`。已有复算结果见 [analysis.json](evidence/production-20261012/analysis.json)。该命令不访问开发板、不加载模型、不改变输入或正式服务。

## 2026-10-13：W2 性能失败自动化验证补充

本轮只补充诊断层的验证。`performanceVerdict` 是正式诊断与测试共用的性能判定函数，位于 `window_preprocess/performance_verdict.cpp`。`main.cpp` 将同一次判定生成的 `report` 写入 `report.txt`，将 `status` 写入 `status.txt`，并返回 `exit_code`。抽取前后的门槛、报告格式和失败退出码一致。数值与工作量的前置检查保持原流程。

固定统计用例将旧预处理中位数设为 100，旧恢复中位数设为 100，旧恢复 P95 设为 120，单位为纳秒。这些数值仅用于判断分支，不是性能采样。例如，新预处理中位数为 30、新恢复中位数为 80、新恢复 P95 为 110 时，比例为 0.30，函数必须生成失败报告、状态“性能门槛未通过”和退出码 1。

| 固定用例 | 期望外部结论 |
|---|---|
| 比例 0.30；其余条件满足 | 失败，退出码 1 |
| 恢复中位数相等或增加；其余条件满足 | 分别失败，退出码 1 |
| 恢复 P95 增加；其余条件满足 | 失败，退出码 1 |
| 比例恰好 0.20；其余条件满足 | 通过，退出码 0 |
| 预处理中位数比 20 大一个可表示浮点步长 | 失败，退出码 1 |
| 恢复中位数比 100 小一个可表示浮点步长 | 通过，退出码 0 |
| 恢复 P95 相等 | 通过，退出码 0 |
| 恢复 P95 比 120 大一个可表示浮点步长 | 失败，退出码 1 |
| 比例 0.18、恢复中位数 80、P95 110 | 通过，退出码 0 |

合计 10 个用例。`std::nextafter` 取得相邻可表示浮点数，用来区分“严格降低”与“允许相等”。报告继续保留原有输出精度，因此边界数值在文字中可能显示为同一个整数；断言使用实际浮点值，子进程核对相应结论和退出码。测试没有复制门槛表达式，没有修改采样数据。

`nanotrack_window_performance_tests` 检查共用函数的判定、报告统计字段、结论文字、状态文字和退出码。`nanotrack_window_performance_exit_tests` 另外启动 10 个真实子进程，从操作系统取得退出状态；6 个失败用例均为 1，4 个通过用例均为 0，报告与状态一致。

| 本轮检查 | 结果和证据 |
|---|---|
| 既有 ARM64 Runtime 2.3.2 容器，Release 构建诊断及相关测试 | 成功；6/6 相关 CTest 通过，见 [最终构建与详细测试日志](evidence/w2-20261013/local-build-test-final.log) |
| adb 设备 `5ceb49407502303d`，`hzhy` 的 `/tmp/nanotrack-window-w2-20261013/` 隔离运行 | 10 个固定统计断言通过；10 个实际子进程退出状态与报告通过，见 [板端测试日志](evidence/w2-20261013/board-test.log) |
| 当前改动文件、测试程序和诊断程序身份 | 独立保存于 [当前身份](evidence/w2-20261013/current-code-identity.txt)，历史哈希和原始性能数据未覆盖 |
| OpenSpec 严格验证及 diff 空白检查 | 通过，见 [最终检查](evidence/w2-20261013/final-checks.txt) |
| clang-tidy / clangd | ARM64 容器未提供，仓库未配置对应检查命令；未执行匹配目标环境的静态工具检查 |

容器执行命令见 [run-local.sh](evidence/w2-20261013/run-local.sh)，从仓库根目录用 `bash` 运行。板端只通过 adb 传入独立测试二进制和退出状态检查脚本，不覆盖历史测量源码目录或正式安装目录。板端使用已有系统库；没有加载真实模型，没有重新测量十对 NPU A/B，没有停止正式服务。测试前后服务仍为 active/running、PID 6107，见 [开始状态](evidence/w2-20261013/service-before.txt) 和板端测试日志。

本轮不调查或修改 W1 内存差异。上述历史性能结果继续归于原实测设备和代码身份；它们不代表新诊断程序重新完成了完整性能采样。W2 的修复证据已补充，正式独立重新验收及整轮结束的正式服务重启交由随后独立验收流程完成。本轮没有提交、部署、规格同步或归档。
