# 验收记录：NanoTrackV3 搜索特征直连性能测试

本文件汇总本变更的全部检查、命令、数据身份和结果。首次验收日期为 2026-09-20，持续负载于 2026-09-21 补齐，实机均为**开发板 2**（`ssh firefly2`）。未执行的检查在第 8 节明确列出，不标记为通过。

## 1. 证据分层

本变更有两层证据，必须分开理解：

- **前期 Python 探针**（第 2 节）：用 `ctypes` 直接调用 RKNN Runtime 做的可行性验证。它只证明"这种共享内存用法在该 Runtime 与该组模型上可行，以及哪种绑定顺序可靠"。
- **正式 C++ 实现证据**（第 3–7 节）：新增程序 `nanotrackv3_search_direct` 自身的构建、受控测试和实机验收。

第 2 节的探针结论不能替代第 3–7 节；第 3–7 节的结论也不依赖探针的临时目录继续存在。

## 2. 前期 Python 探针（可行性验证）

| 项目 | 内容 |
|---|---|
| Runtime / 驱动 | `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` / `0.9.8` |
| 样本量 | 3 个独立进程 × 240 次 = 720 次 |
| 结果 | head 两项输出与独立普通参照逐元素相同，最大绝对误差均为 0 |
| 参照独立性 | 每进程产生 6 种不同参照输出组合，具备输入敏感性 |

被否定的顺序：只在初始化绑定搜索输出（72 次全失败，诊断读到全零）；只在初始化绑定 head 搜索输入（0/24）；在模板提交之前重新绑定（23/24）。通过的顺序：两端都在各自普通提交之后重新绑定（24/24，再由 720 次重复确认）。

原始数据位于开发板 `/tmp/nanotrack-search-direct-check-20260920/`，本机副本 `/private/tmp/nanotrack-search-direct-check-20260920/`。这两个都是临时目录，可能被清理；结论已固化于本文件与 `design.md`。

## 3. 数据与程序身份

三份模型、录像与共享库的 SHA-256 在验收开始与结束时各核对一次，均未改变：

| 项目 | SHA-256 |
|---|---|
| template backbone | `c0002d937a7a414a7ef829b206f1674e0aff3311bf7bd5514baaa0106c2e7059` |
| search backbone | `8985bc1f825dac3ac28bd1805abfee2cc81112acf8f3945cd495c9d325b4c754` |
| head | `f93e5c0e0b2a2a8bfe0425d9d43e6f0a6659a6464a9b09d94d35f21fa64f15bd` |
| 录像 `input.mov` | `bc0f21ed1cbdf0ab8b7399d295fbea8bbb0587c9ec74bf192067f8efdf67cc3b` |
| 系统 `/usr/lib/librknnrt.so` | `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` |

三份模型与已归档 baseline `detailed-006` 使用的完全一致。

运行环境：Ubuntu 22.04 ARM64、内核 `6.1.141-rt52`、Runtime `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`、驱动 `0.9.8`、系统库 `/usr/lib/librknnrt.so`（未被容器 SDK 覆盖）。CPU 调频策略为 `interactive`，NPU 为 `rknpu_ondemand`；按规格要求未提权修改频率。

程序身份（2026-09-20～21 实机验收所用的修复前历史快照，不代表 WARNING 修复后的当前工作树）：

| 项目 | 值 |
|---|---|
| `code_commit` | `17ec90ec361c3979eaf03b014f440124182807e2` |
| `code_snapshot_sha256` | `b94286a1b2ea27e8f37143e2fb255558c2d558b298e23d1b9887b6b3f1b77052` |
| `nanotrackv3_search_direct` | `ccd6c53caa3512b5627b7a60c453c0c199398eb6a1bfa4d41ef3a53479d71365` |
| `nanotrackv3_baseline` | `3f283d9dc2a5eb8b06812a554029cc93ae00a1cc3e31226fab5d6619004eb73f` |
| `nanotrackv3_search_direct_probe` | `600535f3c8dcd61a4554bfcd5c74ae8315369903698f9a926594c532b74f3d06` |
| `nanotrackv3_benchmark_compare` | `dcb0919c8848c0e75023d4f95455224f0c093e8a8346843f9080cef3c0ff5caa` |

