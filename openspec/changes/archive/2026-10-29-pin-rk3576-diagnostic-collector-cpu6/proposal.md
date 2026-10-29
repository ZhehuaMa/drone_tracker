# 提案：将 RK3576 诊断收集器固定到 CPU 6

## Why

目前无法通过统一启动入口让独立诊断收集器稳定地只在 CPU 6 运行。入口会把它的允许范围重新设置为 CPU 4–7，因此启动前执行单核绑定也会被覆盖。本变更只收窄收集器自身的执行位置，让服务启动和手动启动获得一致结果。

## What Changes

- 诊断入口继续读取并校验已安装的共享 CPU 策略，但仅将收集器自身绑定到 CPU 6。CPU 亲和性是进程允许运行的核心集合，不代表独占核心。
- 在执行收集器程序前确认 CPU 6 属于共享允许范围，并读回实际集合 `{6}`。失败时退出并报告诊断不可用，不选择其他核心。
- 两种入口共用同一实现，保持普通 `SCHED_OTHER` 调度，即现有非实时调度方式。
- 保留业务允许范围和职责映射，以及其他进程和线程的亲和性、优先级与系统设置。
- 更新相关入口测试、生产板隔离检查和部署说明。

具体例子：共享策略仍允许 `[4, 5, 6, 7]`。变更前，启动诊断服务后收集器允许使用这四个核心；变更后，同一入口只允许收集器使用 CPU 6。控制与跟踪仍为 CPU 4，检测仍为 CPU 5，显示仍为 CPU 6，采集与录像仍为 CPU 7。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `production-binary-deployment`：增加诊断收集器固定 CPU 6 的启动契约，覆盖服务与直接入口、失败拒绝和其他进程隔离。

## Impact

主要涉及 `deployment/diagnostic-launch.py`、`deployment/run-diagnostics` 的说明、`deployment/tests/test_diagnostics.py`、`src/diagnostics/tests/entry_service_probe.py`、`src/diagnostics/tests/service_acceptance.py` 和诊断部署文档。只有需要适配入口检查时才调整相关测试辅助代码。

不引入新配置字段、依赖或模型。不更改 `deployment/cpu-affinity.yaml`、`deployment/cpu-policy.py`、业务启动入口和职责实现。原始诊断事件、文件格式和收集器主体算法不变。

现有未归档变更 `add-rk3576-tracking-diagnostic-process` 的部署增量要求收集器使用完整共享范围。本变更明确用 CPU 6 子集替代该收集器范围行为，不修改该旧变更的工件或未完成的性能任务 7.3。后续同步或归档时须保留这项后继约束，不能恢复旧的完整范围行为。
