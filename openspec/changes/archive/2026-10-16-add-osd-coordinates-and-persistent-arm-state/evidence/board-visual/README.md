# 生产板 DRM 扫描平面检视

这些 PNG 来自 `HZ-RK3576_WuKong Board` 正在扫描的实际 DRM（Linux 显示设备管理接口）帧缓冲。不是 CPU 布局模拟，也不是生成示意图。测试程序为隔离安装的新节点，SHA-256：`71be771a0914f3939394dffad120337896ec08c25f27cbbd1ae2bf7aca757ca9`。

`capture_planes.cpp` 枚举正在使用的显示平面，通过 GETFB2 取得帧缓冲并只读映射，分别保存平面 58 和 103。PNG 是这两层按实际 alpha 合成的检视图。原始平面位于板端 `/home/hzhy/osd-gps-armed-status-20261016/visual` 和主机 `/tmp/osd-gps-armed-status-20261016/board-visual`。两层顺序读取，动态图像可能相差一次刷新；这里不以截图证明动态图像与框的逐帧配对。

720×480 使用 `cached-overlay`，1280×720 使用 `legacy`。缓存路径的 HUD 位于独立覆盖平面；旧路径把 HUD 合成到视频平面，不能把旧路径的空覆盖平面误判为未绘制状态。两种路径均检视合成后的实际画面。测试用固定真实录像与既有 RKNN 模型；位置、连接、解锁、电池和告警为明确注入的合成遥测，不是飞行记录。

人工检视（通过 `view_image` 读取实际扫描像素）确认：

- 两种尺寸下最长 `LAT -90.000000`、`LON -180.000000` 均完整可读，主字号和描边没有缩小或裁切。
- `720-armed-over75s.png` 的计时为 `T+01:16`，底部仍为绿色 `ARMED`。
- `720-armed-trigger-frozen-warning.png` 同时显示左上 `TRIG OFF`、底部 `ARMED`、独立中文冻结提示和右侧红色 `FAILSAFE`。
- `1280-disarmed-frozen.png` 显示白色居中的 `DISARMED`；`1280-stale.png` 显示琥珀 `ARM --` 和坐标占位，未残留旧坐标。
- 恢复、非法位置及任务覆盖由各自 PNG 和 `events.json` 记录。

`checks.json` 的附加断言比较固定区域的前景色字形，确认跨越 75 秒、触发及触发消失后解锁文字不变。断连与过期的未知状态一致，非法位置和超时占位一致，有效定位恢复原字形。已冻结后的中心视频区域保持相同像素。动态视频底图会变化，因此跨实时/冻结比较的是字形掩码，不是整块含背景的矩形。CPU 单元测试中的完整逐像素路径对照另见上层说明。

这是远程读取实际显示输出的可读性检查。没有测量显示器物理亮度、观看距离、环境光或现场视觉舒适度；不将这些未测项目称为通过。测试过程中不改显示器标定、正式配置或发布链接。

本阶段退出码为 0。`restore.json` 和 `../board-after-visual.log` 确认 tracker、apm-bridge、drone-interceptor 恢复/保持 active，lightdm 保持原先 inactive；正式发布、配置及系统 Runtime 指纹不变。