源码快照由 `generate_code_identity.cmake` 按提交差异加未跟踪源码生成，已覆盖新增的 `search_direct/` 目录。历史验收时曾独立重算并与实机记录核对一致。第 10 节随后修复了代码，因此以上实机数据只对应本表的历史快照，不能用来声明当前工作树已经实机复验。

首帧框 `[124.92928, 22.944768, 75.84896, 39.202816]`，两侧完全相同。

验收目录：开发板 `/tmp/nanotrack-search-direct-acceptance-20260920/`（新建的隔离目录，未覆盖系统库或历史 baseline）。

### 证据归档

仓库内副本见 [`evidence/board2-20260920/`](evidence/board2-20260920)：

| 文件 | 内容 |
|---|---|
| `records.tar.gz` | 全部运行的原始记录：每个运行的 `manifest.json`、`summary.json`、`report.md`、`frames.csv`、`environment.csv`、`diagnostics.log`、所用配置与 `code_snapshot.txt`；数值验收与每种观测模式首轮另含 `spans.csv`；20 次启停循环的记录；全部配置与执行脚本 |
| `compare.json` / `compare.md` | 性能比较工具的完整输出 |
| `probe-1.json`～`probe-3.json` | 三个独立进程的实机数值探针结果 |
| `cycles.csv` | 20 次创建/初始化/reset/销毁的退出码与资源采样 |
| `endurance-resources.csv` | 持续回放的资源采样（本次被人为中止，见第 8 节） |

`records.tar.gz` 的 SHA-256 为 `c31194cfc0831677be2ebdc63029930ac72b645f399fad1e8976352a0670b903`。归档后结论不再依赖开发板 `/tmp` 继续存在。

## 4. 构建与受控测试

两种构建入口都在 `AGENTS.local.md` 指定的 ARM64 镜像 `roc-rk3576-ros2-build:humble-arm64` 中执行。

| 检查 | 命令 | 结果 |
|---|---|---|
| 仓库构建与全部 CTest | `./build.sh --test` | 通过，29/29 |
| 新目录独立构建（无 ROS） | `cmake -S tests/nanotracev3_performance_benchmarks/search_direct -B build-search-direct -DDRONE_TRACKER_BUILD_TESTS=ON …` | 通过 |
| 独立构建的 CTest | `ctest --test-dir build-search-direct` | 通过 |
| 独立入口可用性 | `nanotrackv3_search_direct --help`、`nanotrackv3_search_direct_probe --help` | 通过 |

受控测试用 RKNN 设备替身，模拟两条实机事实：绑定只指定内存不复制数据；任何普通输入提交都会让已建立的绑定失效。

| 测试 | 覆盖内容 |
|---|---|
| `search_direct_direct_backend_tests` | 原生属性准入（零跨度规范化、类型/排列/分组/容量/量化不符）、角色索引重排与重复角色、直连数值与独立普通参照逐元素一致、模板切换与 A/B/A 搜索交替、诊断读取共享特征、模型调用次数、错误绑定顺序被检出、reset 与模板失效 |
| `search_direct_direct_failure_tests` | 分配、导入、原生属性查询、绑定、运行、输出取回与释放失败；失效后端不得继续执行；无重复释放；停止无法确认与释放失败的致命路径在子进程中验证 |
| `search_direct_runner_tests` | 两条路径同配置完整运行、身份字段、阶段归属、记录独立重算、逐帧数值对照、关闭细分观测、绑定失败的失败记录；比较工具接受完整同身份对照，并拒绝分组缺失、轮次不足、变体标记不符与首帧框不同的输入 |
| 原 baseline 测试 | 配置、runner、模型、解码、记录与代码身份测试全部通过，共用框架扩展未改变基线行为 |

