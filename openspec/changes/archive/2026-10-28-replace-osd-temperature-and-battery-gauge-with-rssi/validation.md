# RSSI 显示替换验证记录

## 结果与范围

2026-10-22 在专用工作树完成温度行移除及 RSSI 条形替换。RSSI 在此指飞控提供的遥控接收链路质量百分数。左下条长和唯一相邻百分数均取自 RSSI，不再取自电池比例。输入未知时保留 `RSSI`、空轨道和 `--%`。其他电池读数与告警保留。

本记录包含 ARM64 交叉构建、完整测试及生产板候选程序验证。交叉构建指在 x86_64 主机运行编译器，输出 ARM64 程序；它不能替代板端相机、NPU 或 HDMI 验证。生产板测试使用明确注入的 RSSI，不证明真实射频读数精度、视频信号质量或实飞表现。未永久部署候选版本，结束后原生产服务已恢复。

## 输入来源与实现证据

权威来源是用户指定的 `apm_bridge` 本地 `rk3576` 分支，提交 `390668d2d822731e95651cd3b88547e5912e66a1`：

- `include/custom_link/protocol.h:57` 定义 `PayloadMedium.rssi_pct` 为 0–100。
- `src/custom_link_vehicle_node.cpp:147` 发布 `/mavros/rc/in`，使用 best-effort、深度 10；best-effort 允许丢弃消息，与此显示订阅相容。
- `src/custom_link_vehicle_node.cpp:504` 原样赋值给 `mavros_msgs/msg/RCIn.rssi`。

生产板旧桥接服务不作为最新代码依据。本实现不按通用 MAVLink 的 254 标度重新换算。0、85、100 原样保留，101、254、255 清除旧值。独立接收时间使用单调时钟；默认 2000 毫秒边界仍有效，2001 毫秒失效，其他遥测不能延长 RSSI 的寿命。

主要代码为 `src/osd/ros_telemetry.*`、`presentation.h` 和 `layout.cpp`。RSSI 标签位置为逻辑 `(22,278)`，轨道为 `(20,330,150,12)`，填充内部宽 148、高 10，数字起点为 `(178,318)`。填充始终为黄色。布局额外拒绝超范围直接快照。

## 本机构建和测试

执行命令：

```sh
./build.sh --test --artifacts-dir .docker-artifacts/rssi-validation --jobs 12
```

平台为 Ubuntu 22.04 x86_64，镜像为 `drone-tracker-rk3576:humble-amd64-rknn2.3.2`，不可变 ID 为 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。编译器 GCC 11.4.0；输出节点确认为 ARM aarch64 ELF。

最终结果：**62/62 CTests 通过**，总耗时 167.20 秒；另一个必需的原生系统内存限制测试 **1/1 通过**，耗时 0.28 秒。首次运行的旧 HUD 断言仍要求未知电量比例隐藏条形；修正为电池比例失效时 RSSI 区域不变后，完整复验通过。没有关闭或跳过该测试。

关键覆盖：

| 检查 | 证据 |
|---|---|
| 输入边界、非法清除、2000/2001 毫秒、独立过期及恢复 | `osd_telemetry_tests` |
| 隔离重映射、best-effort、停止后更新/过期/恢复，状态及旧框不被改写 | `ros_osd_telemetry_tests` |
| 温度不再影响像素，电池/告警不改变 RSSI | `osd_rssi_tests`、`osd_hud_style_tests` |
| 0/10/85/100 填充分别为 0/14/125/148，低值仍黄色 | `osd_rssi_tests` |
| 100→未知→9 清除旧条形和数字、两路径逐像素一致 | `osd_rssi_tests` |
| 720×480、1280×720、跟踪、预览、冻结、稳定缓存及局部更新 | `osd_rssi_tests` |
| 既有跟踪、相机、模型、诊断和进程资源行为 | 完整 62 项测试 |

保留日志：[测试控制台](evidence/test-console.txt)、[逐项 CTest 记录](evidence/ctest-last.txt)、[原生资源检查](evidence/native-resource-limit.txt)及[构建控制台](evidence/build-console.txt)。完整生成产物保存在本工作树 `.docker-artifacts/rssi-validation/backends/amd64/`。实际共享布局快照为 [720×480](evidence/rssi-85-720.png) 与 [1280×720](evidence/rssi-85-1280.png)，它们是程序测试输出，不是板端截图。

本机未配置/提供 `clang-tidy` 和 `clangd`，因此没有执行这两项静态工具检查；编译与运行测试不被表述为这些工具已通过。首次完整编译另有 SDK 工具链空白语法提示和未修改相机计时源码的 `__int128` 警告，最终增量构建成功。本次新增代码没有相关编译警告。

