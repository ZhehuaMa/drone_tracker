# 独立验证报告：replace-osd-temperature-and-battery-gauge-with-rssi

## 范围与结论

2026-10-22，由独立于规划和实施的子代理执行第一轮验证。验证依据为本 change 的 proposal、design、两份 delta specs 和 tasks，以及工作树中实际实现、测试和生产板证据。delta specs 是本次对既有能力的增量要求，不表示主规格已经同步。

初次审查及最终结果均为 **CRITICAL 0、WARNING 0、SUGGESTION 0**。CRITICAL 表示阻止完成的缺陷，WARNING 表示规格、设计或场景覆盖方面需要修复的问题。没有发现需要进入修复阶段的问题，没有修改实现、测试或既有规划工件。只新增本报告和独立复验日志。

## 总览

| 维度 | 结果 |
|---|---|
| 完整性 | CLI 聚合任务 11/11 完成；10/10 个 ADDED 或 MODIFIED requirement 找到实现和证据；无 REMOVED 或 RENAMED requirement |
| 正确性 | 10/10 个 requirement 完成实现映射；30/30 个场景有实现及测试或板端证据支持 |
| 一致性 | 4 项设计决策均遵循；代码边界、锁、时钟、命名及共享布局符合相邻模式 |

本 skill 定义的任务完成、规格覆盖、实现映射、场景覆盖、设计遵循和代码模式一致性六项检查全部执行，没有跳过项。

## 规格与场景映射

下表中的源码路径均相对于本工作树。测试通过记录见 `evidence/test-console.txt` 和本轮 `evidence/verify-1-targeted-tests.txt`。

| 能力与 requirement | 实现 | 场景证据 |
|---|---|---|
| `osd-ros-telemetry`：外部遥测经已有消息类型输入 | `src/osd/ros_telemetry.cpp:197` 的既有适配器新增 RCIn best-effort 深度 10 订阅，沿用 ROS 重映射；未更改控制输入 | 4 个场景：既有可选适配构建边界与 CMake；`src/tests/ros_osd_telemetry_test.cpp:42` 的重映射、QoS、独立雷达及停止状态检查；`src/osd/tests/hud_style_test.cpp:104` 与 presenter 任务覆盖测试 |
| `osd-ros-telemetry`：RSSI 使用生产桥接的百分比契约 | `src/osd/ros_telemetry.cpp:75` 原样接受 0–100，其他值立即清空；`src/osd/presentation.h:55` 的独立测量字段 | 2 个场景：`src/osd/tests/telemetry_test.cpp:94` 覆盖 0、85、100 和 101、254、255；每次非法值之前先写入有效 85 |
| `osd-ros-telemetry`：RSSI 有效期与电池及图像更新独立 | `src/osd/ros_telemetry.cpp:156` 独立过期；`src/osd/ros_telemetry.h:43` 使用单调时钟及包含边界的期限判断；图像通路不更新 RSSI 接收时刻 | 3 个场景：遥测单测检查 2000/2001 毫秒、电池/温度独立性与恢复；ROS 集成测试检查停止后的接收、过期及恢复且不恢复旧框 |
| `rk3576-hdmi-osd`：HDMI HUD 采用固定参考版本的默认视觉布局 | `src/osd/layout.cpp:237` 的共享场景保留布局，仅替换指定 RSSI 区域 | 3 个场景：HUD 测试完整正常状态；布局及 RSSI 测试的 720×480/1280×720；坐标与持续解锁测试检查主字体和位置 |
| `rk3576-hdmi-osd`：单目标框采用参考角框和锁定装饰 | 目标绘制代码未改；RSSI 只增加 HUD 元素 | 6 个场景：`src/osd/tests/layout_test.cpp` 的空/非空标签、移动和停止；HUD 测试的角框、锁定菱形、任务横幅；`src/osd/tests/rssi_test.cpp:81` 检查 RSSI 变化不影响目标层和其他 HUD |
| `rk3576-hdmi-osd`：动态文字沿用参考视觉但尊重现有数据语义 | `src/osd/layout.cpp:314` 的 meter 只取 RSSI，原电池读数、告警和触发位置未改 | 4 个场景：`src/osd/tests/hud_style_test.cpp:163` 的缺失电量与冻结告警；layout 测试的预览告警及独立触发/解锁；板端 ON/OFF 后仍显示 RSSI |
| `rk3576-hdmi-osd`：温度行由固定 RSSI 标签取代 | `src/osd/layout.cpp:276` 的白色主字体 RSSI；删除温度元素；没有标签旁数字 | 1 个场景：`src/osd/tests/rssi_test.cpp:36` 的温度未知、31、-25 输出像素一致；源码检查不再绘制 TMP；板端截图确认 |
| `rk3576-hdmi-osd`：黄色条与旁边百分数共同表示 RSSI | `src/osd/layout.cpp:314` 保持轨道和文字位置，使用整数 `148*p/100`，填充固定 AMBER | 3 个场景：RSSI 测试检查 0/10/85/100 宽度及颜色、电池/告警独立性；实际 HDMI 85% 对应 125 像素 |
| `rk3576-hdmi-osd`：RSSI 未知和恢复时不残留旧填充 | 同一共享元素为未知生成空轨道和 `--%`，与有效零的 `0%` 区分 | 2 个场景：RSSI 测试比较 100→未知→9 和带电池的未知初态；板端截图确认过期后无填充，恢复 9% 后为 13 像素 |
| `rk3576-hdmi-osd`：RSSI 两路径一致并保持局部更新 | 沿用既有 element 缓存键、脏区清除和统一缩放；未新增平台逻辑 | 2 个场景：`src/osd/tests/rssi_test.cpp:67` 在两种画布、跟踪/预览/冻结下逐像素比较缓存和完整路径；稳定绘制不增加字段工作，仅 RSSI 变化不影响区域外像素 |

