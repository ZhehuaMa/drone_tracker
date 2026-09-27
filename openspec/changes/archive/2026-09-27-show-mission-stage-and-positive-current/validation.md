# 验证记录

## 范围与平台

本次验证针对 HUD 状态栏的 `MISSION` 触发覆盖，以及电流绝对值显示。构建平台为开发机上的 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` ARM64 容器。容器对齐生产板的 RKNN Runtime 2.3.2，但没有生产板的显示设备、RKNPU 驱动或实飞遥测。本次没有部署到 `HZ-RK3576_WuKong Board`。

## 已完成检查

- `openspec validate show-mission-stage-and-positive-current --strict`：通过。
- `git diff --check`：通过；Git 文件监视器曾输出 IPC 提示，但差异检查退出码为 0。
- `./build.sh --test`：RK3576 ARM64 Release 构建成功；43/43 项 CTest 通过。相关测试包括 `osd_presenter_tests`、`osd_layout_tests`、`osd_short_text_tests`、`osd_hud_style_tests`、`osd_telemetry_tests` 和 `ros_osd_telemetry_tests`。
- 新增显示管理器测试覆盖任务标志跨帧保留、超过两秒仍保留、下一次状态发布后清除，以及普通 `ON` 不启动覆盖。HUD 图像测试核对任务覆盖前后的状态栏变化、目标框不变和两条渲染路径一致。短文本测试核对 `-3.0 A`、缺失和非有限电流。

## 未完成的实机检查

- 尚未在生产板 HDMI 上观察 `MISSION` 后持续显示 `ON MISSION`、收到新状态后恢复，以及负电流正值显示。
- 尚未使用生产板真实模型、相机或录像输入复验持续负载与屏幕可读性。容器测试不代替这些检查。
- 容器未提供 `clang-tidy` 和 `clangd`，仓库也未配置目标架构的静态检查命令，因此本次未运行这两项检查。
