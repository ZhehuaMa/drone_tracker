# 生产板两次断连调查

## 结论与边界

2026-10-16 16:55–16:58（Asia/Shanghai），独立调查代理只通过 `ssh firefly-production` 检查 HZ-RK3576_WuKong 生产板。没有重新施加回放负载，没有停止服务，也没有修改内核、设备树、频率策略、温控阈值或正式部署。

两次断连的直接原因已经确认：内核触发过温保护关机。时间分别为 **16:22:18** 和 **16:46:14**，与验收连接断开一致。第二次关机前，各温度传感器的读数持续上升。该证据高度支持持续负载下散热不足；尚不能远程确定散热器接触、风扇、风道或环境温度中的具体物理原因。

这次不是仅凭 SSH 失败或 LED 熄灭判断原因。日志直接记录了保护关机；没有必要再次触发关机来确认。任务 4.4 仍未完成，OpenSpec 仍为 14/15。不能以短案例通过替代连续 610 秒负载要求。

## 直接证据

持久化文本日志 `/var/log/kern.log` 和 `/var/log/syslog` 在两次故障时均包含：

```text
Oct 16 16:22:18 hzhy kernel: [353760.376157] thermal thermal_zone2: little-core-thermal: critical temperature reached, shutting down
Oct 16 16:22:18 hzhy kernel: [353760.376161] reboot: HARDWARE PROTECTION shutdown (Temperature too high)
Oct 16 16:46:14 hzhy kernel: [  661.767598] thermal thermal_zone2: little-core-thermal: critical temperature reached, shutting down
Oct 16 16:46:14 hzhy kernel: [  661.767602] reboot: HARDWARE PROTECTION shutdown (Temperature too high)
```

原始摘录见 `evidence/outage-investigation/outage-system-log.txt` 和 `thermal-context.txt`。文本日志含二进制零字节，普通 grep 会只报“匹配到二进制文件”；必须用 `grep -a` 才能读到相关行。旧 journal 的最后时间为 14:36:49，且没有保留上一次启动；pstore（保存跨重启崩溃记录的内核区域）为空。这些日志空白不代表没有故障。`last -x` 同时记录 16:22 和 16:46 的 shutdown。

## 温度趋势与当前温控设置

从板端保留的五个完整案例重新提取温度。下表为 `thermal_zone2`，即内核命名的 `little-core-thermal`；单位摄氏度。原始数据为板端每秒左右一次的 sysfs 读数，不是估算。

| 案例 | 开始 | 最高 | 结束 |
|---|---:|---:|---:|
| 第一组 off | 67.461 | 86.811 | 86.811 |
| 第一组 legacy | 84.999 | 100.145 | 100.145 |
| 第一组 cached-overlay | 96.589 | 103.700 | 102.811 |
| 第二组 off | 97.478 | 103.700 | 103.700 |
| 第二组 legacy | 101.923 | 111.700 | 111.700 |

第二组 legacy 的 bigcore 最高为 112.589°C。第二组 cached-overlay 因关机未写出资源 JSON。其终止日志仍保存在取回的板端原始归档中。关机前不仅一个传感器升温：SoC、DDR、NPU 和 GPU 均呈相近趋势。OSD 关闭案例也明显升温，现有证据不能将故障单独归因于新增 OSD 字段。

当前 sysfs 显示 little-core 的被动温控点为 75°C、85°C，关键关机点为 115°C，各滞回值为 2°C。滞回值用于避免温度在边界附近来回切换。其他五个温区的关键关机点均为 115°C。CPU policy0 为 performance，最低和最高均为 2.016 GHz；policy4 为 performance，范围 408 MHz–2.208 GHz。只暴露 GPU、NPU 两个 cooling_device（内核可调的降温设备），没有可远程控制的风扇接口。以上是现状记录，不代表已完成 CPU 降频机制审计。

恢复后的正式服务基线温度约为 65–70°C，远低于故障前趋势。原始设置及读数见 `current-state.txt`、`thermal-context.txt`、`baseline-temperatures.txt`，逐案例汇总见 `previous-case-trends.json`。

同一时刻还有 `rk_tsadcv2_temp_to_code: invalid temperature, temp=2147482724 error=511` 和 `Failed to set trips: -34`。不能将该整数当作实际温度，或单凭它断言温度传感器失效。Rockchip 6.1 参考源码中，`rockchip_thermal_set_trips` 将温控上界加校准量后交给报警温度转换函数；转换错误参数与实际采样温度是两条路径。因此现有趋势仍支持真实热积累。参考源码不是当前厂商内核构建的逐字证明，底层转换错误是否需要修复仍待独立分析：[Rockchip 温度驱动](https://github.com/rockchip-linux/kernel/blob/develop-6.1/drivers/thermal/rockchip_thermal.c)、[内核关键温度保护逻辑](https://github.com/rockchip-linux/kernel/blob/develop-6.1/drivers/thermal/thermal_core.c)。

## LED、脚本和资源检查

用户报告常亮 LED 在失联时熄灭，与整板保护关机一致。当前 Linux 只暴露 `mmc0::`，触发器为存储活动 `mmc0`；不能据此将用户观察的物理 LED 等同于该节点。未取得该 LED 的板级电路映射。

检查比较与持续负载脚本，未发现 OS 的 shutdown、poweroff、halt 或 reboot 调用。`rclpy.shutdown()` 只清理 ROS 客户端。脚本只停止和恢复 tracker，并使用原有 CPU 分配。完整案例没有显示失控的线程或文件描述符增长：off 的描述符保持 44，legacy 保持 56，cached 保持 52；每个案例节点独立退出。RSS 峰值约 205–227 MiB，现有证据不支持内存耗尽是这两次故障的直接原因。没有发现对应时刻的 OOM、panic、看门狗或仅网络链路异常证据能替代已确认的温控关机原因。

验收脚本存在可修正的保护和留证缺口：比较脚本把温度暂存在内存，案例结束才写 JSON，且没有温度超限提前退出；持续负载脚本没有采集温度。持续负载的 finally 中，节点等待超时可打断后面的服务恢复。后续应增加逐条落盘温度、在低于硬件保护点时提前终止、以及不受节点清理异常影响的服务恢复。该提前退出只能产生“未完成”，不能缩短 75 秒或 610 秒后称验收通过，也不能改变性能门槛。

## 恢复状态和后续条件

16:57:36 最终核对：新 boot ID 为 `62b4c6ba-0fa7-4996-8aca-f8c522b548f4`；`drone-tracker`、`apm-bridge`、`drone-interceptor` 全部 active/running 且 enabled；`lightdm` 维持原 inactive/dead。没有残留私有验收节点或比较/持续负载脚本。

正式链接仍指向 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。相机配置 SHA-256 为 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`，CPU 策略为 `4f46293201355131c1e72c8a72cdfaafa2c3121eaa48fe0b5ad69ac442e4f01b`，系统 Runtime 为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`，均与原基线一致。详见 `recovery-final.txt`。

下一步需要确认实际散热器安装、导热接触和风扇/风道。远程暂未发现可调风扇接口。确认有效散热后，先修复上述验收脚本的保护与恢复，再按原时长、模型、输入和门槛继续。不能通过提高 115°C 阈值、禁用温控、改变频率策略或跳过连续负载来绕过阻塞。本调查没有修改上述设置，也没有进入后续 OpenSpec verification、同步或归档。

完整取回的原始案例归档保留在主机 `/tmp/osd-gps-armed-status-20261016/outage-investigation/previous-board-evidence.tar.gz`；小型关键日志已复制到本 change 的 `evidence/outage-investigation/`。板端原目录保持不变。