版本差异与缺口：构建镜像内 RKNN SDK 为 **2.3.0**，实机 Runtime 为 **2.3.2**、驱动 **0.9.8**；容器构建通过不是实机证据。镜像未提供 `clang-tidy` 与 `clangd`，仓库也没有配置静态检查命令，**静态检查为明确缺口，本次未执行**。

## 5. 实机数值验收

### 5.1 原生属性与共享身份

程序实测查询到的属性与设计记录完全一致：

```
search 原生输出：[1,12,16,16,8] FP16 NC1HWC2 elems=24576 bytes=49152 w_stride=0  effective_w_stride=16
head   原生输入：[1,12,16,16,8] FP16 NC1HWC2 elems=24576 bytes=49152 w_stride=16 effective_w_stride=16
```

两端 `w_stride` 的字面值不同（0 与 16），按 SDK 约定有效跨度相同。程序按有效值判定为兼容——这正是 `spec.md` 中"字面跨度不同但实际兼容"的场景在实机上真实出现，说明不能按字段字面值比较。

共享内存文件描述符记录为 `shared_feature_fd=17/17`，即 head 的导入对象与 search 的原始对象引用同一块存储。角色索引实测为 `template_input=0 search_input=1 classification_output=0 location_output=1`，由形状识别得出，未写死。

### 5.2 原始输出的独立参照对照

工具 `nanotrackv3_search_direct_probe` 在实机上逐元素比较三条路径的 **head 两项原始输出**（未经后处理的分类与位置张量）：

1. 正式程序的"先 search 后 head"顺序；
2. 已通过实机验证的探针顺序（head 提交模板 → head 绑定搜索输入 → search 提交图像 → search 绑定输出 → 执行 search → 执行 head）；
3. 完全独立的普通 search 会话加普通 head 会话参照，不使用被测共享缓冲区生成自身输入。

输入为固定种子的 Float32 灰度图并复制到三通道，保留小数。模板种子 11、29；搜索种子 101、202、303；搜索按 A/B/A/C 交替，每四次切换模板。

```sh
./bin/nanotrackv3_search_direct_probe --template-model … --search-model … --head-model … \
  --iterations 240 --output logs/probe-1.json
```

三个独立进程各 240 次，结果完全一致：

| 指标 | probe-1 | probe-2 | probe-3 |
|---|---|---|---|
| 不一致样本 | 0 | 0 | 0 |
| 非有限值样本 | 0 | 0 | 0 |
| 正式顺序对参照的最大绝对误差 | 0 | 0 | 0 |
| 探针顺序对参照的最大绝对误差 | 0 | 0 | 0 |
| 模板切换次数 | 60 | 60 | 60 |
| 不同的参照输出组合数 | 6 | 6 | 6 |

参照输出有 6 种组合，说明"逐元素相同"不是因为输出恒定。正式顺序与已通过的探针顺序在实机上给出完全相同的原始输出，因此可以断言两套调用顺序等价。

### 5.3 完整回放数值对照

命令（每次只改输出目录）：

```sh
./bin/nanotrackv3_baseline      configs/numeric-baseline.yaml
./bin/nanotrackv3_search_direct configs/numeric-direct-1.yaml   # 同样执行 -2、-3
```

配置：`warmup_updates: 30`、`max_updates: 240`、`repeats: 3`、`detailed_observation: 1`。每个进程因此有 3 次模板建立和 3×240=720 次正式更新。

| 对照 | 结果 |
|---|---|
| 4 个运行各自的独立重算 | 通过，各 720 次有效更新、0 次失败 |
| direct-1/2/3 对 baseline 逐帧 | `max_bbox_error=0`、`max_score_error=0` |
| direct-2、direct-3 对 direct-1 | `max_bbox_error=0`、`max_score_error=0` |
| 结果列逐行比较 | 3 个直连进程与 baseline 的 754 行帧身份、框、分数、状态列完全相同 |
| 输入敏感性 | 720 次正式更新共产生 241 种不同的框/分数组合 |

框误差与分数误差均为 0，优于 `1e-4` 像素与 `1e-6` 的验收上限。模型调用次数由 `baseline_verify_records` 核对：每次初始化只执行一次模板模型，每次更新只执行一次搜索模型和一次 head。

