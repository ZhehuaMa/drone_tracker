# 实现与验证证据索引

本索引用于独立审查 `improve-detection-tracking`。正式程序的 C++ 实现已完成；本次没有提交 Git、归档变更或替换正式部署。下文的“合格运行”仅表示输入身份、原始记录和时间约束通过检查，不表示跟踪精度或性能达标。

## 工作目录与报告

- 独立规格验证报告：`verification-20261015.md`。任务 2 的检查结果为 0 项 CRITICAL、2 项 WARNING、0 项 SUGGESTION；按用户要求不修复 WARNING。
- 工作目录：`/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker`，分支为 `codex/improve-detection-tracking`。
- 实现评估报告：`/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/docs/improve-detection-tracking-implementation-20261015.md`。
- 主机证据根目录：`/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/.docker-artifacts/improve-cohort-20261015`。
- 板端隔离根目录：`/home/hzhy/improve-detection-tracking-20261015`。访问仅使用 ADB，设备序列号为 `5ceb49407502303d`。

## 构建与测试

- 最终集成构建入口：`./build.sh --test --artifacts-dir .docker-artifacts/improve-detection-tracking --jobs 16`。
- 完整测试日志保存在证据根目录的 `candidate/build-verified.log`、`candidate/build-postprocess.log` 和最终的 `candidate/build-delivery.log`。最终日志包含加强后的像素、时间戳、区域关联测试与持续替换进度测试。`candidate/build-final-coverage.log` 保留测试记录器新增观察字段时遗漏空输入保护的失败；随后补回保护并复验。
- 最终 `candidate/build-delivery.log` 的 `diagnostic_tracker_tests` 也覆盖提前返回、看门狗输出与下一帧 Detecting 的新增直接断言。
- 镜像身份：`sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。`candidate/build-environment.txt` 保存工具链配置。镜像运行于 x86_64，输出程序为 ARM64；部分测试通过 QEMU 指令模拟器运行。
- 完整测试结果为 57/57 CTest 通过，并通过独立的原生 Linux 内存限制测试。Python 回归部分共 56 个测试用例。`build-validation.json` 保留此前失败、修复过程和环境前置条件。
- 精确参考源码构建位于 `/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/.docker-artifacts/improve-reference-cb/source`；安装位于同级 `build-output/backends/amd64/install/drone_tracker`。参考提交为 `cb977c204615ade46a62d198d821ed93de449108`，生产 C++ 没有算法适配修改。
- 候选安装目录：`/home/mzh/.codex/worktrees/improve-detection-tracking/drone_tracker/.docker-artifacts/improve-detection-tracking/backends/amd64/install/drone_tracker`。

## 板端比较与身份

参考版本和候选版本各有 3 次独立合格运行。证据根目录下的 `reference/run-001` 至 `run-003`、`candidate/run-001` 至 `run-003` 保存完整数据。每次运行均保留原有 120 秒总时限和 50 毫秒供帧迟到预算，没有扩大约束或按速度挑选样本。

每个运行目录的 `identity.json` 保存源码、程序、模型、配置、具体板卡、实际依赖库、RKNN Runtime、RKNPU 驱动、CPU 调度和诊断模式身份。`completion.json` 给出合格结论和文件校验和；`deadline.json` 给出总耗时与子进程清理结果；`raw/**/*.dtd`、`events.bin` 和 `events.jsonl` 是原始二进制及其解析记录。`metrics.json` 保存可重新计算的完整窗口和核心窗口指标。`preparation.json` 保存准备阶段的进程内存观测。

| 对象 | SHA-256 |
|---|---|
| 参考回归程序 | `bad61138c886dc94811a6376c2f74ad08f6c8bae4e2a99a2dae81eb9feaaab63` |
| 候选回归程序 | `07b48c8ef98018dcf097598cc7a1640656d9b62e7125e188f8c6543dc9748874` |
| 参考导出源码 | `51c9de649aa8df896bb30e8474bf5666459685ba1f4257922f7023699d5e7ce9` |
| 候选导出源码 | `1fce3ccd2827b947ffd6da320a5f5133d319cb6a6aee92e91b7ce56c5e349b5f` |
| 两版共用的诊断收集程序 | `780b2cd710714a9f71ac83512da164a6365839b8f62511dc61f721fe9b85aaa5` |

`build-identities.json` 保存完整源码文件列表和程序指纹。导出源码包含两版共用的回归收集工具，不等同于完全未修改的 Git 提交。参考生产 C++ 保持精确提交内容；候选生产 C++ 与当前实现一致。

运行时源码快照冻结后，补充了测试断言和离线基线读取器对第二版指标的支持。这些修改没有改变正式 C++ 行为或上述候选程序指纹。`production-source-match.json` 逐个核对全部正式 C++ 源码、头文件及运行回归入口与实测快照完全相同。`post-freeze-differences.json` 逐项列出冻结后测试、离线读取器和说明文件的修改及最终源码树哈希。`analysis-tools/manifest.json` 保存实际使用的离线分析工具指纹；原有运行源码、指标、身份及原始记录均未重写。第一次冻结拒绝新指标的记录保存在 `freeze.log`，修复后成功结果保存在 `freeze-v2.log`。六次板端运行均合格，没有被隐藏或丢弃的板端尝试。

- 冻结基线：`baseline/baseline.json`。
- 正式比较：`comparison/comparison.json`、`comparison/comparison.csv`、`comparison/report.md`，比较结论为 `comparable`。
- 原始计数补充：`raw-summary.json`，含取消原因、仅元数据比较、看门狗次数及内存观测。
- 全片有效框发布比例中位数从约 43.08% 变为 55.65%；排队等待时间的第 95 百分位数中位数从约 521.50 毫秒变为 45.71 毫秒。候选仍存在核心窗口 352.01 毫秒发布间隔的较慢样本，已完整保留。没有预设性能判定规则，也没有据此宣称跟踪精度提升。

## 服务恢复与实际限制

`service-before.json` 和 `service-after.json` 确认正式服务、部署链接、配置、模型、系统 Runtime 和 CPU governor 未发生持久变更。`final-service-status.txt` 确认 `drone-tracker.service` 与 `drone-interceptor.service` 均为 active/running；诊断服务维持原来的 inactive 状态。六份 `restore-ROLE-NNN.json` 逐次确认恢复成功。

每次测试前，`run_one.sh` 先确认独立的 180 秒 systemd 恢复定时器已启用，再停止正式服务。正常退出由 EXIT 清理函数调用 `restore_guard.py`；定时器处理连接或外层进程失效的情况。没有覆盖 `/opt/drone_tracker/current`、生产配置、模型或系统 RKNN Runtime。

需要明确区分的验证边界如下：

- 并发验证使用受控启动、推理屏障、持续生产者和资源弱引用。它覆盖指定交错及生命周期，不是穷举线程调度，也未执行线程消毒器或长期负载测试。
- 内存数据来自回放前准备阶段，不能充当整个运行过程的内存曲线。该板实际报告系统内存为 8,112,044 KiB；不能以此证明仓库文字中 3.8 GiB 配置的内存验收。
- 视频没有人工目标标注。有效框连续性不能证明目标正确，也不能验证真实丢失识别延迟。看门狗没有结果年龄上限，任意已读结果仍会复位计数。
- 实机视频没有出现“来源像素已淘汰但仍有有效跟踪元数据”的实际样本；该分支由受控输入和资源寿命测试验证。
- 实机视频没有出现看门狗超时；60/61 边界、不可比较结果复位、无目标复位及 Detecting 不启用看门狗由受控测试验证。
- `clang-tidy` 与 `clangd` 未配置或不可用。没有执行 Jetson、相机、HDMI、长时间负载或飞行验收。
