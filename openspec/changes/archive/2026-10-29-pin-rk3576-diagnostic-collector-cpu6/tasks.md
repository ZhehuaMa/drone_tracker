# 实施任务：诊断收集器固定 CPU 6

## 1. 统一诊断入口的固定核心行为

- [x] 1.1 实现仅收集器自身使用 CPU 6：修改 `deployment/diagnostic-launch.py`，保留共享允许列表参数，先确认其中包含 6，再仅对当前进程设置 `{6}` 并精确读回；检查成功记录包含请求和实际集合，失败路径不执行 worker，普通调度和后续参数传递保持不变。
- [x] 1.2 验证成功与失败的入口边界：更新 `deployment/tests/test_diagnostics.py`，覆盖 `{6}`、策略不含 6、设置异常、读回错误、参数及调度保持；核对调用方和一个带工作线程的独立旁路进程在诊断成功及失败后亲和性不变。运行相关 Python 测试并保存结果，真实 CPU 6 不可用的宿主机限制须明确记录，不能替换生产固定值。
- [x] 1.3 说明新的安装入口契约：更新 `deployment/README.md`、`deployment/run-diagnostics` 注释和受影响诊断说明，明确服务与直接入口均固定 CPU 6、共享策略必须允许 6、失败不回退以及 CPU 6 仍与显示共享；检索并修正活动文档中关于收集器使用完整允许范围的旧描述，历史证据保持原样。

## 2. 现有服务检查与生产板集成验收

- [x] 2.1 使既有服务检查遵循固定核心：更新 `src/diagnostics/tests/entry_service_probe.py`、`src/diagnostics/tests/service_acceptance.py` 的收集器期望为 `{6}`，保留业务自身的共享范围；只在必要时调整相关测试辅助代码，核对所有当前安装入口调用者的期望一致且没有改动裸 worker 的实验启动语义。
- [x] 2.2 确认业务策略保持原样：运行 `deployment/tests/test_cpu_policy.py` 和相关部署 Python 检查，记录 `deployment/cpu-affinity.yaml`、`deployment/cpu-policy.py`、`deployment/run-camera` 及业务职责源码相对任务开始时内容未变；证据保存在本 change 的验收记录中，不修改旧诊断 change 的工件或任务。
- [x] 2.3 在指定生产板确认真实执行范围：经 `ssh firefly-production` 在 HZ-RK3576_WuKong Board 对新入口的隔离副本执行一次总预算不超过 180 秒的直接启动、测试服务启动及重启检查；使用真实 ARM64 worker，保存板卡与二进制身份、路径替换清单、全部实际线程的 CPU 6 读回和 `SCHED_OTHER`，以及调用方、旁路进程及其线程、可识别既有业务线程的前后范围。缺席的业务进程如实记录，不启动相机或模型作为本项前提。
- [x] 2.4 交付可核查的清理与恢复结果：保存板端检查前后的正式服务运行及启用状态、正式链接和受保护配置指纹；停止并回收全部测试服务及其进程。若检查中停止了原先运行的正式服务，在所有工作结束时恢复并读回原状态；输出独立的验收记录，明确未作正式部署、性能或实飞验收。

## Workflow follow-up

- 完成实现后，针对 `pin-rk3576-diagnostic-collector-cpu6` 执行验证修复循环；零 CRITICAL 和零 WARNING 后交付。
- 按用户要求仅暂存本变更相关文件并生成英文 Conventional Commit 消息，不执行提交。
- 将来同步或归档时，保留本变更对旧 `add-rk3576-tracking-diagnostic-process` 完整收集器范围要求的后继关系；本任务不归档任一变更，也不完成旧任务 7.3。
