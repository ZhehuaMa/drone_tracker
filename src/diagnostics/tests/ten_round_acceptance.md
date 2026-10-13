# 任务 7.3 十组成对验收入口

本入口对同一生产板上的同一业务程序执行十组 `off`/`basic` 对照。`off` 表示关闭诊断，`basic` 表示记录基础事件。奇数组先关闭、再基础；偶数组顺序相反。每组两个实际测量窗口各 45 秒，各自先预热 4 秒。窗口内所有调用和提交样本均保留，不把子窗口算成新的组。

每组提交 P99 必须不超过 20 μs，完整调用 P99 增量必须不超过 0.2 ms，消费帧率相对下降必须不超过 1%，健康日志必须没有缺口。P99 是排序后第 `ceil(0.99×N)` 个样本。至少八组同时满足全部门槛，任务才完成。超时、样本不足或资源未回收都算失败。新的完整十组是一个独立批次；旧批次和失败组不可删除，也不可与新批拼出八个通过组。

## 使用前准备

1. 按 `AGENTS.local.md` 取得匹配目标平台的安装产物。保存节点、worker、inspect、视频、配置、模型与脚本的 SHA-256 指纹。SHA-256 是核对文件内容的摘要。修改诊断热路径后，必须用新二进制取得新批次证据，不能套用旧二进制结果。
2. 在板端建立全新的独立目录。目录下准备 `drone_tracker/` 安装树、`fixed-balloon.avi`、空 `evidence/` 及本目录的四个 Python 文件：`board_acceptance.py`、`analyze_board_acceptance.py`、`ten_round_window.py`、`ten_round_restore_guard.py`。允许只读链接到已验证产物及固定视频；不得改变正式 `current`、配置、模型或系统 Runtime。
3. 设置 `DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT` 为该绝对目录。设置 `DRONE_DIAGNOSTIC_UNIT_PREFIX` 为本批独有的 systemd 单元前缀。每个新批次都使用新目录和新前缀，不复用旧 `armed` 或输出目录。
4. 使用与实际测试服务相同的环境做短预检。先加载 `/opt/ros/humble/setup.bash`，再加载独立安装的 `share/drone_tracker/local_setup.bash`。业务必须设置 `DRONE_TRACKER_CPU_CONFIG=/opt/drone_tracker/config/cpu-affinity.yaml`。ROS 隔离环境固定为 `ROS_DOMAIN_ID=198`、`ROS_LOCALHOST_ONLY=1`。控制器与 bag 使用 CPU 0–3；业务和 worker 允许 CPU 4–7，业务继续使用既有职责映射。
5. 正式服务保持运行时，用隔离视频、HDMI 关闭的实际业务短预检。核对安装目录解析、模型加载、实际 ON/OFF、`CPU policy loaded mode=role-pinned` 和职责读回。协议版本查询提前返回，不能代替此预检。保存完整环境、实际 YAML 和运行输出。确认正式服务、HDMI、全部受保护文件及隔离残留无变化。

## 限时运行及恢复

systemd 控制组是操作系统统一管理的一组进程，包含后来创建的新会话进程。`ten_round_window.py` 为每组创建独立服务，设置 `RuntimeMaxSec=140s`、`TimeoutStopSec=5s` 和 `KillMode=control-group`。控制器即使卡住，systemd 也会终止该组全部后代。单组外部预算为 150 秒，包括启动、预热、两个窗口、切换、收尾与只读导出。`*-window.json` 保存实际时间、服务状态、控制组及剩余进程，不能仅凭控制器退出声称全部回收。

1. 先以独立 systemd 服务运行 `ten_round_restore_guard.py`，传入本批目录和前缀。确认服务 `active/running` 及 `evidence/guard-ready.json` 已存在。
2. 再以另一个独立服务运行 `ten_round_window.py`。两个服务的 sudo 子 shell 均须先加载 ROS 环境。窗口程序核对正式服务状态，保存正式身份，并在停服前写入 `armed` 单调时间。单调时间是不会受系统墙钟校准影响的计时来源。
3. 守护从 `armed` 起计时。收到 `restore-requested` 或到第 1720 秒，就有限清理本批单元并恢复正式 `drone-tracker.service`。恢复始终放在 `finally` 中，清理失败也继续执行。总截止为 1800 秒，包含服务恢复。SSH 断开不影响守护。
4. 结束、失败或超时后读回正式服务 `active/running/enabled`、HDMI 控制权、原 `current`、配置、全部模型和系统 Runtime 指纹。扫描本批控制组与进程，确认没有隔离残留。不要杀与本批无关的程序。

## 独立复算与交付

正式服务恢复后，使用板端 Python 运行：

```bash
source /opt/ros/humble/setup.bash
python3 analyze_board_acceptance.py "$DRONE_DIAGNOSTIC_ACCEPTANCE_ROOT/evidence"
```

每个 `case.json` 的 `measurement_start_ns` 与 `measurement_end_ns` 定义实际半开区间，即包含起点、不包含终点。分析器保留区间内所有样本，用基础调用 P99 减关闭调用 P99，并用 `100×(1-basic_fps/off_fps)` 计算帧率下降。负数保留原符号。消费数量取实际调用的消费标志，不以消息或日志事件数代替。

交付全部十组原始 CSV、二进制诊断、只读导出、bag、实际 YAML、慢样本、窗口结果、程序与输入清单、恢复记录及独立中文报告。启动失败或配置不合格的尝试也完整保留，并明确不能提供合格对照。另保存缺失组与未完成窗口，不能冒充完整十组。至少八组通过才勾选任务 7.3，然后重新执行 `openspec instructions apply --change add-rk3576-tracking-diagnostic-process --json` 核对进度。

本入口只完成十组测试的外部截止与恢复保护。旧 `bounded_run.py` 的覆盖问题及其他独立校验警告须分别处理，不因本入口存在就宣称它们已修复。