### 5.4 阶段归属

直连运行的 `spans.csv` 中，`search_backbone` 下有 `inputs_set`、`io_mem_bind`、`run`，**没有** `outputs_get`、`output_copy`、`outputs_release`、`normalize`；`head` 下有 `inputs_set`、`io_mem_bind`、`run`、`outputs_get`、`output_copy`、`outputs_release`、`normalize`；`template_backbone` 保持普通路径且没有 `io_mem_bind`。普通路径运行则相反。父子区间与 `residual_ns` 由核对工具独立重算通过。

## 6. 资源验收

### 6.1 重复创建、初始化、reset、销毁

执行 20 次独立进程运行，每次建立后端、完成 4 次初始化（1 次预热轮加 3 次正式轮，每轮开始都 reset）后销毁，并在进程存活期间每 0.2 秒采样。

| 指标 | 结果 |
|---|---|
| 退出码 | 20 次全为 0 |
| 每次初始化次数 | 均为 4 |
| 进程内最大打开文件描述符数 | 恒定 25，无增长 |
| 峰值常驻内存 `VmHWM` | 51848–51868 kB，波动 20 kB 内，无趋势性增长 |
| 结果一致性 | 20 次的帧结果列完全相同 |

原始数据：[`evidence/board2-20260920/cycles.csv`](evidence/board2-20260920/cycles.csv)，每次运行的完整记录在 `records.tar.gz` 的 `pack/runs/cycles/`。

### 6.2 首次持续回放（未达到时长）

按 `repeats: 320`、`max_updates: 300`、`detailed_observation: 0` 启动，运行约 **301 秒**后按要求人为中止，**未达到 30 分钟**。

已取得的部分观测：进程状态记为 `interrupted`/`signal`（说明信号处理路径正常收尾并保留了记录），已写出 14199 帧记录；运行期间打开文件描述符恒定 25，`VmRSS` 恒定 63844 kB，`VmHWM` 恒定 77304 kB，线程数恒定 1，CPU 温度 46.2–50.8 ℃。

这些数据**不足以**作为持续负载验收。后续验收使用第 6.3 节的新记录，不将本次中止记录改写为通过。

### 6.3 2026-09-21 持续回放补充验收

验收目录为开发板 `/tmp/nanotrack-search-direct-acceptance-20260921/`。接续检查时，该目录中的正式程序已运行约 25 分钟；没有重新启动或并发运行另一项推理负载。程序、源码快照、三份模型、录像和系统 Runtime 文件指纹均与第 3 节一致。运行后再次核对文件指纹，全部一致。

配置沿用相同输入与首帧框，设置 `warmup_updates: 30`、`max_updates: 300`、`repeats: 600`、`detailed_observation: 0`。用户明确选择满 30 分钟后有序停止，而非等待全部 600 轮。因此向核验过可执行路径的 PID 2362 发送 `SIGINT`（请求程序完成当前帧并收尾的中断信号）。**退出码为 130，manifest 和 summary 保留 `interrupted` / `signal`，不是自然完成 600 轮。**

关键命令如下，完整配置和监控脚本均包含在证据归档中：

```sh
cd /tmp/nanotrack-search-direct-acceptance-20260921
# monitor.py 启动正式程序，每 10 秒记录进程资源。
python3 monitor.py
# 另一个终端确认采样时长超过 1800 秒、PID 和可执行路径一致后：
kill -INT 2362
./bin/baseline_verify_records runs/endurance
sha256sum -c logs/identity-before.txt
```

原监控脚本要求自然退出码为 0，因此会把此次退出码 130 判为不满足其退出条件；未修改或掩盖这个条件。该脚本的结果仅用于记录退出码和时长。本次验收以用户授权的受控停止、实际帧处理时间跨度、落盘记录核对及逐帧独立参照作为判据，不将监控脚本称为通过。

