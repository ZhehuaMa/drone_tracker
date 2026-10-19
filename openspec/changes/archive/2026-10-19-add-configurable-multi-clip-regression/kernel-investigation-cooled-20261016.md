# 内核日志调查：前次 USB 连接中断

结论：前次运行由内核温度保护触发关机，关机过程中 USB/ADB 断开。该结论有前一启动周期的内核日志直接支持。用户随后报告已重启并加装风扇；风扇效果由新批次温度另行观测，不以用户操作推断原因。

## 设备与证据来源

设备为 HZ-RK3576_WuKong Board，ADB 序列号 `5ceb49407502303d`，machine ID 为 `1fe0a397f2ae4343896c7cf9d253a608`。所有板端访问仅使用 ADB。前一启动 ID 为 `ef11c970-bd3c-401c-ae8e-d894a86e42fe`，当前启动 ID 为 `937279a6-69b1-4465-9c4c-c81adb81a00c`。

普通 `journalctl --list-boots` 展示的启动列表包含旧归档且未可靠定位当前记录。按日志字段 `_BOOT_ID` 精确查询成功取得前一启动的 5362 条记录；已逐条核对这些记录的启动 ID。原始证据见 `kernel/previous-target-boot.jsonl`，关键事件见 `kernel/decisive-events.json`。当前 `dmesg`、当前启动内核 JSON、热区阈值、冷却设备及 CPU 频率策略也分别保留。

## 关键事件

日志接收时间 `__MONOTONIC_TIMESTAMP` 与内核事件时间 `_SOURCE_MONOTONIC_TIMESTAMP` 是两个字段，不能直接当成相同计时点。以下均为原值，单位微秒，属于同一个前次启动 ID。

| 事件 | 日志接收单调时间 | 内核源单调时间 |
|---|---:|---:|
| `thermal thermal_zone2: little-core-thermal: critical temperature reached, shutting down` | 1129755356 | 1131239418 |
| `reboot: HARDWARE PROTECTION shutdown (Temperature too high)` | 1129756101 | 1131239421 |
| `thermal thermal_zone2: little-core-thermal: critical temperature reached, shutting down` | 1129838222 | 1131324463 |
| `reboot: HARDWARE PROTECTION shutdown (Temperature too high)` | 1129839214 | 1131324467 |
| `thermal thermal_zone2: little-core-thermal: critical temperature reached, shutting down` | 1129881593 | 1131368447 |
| `reboot: HARDWARE PROTECTION shutdown (Temperature too high)` | 1129882491 | 1131368450 |
| `android_work: sent uevent USB_STATE=DISCONNECTED` | 1131632106 | 1133121370 |

前次维护 guard 在自身 `time.monotonic()` 约 1129.875 秒收到 SIGTERM，随后确认测试资源已回收。重启生产服务失败的原因是系统已排入关机停止任务。内核日志明确记录 `little-core-thermal: critical temperature reached, shutting down` 和 `HARDWARE PROTECTION shutdown (Temperature too high)`，之后记录 USB 断开。这比仅观察到高温更强，足以确认当次热保护关机。

主机 USB 断开记录时间为 2026-10-16 16:53:34（Asia/Shanghai）。板端墙钟处于 2026-08-15，存在显著日期偏差，不能把板端墙钟与主机时间直接相减。此次因果判断依据同一启动 ID 内的事件顺序、明确关机原因及维护进程收尾记录，不声称得到毫秒级跨设备时间校准。

## 当前状态及限制

当前启动读取到六个热区约 37–40°C。`little-core-thermal` 的被动调节阈值为 75°C 和 85°C；所有读取到的临界关机阈值为 115°C。阈值是配置，不是前次达到的精确温度；关键日志没有提供当时传感器数值。前次身份快照中的 92.247–95.803°C 是较早观测，不应写成最终关机温度。

`/sys/fs/pstore` 为空，没有额外持久化崩溃记录。当前启动 `dmesg` 未发现新的临界温度关机、panic 或 OOM 记录。调查未写入内核、sysfs、频率策略、时钟或冷却设置。

重新连接后生产服务已确认 active/running/enabled，PID 1366；旧测试进程和临时恢复单元均不存在。旧批次恢复了 7 个已开始轮次，其中 6 个已最终化、4 个合格，尾段第 1 轮因关机中断，后续未开始；这些证据原样保留，未用于新比较组。

外部证据根目录：`/home/mzh/nickluo/drone_tracker-validation/configurable-video-regression-task2-cooled-20261016`。
