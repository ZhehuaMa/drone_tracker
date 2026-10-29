# CPU 6 绑定实施验收记录

日期：2026-10-29，时区 Asia/Shanghai。变更：`pin-rk3576-diagnostic-collector-cpu6`。

## 实现与范围

已安装诊断入口继续校验共享 CPU 策略。`diagnostic-launch.py` 先要求共享允许列表包含 CPU 6，再仅对自身设置 `{6}` 并精确读回，成功后执行真实收集器。亲和性表示允许运行的核心集合，不表示独占核心。普通调度 `SCHED_OTHER` 和已有参数传递保持原样。

策略排除 CPU 6、设置失败或读回不一致均在执行收集器前返回非零，不选择备用核心。业务入口、共享策略、职责源码和裸 worker 的实验启动语义未修改。服务检查脚本已将收集器期望更新为 `{6}`，并检查自动恢复及显式重启后的范围。

## 主机检查

主机为 Ubuntu 22.04.5 x86_64，具有 CPU 0–23。本次没有改动 C++ 源码，也没有重新编译未改动的 worker。

- 运行 `PATH=/usr/bin:/bin /usr/bin/python3 -B -m unittest deployment.tests.test_diagnostics deployment.tests.test_package deployment.tests.test_prepare_display -v`，19 项通过，见 [完整输出](evidence/deployment-tests.txt)。
- 独立运行 `/usr/bin/python3 -B deployment/tests/test_cpu_policy.py /home/mzh/nickluo/drone_tracker`，退出码为 0。该脚本把第一个参数作为仓库路径，不能通过一般的 unittest discover 导入；初次通用发现调用失败，随后改用独立调用，没有修改该脚本。
- 诊断测试实际执行入口链，核对参数、CPU 6 和普通调度。失败注入覆盖策略排除、设置异常与错误读回。每种情况下，调用方和另一进程的既有工作线程保留原亲和性，旁路进程继续运行。
- 对真实 CPU 6 的可用性判断使用独立子进程实际尝试 `sched_setaffinity(0, {6})`。不会仅凭调用方当前掩码推断硬件不可用，也不会把生产固定值改成其他核心。
- `deployment/cpu-affinity.yaml`、`deployment/cpu-policy.py`、`deployment/run-camera`、`src/platform/cpu_affinity.cpp` 和头文件的开始指纹与最终内容一致，见 [文件指纹](evidence/business-files-before.sha256) 和 [复核结果](evidence/business-files-verified.txt)。

常规 Python 优先尝试了 Conda base，但其中没有 YAML 依赖。首次入口子进程因此失败；改用带 YAML 的 `/usr/bin/python3` 3.10，并让测试子进程使用相同解释器目录。最终测试没有跳过项目。

## 生产板真实 worker 检查

通过 `ssh firefly-production` 连接 **HZ-RK3576_WuKong Board**。系统为 Ubuntu 22.04.5 ARM64，内核 `6.1.141-rt36`。每个远程命令加载 ROS 2 Humble 环境。板卡、启动身份及真实 ELF 二进制信息见 [身份记录](evidence/board/board.json)。

真实收集器取自板端冻结安装目录 `/home/hzhy/cpu6-cpu7-full-production-20261029/drone_tracker`，没有修改该目录。其 SHA-256 为 `2359d64998a19d8390853a4e7c7feac00dce65f3d350b83b31ecc7cf42b1db7c`。新入口的隔离副本只替换安装、通信和存储路径。固定核心逻辑保持原样，[直接入口清单](evidence/board/direct-entry-fixture.json) 和 [服务入口清单](evidence/board/service-entry-fixture.json) 保存脚本指纹及路径替换。

| 启动方式 | 实际 PID / 线程数 | 全部线程允许集合 | 调度 |
| --- | --- | --- | --- |
| 直接入口 | 32789 / 1 | `{6}` | `SCHED_OTHER` |
| 隔离测试服务 | 32811 / 1 | `{6}` | `SCHED_OTHER` |
| 重启该测试服务 | 32832 / 1 | `{6}` | `SCHED_OTHER` |

三个实例均执行真实 worker。记录同时包含 `/proc/<pid>/task/*/status` 与系统亲和性调用读回，见 `evidence/board/worker-*.json`。调用方、带工作线程的旁路进程、已有 interceptor PID 1515 的 18 个线程，以及 bridge PID 1584 的 14 个线程，在每个检查点及直接实例停止后均保留原亲和性和调度。正式 tracker 本来就没有运行，本次没有为获取线程记录启动它或相机。

第一次隔离尝试中，嵌套测试目录使 Unix socket 路径超过内核长度限制。启动器已读回 CPU 6，但真实 worker 拒绝该端点。该尝试没有残留进程，正式状态保持原样。随后只把测试通信目录改成短的 `/run/drone-diag-cpu6-direct`，保留失败记录于 [attempt01](evidence/board/attempt01/result.json)。生产路径和程序没有因此改动。

有效检查由外部截止程序限制在 180 秒内。截止程序独立追踪子进程，并登记测试服务以回收 systemd 创建的进程。实际总时长 **4.683 秒**，无超时，退出码 0，所有登记进程与控制组成员均已回收，见 [外部窗口](evidence/board/window.json)。单独检查结果为 [result.json](evidence/board/result.json)，原始小文件摘要见 [manifest.json](evidence/board/manifest.json)。

## 正式状态与清理

没有停止任何正式服务。检查前后 `drone-tracker.service` 均为 `inactive/dead`、MainPID 0，开机启用状态仍为 `enabled`；正式诊断服务均为 `not-found`、`inactive/dead`。没有把原先停止的服务擅自启动。

`/opt/drone_tracker/current` 仍指向 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。正式配置、入口、模型和系统 Runtime 共 16 个受保护文件的 SHA-256 一致，见 [检查前](evidence/board/formal-before.json) 和 [检查后](evidence/board/formal-after.json)。其中系统 RKNN Runtime 仍为既有 2.3.2 库，指纹为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。未启动 NPU、模型或相机。

测试服务 `drone-diag-cpu6-validation.service` 已停止，其单元文件已移除并重新加载 systemd；最终为 `not-found`、MainPID 0。直接实例、服务实例和旁路进程均已退出，独立运行目录及测试 StateDirectory 已回收。板端证据保存在 `/home/hzhy/pin-collector-cpu6-validation-20261029/`，完整本地副本在仓库忽略目录 `.docker-artifacts/collector-cpu6-validation-20261029/`。

本次没有正式部署新入口，没有执行模型推理、相机采集、性能对照、长期负载或实飞验收。输入为无客户端会话的收集器启动；该检查证明实际 CPU 绑定和隔离边界，不证明跟踪性能改善，也不替代旧诊断变更任务 7.3。