| 检查 | 结果 |
|---|---|
| 首帧计时开始至末帧计时结束 | **1831.708 秒（30 分 31.708 秒）**，排除仅靠启动或收尾等待凑足时长 |
| 监控进程记录的总时长 | 1842.070 秒，含最多一个采样周期的退出检测等待 |
| 落盘记录核对 | 87,597 条帧记录、87,597 条计时区间，2336 项统计核对通过 |
| 正式更新 | 87,275 次，0 失败帧 |
| 轮次覆盖 | 290 个完整正式轮次，加第 291 轮的 275 次更新；另有 31 条预热帧记录 |
| 旧帧污染检查 | 全部 87,597 帧按输入帧号对照独立普通路径 `perf-baseline-total-1`；框最大误差 0，分数最大误差 0 |
| 帧序列检查 | 每轮从帧 0 连续递增，无缺帧、重复或错序；初始化及 300 种更新输入均有普通路径参照 |
| 历史启停复核 | 第 6.1 节的 20 次启停记录重新与同一普通路径逐帧对照，通过；本次未宣称重新执行 20 次进程创建 |
| 运行后状态 | 目标进程已退出，程序、模型、录像、系统 Runtime 指纹均未改变 |

资源观测共 184 个样本，包含启动、182 个稳定负载样本及收尾样本。FD 是进程打开的文件描述符数量；`VmRSS` 是当前常驻主机内存，`VmHWM` 是其历史峰值：

| 指标 | 稳定负载阶段 | 收尾样本 |
|---|---|---|
| FD | 恒定 25 | 9 |
| `VmRSS` | 恒定 63,460 kB | 56,388 kB |
| `VmHWM` | 恒定 76,920 kB | 76,920 kB |
| 线程数 | 恒定 1 | 1 |
| CPU 温度 | 43.461–49.923 ℃ | 49.923 ℃ |

未观察到主机资源随时间增长或已记录的推理错误。设备侧 `/sys/kernel/debug/rknpu/mm` 仍因权限不足不可读，此缺口保留；主机指标稳定不证明设备侧绝对无泄漏。

持久化证据见 [`evidence/board2-20260921/`](evidence/board2-20260921/)：

- `records.tar.gz` 保存完整配置、原始帧与区间、环境记录、身份、诊断、监控脚本、资源采样、受控停止时间及记录核对输出。SHA-256 为 `f8cc0d324dd014265dc481deb8108e3fdc847eafa22abc90804c4543f99b2b45`。
- `verify_endurance.py` 直接读取新旧两个归档，核对数据身份、帧序列、数值容差、历史启停记录和实际负载时长，不依赖开发板临时目录。
- `verification.json` 保存上述脚本的结果。在仓库根目录运行 `python3 openspec/changes/add-nanotrackv3-search-direct-benchmark/evidence/board2-20260921/verify_endurance.py` 可重新计算。本次使用本机 conda base Python 执行。

## 7. 性能对照

方法：同一块板、同一组模型与录像，交替执行 baseline 与新程序，细分观测开启与关闭各 **8 轮**，每轮 `warmup_updates: 30`、`max_updates: 300`、`repeats: 3`，即每组 8 次运行 × 900 次正式更新 = 7200 个样本。

比较工具先核对板号、机器、系统与内核、主机名、CPU 亲和性、录像指纹、三份模型指纹、RKNN 库路径与指纹、精度校验状态、提交号与源码快照指纹、编译选项与编译器、依赖库版本、PTS 时间基、Runtime/驱动版本、首帧框、预热次数、更新上限、轮次与环境采样间隔，并要求四个分组都存在且每组不少于 3 轮。实测拒绝行为：

- 只传入细分观测的运行 → `分组 baseline/total_only 只有 0 轮，少于要求的 3 轮`，退出码 1。
- 传入更新上限不同的运行 → `不满足单变量对照：config.max_updates …`，退出码 1。

`decode_to_result` 延迟（单位毫秒，每组 7200 样本）：

