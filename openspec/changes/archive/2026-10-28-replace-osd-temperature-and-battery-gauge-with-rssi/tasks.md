# Tasks

## 1. RSSI 遥测输入

- [x] 1.1 在 `src/osd/presentation.h`、`ros_telemetry.h`、`ros_telemetry.cpp` 增加独立 RSSI 百分数测量与 `/mavros/rc/in` best-effort 深度 10 订阅；确认有效 0–100 原样保留、101–255 清空，并沿用独立单调时间过期。
- [x] 1.2 扩展 `src/osd/tests/telemetry_test.cpp`，覆盖 0、85、100、101、254、255、有效值被非法值清除、2000/2001 毫秒边界及电池/温度不刷新 RSSI；运行对应测试通过。
- [x] 1.3 扩展 `src/tests/ros_osd_telemetry_test.cpp`，以隔离重映射和 best-effort 发布证明 RSSI 在停止显示后仍能接收、过期及恢复；测试确认本地状态和旧框不被改写。
- [x] 1.4 在 `docs/RK3576_HDMI_OSD.md` 记录 `apm_bridge` 的 `rk3576` 提交 `390668d2d822731e95651cd3b88547e5912e66a1` 所定义的 0–100 输入、重映射方式、未知与过期规则；文档明确旧生产服务不是最新源码依据，不把数据解释成视频信号或 dBm。

## 2. 温度与电量条的显示替换

- [x] 2.1 在 `src/osd/layout.cpp` 移除温度绘制，加入主字体 `RSSI` 标签，将原电量条改为黄色 RSSI 条及唯一相邻百分数；确认固定逻辑坐标、有效 0 与未知 `--%`、始终黄色及防御性范围检查符合 delta specs。
- [x] 2.2 扩展或新增 OSD 布局测试，验证温度变化不影响画面、RSSI 与电池比例/告警独立、0/10/85/100 条长、100→未知→9 清除；逐像素比较两条渲染路径、两种画布、预览/冻结和稳定缓存，并确认 RSSI 之外 HUD 与目标层不变。
- [x] 2.3 更新 `docs/RK3576_HDMI_OSD.md` 的布局和缺失数据说明，清除当前说明中电量百分比条及可见温度行的旧含义；提供 85% 示例并说明电池其他读数与告警保留。

## 3. 构建与生产板集成验证

- [x] 3.1 在专用工作树执行 `./build.sh --test`，记录构建平台、镜像、测试数量与结果；保存实际布局快照，确认 ARM64 完整节点与 RSSI 遥测测试可构建运行。
- [x] 3.2 在生产 `HZ-RK3576_WuKong Board` 记录原服务、版本链接、配置、模型、输入、显示模式和系统 Runtime 指纹；准备独立候选目录及退出恢复流程，再运行隔离候选版本。以真实相机预览、注入 RSSI 的有效→未知→恢复及一次有界 ON/OFF 冒烟，验证 HDMI 图层读回和正常停止；保存证据，明确注入数据不代表真实射频或实飞验收。
- [x] 3.3 结束候选进程并恢复原 `drone-tracker.service`；核对服务 active/running、原版本和配置指纹、模型和系统 Runtime 未替换以及测试进程退出，保存恢复结果。任何失败也必须完成此恢复。
- [x] 3.4 在 change 内交付独立验证记录，包含测试日志路径、实际快照、来源提交、生产板身份、注入数据说明和服务恢复证据；运行 `openspec validate replace-osd-temperature-and-battery-gauge-with-rssi --strict` 通过。