场景审查同时使用源码确认标签、百分数的精确格式和固定位置。没有仅凭“两条路径彼此相等”推断文字内容正确。

## 来源和设计核对

再次只读检查 `/home/mzh/nickluo/apm_bridge` 的 `rk3576` 分支，提交仍为 `390668d2d822731e95651cd3b88547e5912e66a1`。`include/custom_link/protocol.h:57` 明确 0–100；`src/custom_link_vehicle_node.cpp:147` 使用 best-effort 深度 10；`:504` 原样赋值到 `RCIn.rssi`。因此 85 显示 85%，没有误用通用 MAVLink 的 254 标度。旧板端桥接版本不是该契约的依据。

四项设计决策均对应实际实现：直接百分比；显示侧拥有独立值和单调接收时间；原位替换且不重复数字；局部像素、真实 ROS 订阅和生产板注入验证。没有新增线程、设备缓冲区或异步所有权。已有 mutex 继续保护接收与快照，ROS 类型没有进入绘图层。

## 独立复验与生产恢复

- 复用并审查完整 **62/62 CTests** 和额外原生资源限制测试通过的原始日志。
- 在同一固定镜像、既有 ARM64 构建目录中独立运行 `osd_layout_tests`、`osd_hud_style_tests`、`osd_rssi_tests`、`osd_telemetry_tests`、`ros_osd_telemetry_tests`，**5/5 通过**，总耗时 12.00 秒。记录为 `evidence/verify-1-targeted-tests.txt`。没有重新构建或扩大测试范围。
- `openspec validate replace-osd-temperature-and-battery-gauge-with-rssi --strict` 通过；`git diff --check` 通过。
- 独立查看实际生产 HDMI 截图与检查脚本。四阶段中线填充像素数为 125、0、13、125，与 85%、过期、9%、OFF 后 85% 一致。跟踪冒烟取得状态 0、1、2，候选正常退出。
- 比较 `evidence/board/before-fingerprints.txt` 与 `after-fingerprints.txt`，字节一致。保存恢复记录确认原 tracker/interceptor 服务均运行。
- 本轮仅做一次只读 SSH 状态复核，先加载 ROS 环境。`evidence/verify-1-board-status.txt` 再次确认两个服务 active/running、原版本链接及系统 Runtime 哈希。没有停止服务或重跑相机。
- 原主工作区 `git status --short` 为空。本轮未提交、推送、同步主规格、归档或永久部署。

## 问题与限制

### CRITICAL：0

无。

### WARNING：0

无。

### SUGGESTION：0

无。

本次的 RSSI 板端输入为明确标注的注入值，不构成真实无线强度精度、实飞或视频链路质量验收。既有记录明确 `clang-tidy`、`clangd` 未提供；本轮没有声称执行这些额外静态工具。Jetson、实飞及新增长期负载不在本次定向改义范围内。这些限制不作为已通过的测试，也没有发现由其引出的规格或设计问题。

所有本轮 OpenSpec 验证检查通过，没有 CRITICAL 或 WARNING。无需修复或追加验证循环；是否归档由后续独立请求决定。