| 观测模式 | 侧 | 均值 | P50 | P90 | P99 |
|---|---|---:|---:|---:|---:|
| 细分观测开启 | baseline | 21.938 | 21.454 | 26.977 | 33.473 |
| 细分观测开启 | search_direct | 21.444 | 20.662 | 25.759 | 32.772 |
| 仅总计时 | baseline | 21.587 | 21.221 | 27.222 | 34.635 |
| 仅总计时 | search_direct | 21.597 | 21.039 | 26.157 | 33.815 |

**端到端结论：没有确认稳定的性能收益。** 细分观测开启时直连快 0.494 ms（倍数 1.023），仅总计时时直连慢 0.011 ms（倍数 0.9995），两种模式符号相反。观测开销的估计值分别为 baseline `+0.351 ms`、直连 `−0.154 ms`，出现物理上不成立的负值，说明运行间波动已经超过被测效应。单轮均值实测跨度为 15.6–27.5 ms，而 CPU 使用 `interactive`、NPU 使用 `rknpu_ondemand` 动态调频，按规格不得修改。所有样本原样保留，没有删除慢帧或低分帧，也没有设置事后速度门槛。

阶段级成本（细分观测开启，均值，单位微秒）可以单独测量，仅说明工作量的变化，**不用于推导端到端收益**：

| 项目 | baseline | search_direct | 差 |
|---|---:|---:|---:|
| 搜索模型 `outputs_get` + `output_copy` + `outputs_release` + `normalize` | 248.79 | 未执行 | −248.79 |
| 搜索模型 `io_mem_bind` | 未执行 | 80.41 | +80.41 |
| head `io_mem_bind` | 未执行 | 24.95 | +24.95 |
| head `input_prepare` + `inputs_set` | 1076.63 | 279.27 | −797.37 |

head 侧变化最大，原因是直连路径每帧只普通提交模板一个输入，而 baseline 要提交模板与搜索两个输入。把这些阶段直接相加会得到约 0.94 ms，与实测端到端差值并不一致，这正是不能用父子阶段加和推导收益的实例。

原始结论数据：[`evidence/board2-20260920/compare.json`](evidence/board2-20260920/compare.json)、[`compare.md`](evidence/board2-20260920/compare.md)；32 次运行的完整原始记录在 `records.tar.gz` 中。

## 8. 缺口与边界

已明确未执行的检查及验收边界：

1. **受控停止边界**：第 6.3 节已补齐至少 30 分钟持续负载、资源趋势和逐帧参照；没有执行完配置中的全部 600 轮。首次约 301 秒的记录仍仅作历史部分证据。
2. **设备侧 NPU 内存趋势**：`/sys/kernel/debug/rknpu/mm` 需要 root，本次未提权读取，没有设备侧内存占用数据。主机侧文件描述符与内存无泄漏不等于已证明设备侧无泄漏。
3. **静态检查**：构建镜像没有 `clang-tidy`/`clangd`，仓库也未配置检查命令，本次未执行。
4. **Jetson 环境**：本机不具备 Jetson 构建或运行环境，未做任何 Jetson 相关验证；本变更也不改动 Jetson 路径。
5. **跟踪质量**：本次只比较数值一致性与延迟，没有标注数据或质量分析，不构成跟踪质量验收。

结论边界：

- 逐元素一致说明在**这组模型、这段录像和这个 Runtime/驱动版本**下三条路径的 head 原始输出相同，**不证明**设备内部完全没有数据搬运，也不推断其他版本的内部行为。
- 性能结论只在同身份、同跑法下成立，且按观测模式分别给出；本次结论是"未确认稳定收益"，不是"已确认没有收益"，也不是"已确认有收益"。
- 本程序是性能对照工具，不是生产后端。生产工厂与 baseline 的默认加载路径未改变。

## 9. 历史工件一致性核对（2026-09-21，WARNING 修复前）

按 proposal、design、delta spec（本变更新增的规格要求）及 tasks 逐项核对。新增证据未改变模型、算法或生产接口，也没有将受控中止写成全部轮次自然完成。