## 生产板定向验证

板卡为 **HZ-RK3576_WuKong Board**，主机 `hzhy`，Ubuntu 22.04.5 LTS ARM64，内核 `6.1.141-rt36`。系统 RKNN Runtime 2.3.2，RKNPU 驱动 0.9.8。系统库 SHA-256 为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。不是开发板 1 或 2。

候选目录为板端 `/home/hzhy/osd-rssi-candidate-20261022`。仅复制候选程序和验证脚本，没有覆盖原安装或系统依赖。节点 SHA-256 为 `6129a858e03d103958e44feb7d5eedc35b3945777a9596a7d9b8e9ce6016142e`。`ldd -r` 没有缺失库或未解析符号，Runtime 解析到系统 `/lib/librknnrt.so`。

输入是 SC233HGS `/dev/video22`、1920×1200 NV12、请求 60 fps 的真实相机。采集前执行 `v4l2-ctl --list-devices` 与 `media-ctl -p -d /dev/media2` 复核连接。显示是 HDMI-A-1、720×480、60 Hz、`cached-overlay`。桌面 `lightdm` 保持 inactive；节点日志确认独占 DRM 控制权。模型沿用原配置：`balloon_best_zoo6_int8_fp16_cls.rknn` 及 FP16 NanoTrack template/search backbone、head；四个文件及其余板上模型指纹均在前后记录中。

验证顺序：

1. 记录原服务、版本链接、配置、模型和 Runtime 指纹。准备 EXIT 恢复处理及候选超时清理。
2. 暂停原 `drone-tracker.service`，在独立 `ROS_DOMAIN_ID=119`、仅本机通信下运行候选。RSSI 重映射到 `/rssi_acceptance/rc`，不会把注入数据发给生产订阅者。
3. 使用原相机和模型，关闭候选录像以免生成业务录像；原配置文件不变。发布 RSSI 85，停止发布至超时，再发布 9。
4. 每阶段用 `kmsgrab --plane 58` 和 `--plane 103` 读取实际扫描中的视频/HUD 平面，按 alpha 合成图片并核对条形中线黄色像素数。
5. 在同一候选进程发送一次有界 ON/OFF，观察跟踪状态 `[0,1,2]`、真实模型处理和 OFF 后持续无框预览。
6. SIGINT 结束候选，退出码 0；恢复并核对原服务。

| 实际 HDMI 截图 | RSSI | 黄色宽度 |
|---|---:|---:|
| [预览有效](evidence/board/preview-85.png) | 85% | 125 像素 |
| [输入过期](evidence/board/preview-expired.png) | --% | 0 像素 |
| [恢复](evidence/board/preview-recovered-9.png) | 9% | 13 像素 |
| [ON/OFF 后预览](evidence/board/preview-after-off-85.png) | 85% | 125 像素 |

第一次板端检查器误把视频平面当作合成结果来核对黄色像素。实际合成截图已经显示正确 85%；恢复原服务后修正检查器，重新执行上述完整定向流程通过。生产代码未因该检查器问题改变。原始 BGRA 平面保留在板端候选目录；本地保存实际合成截图、[结果 JSON](evidence/board/result.json)、[候选日志](evidence/board/candidate.log)、[链接检查](evidence/board/candidate-ldd.txt)与[预检](evidence/board/preflight.txt)。完整脚本为 [board_run.sh](evidence/board_run.sh) 与 [board_check.py](evidence/board_check.py)。

## 服务恢复与限制

原版本链接仍为 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。最终 `drone-tracker.service` 为 **active/running**，PID **23657**；`drone-interceptor.service` 同样为 **active/running**，未修改。候选进程已退出，真实相机日志确认停止与资源回收。

[恢复记录](evidence/board/restoration.txt)以及[验证前指纹](evidence/board/before-fingerprints.txt)/[验证后指纹](evidence/board/after-fingerprints.txt)证明原程序、配置、模型与系统 Runtime 字节一致。第一次检查失败也通过同样恢复流程恢复服务。未升级 `apm_bridge`，未修改 `/opt/drone_tracker/current`，未永久部署候选。

没有进行真实无线链路强度精度、实飞、Jetson 或新增长时间负载验收。本次只对 RSSI 改义进行定向生产板集成检查，不重复历史完整负载矩阵，也不把本次结果当作其替代。注入的 IMU 仅用于启动跟踪冒烟，不能证明姿态精度。相机内参沿用原模拟值。