| 规格要求 | 实现与证据 |
|---|---|
| 独立入口与基线兼容 | `search_direct/main.cpp` 通过 `ModelLoader` 注入专用后端；共用 runner 的扩展参数默认为空。两种构建及入口测试见第 4 节 |
| 搜索特征交接为唯一优化变量 | `SearchDirectBackend::search` 保留图像输入准备及逐帧模板提交，仅替换搜索特征交接；阶段与调用次数见第 5.3–5.4 节 |
| 原生属性准入与明确拒绝 | `native_attributes.cpp` 的实际属性匹配及容量检查，受控属性测试见第 4 节，实机跨度差异见第 5.1 节 |
| 提交后重新绑定、使用当帧特征 | 正式顺序位于 `SearchDirectBackend::search`；独立原始张量参照见第 5.2 节，持续回放逐帧参照见第 6.3 节 |
| 生命周期与失败边界 | `DirectSession` 停止确认及释放、`releaseShared` 的消费者先释放顺序、reset 模板失效；失败注入见第 4 节，启停及长期负载见第 6 节 |
| 完整计时与路径身份 | 第 3 节身份、第 5.4 节阶段核对及第 6.3 节落盘记录重算；未执行的搜索取回阶段不伪造为零 |
| 独立数值参照 | 第 5.2 节正式 C++ 三进程原始输出对照；第 5.3 节回放及第 6.3 节全部负载帧对照。前期 Python 探针仍单列 |
| 同条件性能对照与资源边界 | 第 7 节四组、每组八轮，保留“未确认稳定收益”的结论；第 6 节覆盖 20 次启停和超过 30 分钟负载。设备内存不可读等限制见第 8 节 |

这次历史文档核对前独立重算的源码快照为第 3 节的 `b94286a1b2ea27e8f37143e2fb255558c2d558b298e23d1b9887b6b3f1b77052`。当时没有修改 C++ 实现或重新构建，复用了身份匹配的既有 ARM64 程序。历史证据核对脚本不参与模型推理或性能测量。该身份不再代表第 10 节修复后的代码。

当时 `openspec validate add-nanotrackv3-search-direct-benchmark --strict` 通过，但工件格式通过不证明实现没有缺口。随后发现的 WARNING 使“全部验收项通过”的结论不再成立；当前状态见第 10 节。同步主规格与归档均未执行。

## 10. WARNING 1、3、4 修复验证（2026-09-21）

本次只修复用户指定的三个缺口，未修改或删除 `compare_runs.cpp`，未处理 WARNING 2。以下测试使用设备替身与生成的 MJPEG 素材，不使用实机模型或真实录像。

| 缺口 | 修复与受控验证 |
|---|---|
| WARNING 1：两端相同的畸形跨度可能被接受 | NC1HWC2 原生排列只沿用既有支持范围。有效宽跨度不得小于逻辑宽；`size` 必须等于 `n_elems × 2`；带跨度容量必须完整且等于计算值。每一步乘法先检查 SDK 的 32 位容量上限，通道分组上取整使用 64 位加法，避免极端分组及跨度回绕。测试覆盖两端相同的不足跨度、不足容量、错误 size、元素数和字节数溢出、超出 64 位范围的维度乘积；畸形属性在分配共享内存和执行模型前失败。49152 字节、零跨度规范化与合法行填充仍通过。SDK 的 `h_stride` 是只写字段，未把它当作原生查询准入依据。 |
| WARNING 3：summary 与中文报告缺少完整身份 | runner 向 `writeReports` 传入本次身份。三种产物均包含变体、交接方式、绑定策略、普通及原生属性、共享字节容量。manifest 旧字段不变；summary 的完整模型描述按行保存为字符串数组，避免 OpenCV 4.5 的单字符串长度限制。中文报告以缩进代码块保留换行。测试逐字段核对值，覆盖名称中的引号、反斜线、换行与 Markdown 标记，以及成功、失败、细分观测开关。baseline 默认调用仍不写入直连身份，旧记录核对测试通过。 |
| WARNING 4：示例目录展开参数无效 | README 为 12 个目录分别重复选项。runner 测试直接读取文档代码块，验证四组各三个不同目录，并把生成的受控记录复制到对应目录后调用未经修改的比较工具；命令成功解析并完成比较。该检查只验证文档命令，不解决 WARNING 2。 |

### 命令与结果

镜像仍为 `roc-rk3576-ros2-build:humble-arm64`，平台为 Docker ARM64，SDK 为 2.3.0。未安装依赖或重建镜像。

独立构建沿用 `.docker-artifacts/build-search-direct-verify`，容器内必须挂载到其 CMake 缓存记录的 `/work/build-search-direct`。执行命令如下（从仓库根目录运行）：

```sh
docker run --rm --network none --platform linux/arm64 \
  --mount "type=bind,src=$PWD,dst=/work/ros2_ws/src/drone_tracker" \
  --mount "type=bind,src=$PWD/.docker-artifacts/build-search-direct-verify,dst=/work/build-search-direct" \
  --mount type=volume,dst=/work/ros2_ws/src/drone_tracker/.docker-artifacts/verification-work \
  --workdir /work/ros2_ws/src/drone_tracker \
  --env TMPDIR=/work/ros2_ws/src/drone_tracker/.docker-artifacts/verification-work \
  --env LD_LIBRARY_PATH=/opt/rknn-runtime/lib \
  roc-rk3576-ros2-build:humble-arm64 bash -lc '
    cmake --build /work/build-search-direct -j 4 &&
    ctest --test-dir /work/build-search-direct --output-on-failure'
```

最小检查先构建 `search_direct_direct_backend_tests search_direct_runner_tests`，再执行 `ctest -R 'search_direct_(direct_backend_tests|runner_tests)$'`，包含素材生成依赖共 **3/3 通过**。随后独立完整构建与 CTest **25/25 通过**，包含原 baseline 配置、runner、模型、解码与记录回归。

仓库构建使用 `build.sh --test` 的同一镜像、挂载、Release/平台/测试参数和安装环境，额外为仓库内 `verification-work` 配置容器原生卷与 `TMPDIR`。这样测试的工作文件不会写入系统临时目录，也避开 macOS 共享挂载的 `std::filesystem::copy_file` 权限错误。容器中实际执行：

```sh
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-args \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_PLATFORM=rk3576 -DDRONE_TRACKER_BUILD_TESTS=ON
export LD_LIBRARY_PATH=/opt/rknn-runtime/lib:${LD_LIBRARY_PATH:-}
source install/setup.bash
ctest --test-dir build/drone_tracker --output-on-failure
```

仓库构建通过，CTest **29/29 通过**。日志保存在 `.docker-artifacts/log/search-direct-w134-build.log` 与 `search-direct-w134-test.log`；独立构建的最终日志为同目录下 `search-direct-w134-independent-build.log` 与 `search-direct-w134-independent-test.log`。匿名容器卷随容器退出删除。

`openspec validate add-nanotrackv3-search-direct-benchmark --strict` 与 `git diff --check` 均通过。这只证明工件格式及差异空白检查通过，不覆盖仍未处理的 WARNING 2。

初次验证出现过两个环境问题：独立目录挂载位置不匹配 CMake 缓存，以及 macOS 共享目录复制权限错误。按上述挂载重试解决，未修改测试断言规避问题。完整描述直接写为单字符串时，旧 OpenCV 核对器曾报 `string is too long`；改为上述按行数组后，新记录和 baseline 回归全部通过。

### 当前任务与验证边界

- 1.2、2.3、3.1、3.4、5.3 已按本次测试重新勾选。当前为 **21/23** 项完成。WARNING 2 仍未处理，2.4 和 5.5 保持未完成；没有缩减原验收条件。
- 历史数值、性能、20 次启停和 30 分钟负载证据未重写。它们仅代表第 3 节的修复前源码快照。
- 本次没有启动板端推理，没有运行真实模型或录像，也未读写系统 Runtime。不能宣称修复后的当前工作树已通过 Runtime 2.3.2/驱动 0.9.8 的实机验证、全量验收或已可归档。
- `clang-tidy`、`clangd`、Jetson 及设备侧内存趋势检查仍未执行。容器通过不能替代这些检查。
