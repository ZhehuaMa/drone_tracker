# 独立核验报告：add-osd-coordinates-and-persistent-arm-state

核验日期：2026-10-16，Asia/Shanghai。工作树为 `/home/mzh/.codex/worktrees/osd-gps-armed-status/drone_tracker`，分支为 `codex/osd-gps-armed-status`。本报告由独立核验代理按 `openspec-verify-change` 执行，不修改产品代码，不同步或归档规格。

OSD 指叠加在视频上的信息，HUD 指其中独立于目标框的遥测和状态图形。delta spec 指本变更对现有规格新增或修改的要求。此次核验读取 proposal、design、tasks、两个 delta spec、实际代码差异、相关既有实现、测试和板端原始证据。没有把任务勾选或分析器的单独布尔值当作实现正确的充分证据。

## 结果总览

| 维度 | 结果 |
|---|---|
| 完整性 | 任务 15/15；规格覆盖 10/10：7 项 ADDED、3 项 MODIFIED；没有 REMOVED 或 RENAMED 项。 |
| 正确性 | 10/10 要求完成实现映射；35/35 场景具备相符代码及测试或板端证据，其中遥测规格 14 项、HDMI 规格 21 项。 |
| 一致性 | 设计的 4 项决定均得到遵守；手工代码模式检查完成，未发现显著偏离相邻模式。 |

核心检查共六项：任务完成、规格覆盖、要求实现映射、场景覆盖、设计遵循和代码模式一致性。六项均执行，没有跳过或标为 Not verified 的核心检查。场景覆盖指实现、现有测试与实际验收的综合证据，不表示每个场景都有一个单独命名的自动测试。

问题计数：**CRITICAL 0，WARNING 0，SUGGESTION 0**。

## 工作流与证据身份

以下命令均在上述工作树执行：

```sh
openspec list --json
openspec status --change add-osd-coordinates-and-persistent-arm-state --json
openspec instructions apply --change add-osd-coordinates-and-persistent-arm-state --json
openspec validate add-osd-coordinates-and-persistent-arm-state --strict
git diff --check
```

CLI 确认本地项目根目录、`spec-driven` 工作流和 15/15 完成。没有缺失的跟踪文件或规划工件。严格验证和差异空白检查通过。CLI 上下文要求英文工件，但用户明确提供的项目规则要求 OpenSpec 工件使用简体中文；本报告遵循后者。

运行代码只改动 `src/osd/short_text.h`、`src/osd/short_text.cpp` 和 `src/osd/layout.cpp`。其余已跟踪差异是四个现有测试文件及 `docs/RK3576_HDMI_OSD.md`。未发现 ROS 话题、消息、跟踪状态、模型处理、采集、设备资源管理或线程生命周期的产品代码变更。

## 完整性与要求实现映射

| 规格、要求 | 实现证据 | 验证证据与结论 |
|---|---|---|
| 遥测：经纬度采用带符号的十进制度数 | `src/osd/short_text.cpp:20`、`:31`；`src/osd/layout.cpp:262`；既有订阅 `src/osd/ros_telemetry.cpp:208` | `src/osd/tests/short_text_test.cpp:35` 覆盖正负、六位小数、极值、有效零、负零及逗号小数点环境；文档明确精度不代表 GPS 精度。符合。 |
| 遥测：无效或过期位置替换为坐标占位文字 | 既有接收校验 `src/osd/ros_telemetry.cpp:114`、快照过期 `:151`；格式化和布局分别处理无值 | `src/osd/tests/telemetry_test.cpp:94` 覆盖无定位、任一坐标非法、2000/2001 ms 边界、独立过期和恢复；缓存长短转换及实际显示图补充清除证据。符合。 |
| 遥测：解锁状态按有效飞行状态持续显示 | `src/osd/layout.cpp:348` 无时长或闪烁条件；`src/osd/presenter.cpp:343` 每次使用当前遥测 | `src/osd/tests/layout_test.cpp:182` 对 0、3.1、75 秒、两闪烁相位、实时/等待/冻结、ON/OFF 组合比较；实际冻结后上锁图与 75 秒图符合。 |
| 遥测：未知解锁状态不能冒充已上锁或已解锁 | `src/osd/layout.cpp:348` 对无值及断连生成琥珀 `ARM --`；快照过期规则未改 | `src/osd/tests/telemetry_test.cpp:128`、`src/osd/tests/layout_test.cpp:222` 及板端断连、过期、恢复图符合。 |
| HDMI：固定参考默认布局 | `src/osd/layout.cpp:241` 保留原区域，`:267` 新增右侧坐标，`:347` 迁移触发并居中解锁文字 | 两分辨率逐像素路径对照、同输入基线差异和实际 DRM 扫描图符合。 |
| HDMI：单目标框和锁定装饰 | 既有 `src/osd/layout.cpp:173` 目标生成及 `:406` 空标签规则未改；目标仍最后加入元素序列 `:358` | `src/osd/tests/hud_style_test.cpp:89`、`:93`、`:103` 和 `src/osd/tests/layout_test.cpp:115` 的颜色、角框、菱形、标签无关性、移动及清框检查符合。 |
| HDMI：动态文字尊重数据语义 | 既有电池未知规则 `src/osd/layout.cpp:244`、告警 `:337`、`:353`；触发期限 `src/osd/presenter.cpp:161`、`:353` 未改 | HUD 缺失值、告警/冻结组合、持续预览状态机和 `triggerLifetime` 检查符合；独立触发与底部状态由新布局及实机共存图确认。 |
| HDMI：右侧完整坐标 | `src/osd/layout.cpp:267`：逻辑 y=314/350、完整框右边界 x=702；`:142` 默认主字形含描边 | 最长经度框宽 244、x=458；与 `MAH 00410` 右边界相同。两分辨率软件图和生产扫描图确认未缩字或裁边。符合。 |
| HDMI：解锁与触发独立位置 | `src/osd/layout.cpp:347` 触发在 (22,80)，`:351` 状态框按文本长度在 x=360 居中 | 布局组合、两秒触发期限测试及已解锁/未解锁/未知板端图符合。 |
| HDMI：两路径一致且无残影 | 两路径复用 `scene`；缓存 `src/osd/layout.cpp:475` 按键及边界比较，`:481` 清旧新范围，`:504` 重建变化区 | `src/osd/tests/layout_test.cpp:207` 覆盖双尺寸状态及坐标转换；`:232` 验证稳定字段跨闪烁相位不重画。符合。 |

15 项任务逐项核对完成：1.1–1.5 对应坐标实现、边界测试、快照测试与文档；2.1–2.4 对应持续状态、触发迁移、组合测试与文档；3.1–3.2 对应两路径比较和八幅软件例图及基线差异；4.1–4.4 对应原生检查、ARM64 集成、规格校验和实际生产板显示及性能证据。任务 4.4 使用最后完整执行的受保护证据，没有把旧中断执行算为完成。

## 35 项场景覆盖

以下路径省略共同前缀 `src/osd/`。实际显示图片位于 `evidence/board-visual/`，软件基线差异为 `evidence/software-diff.json`。

| 编号 | 规格场景 | 支持证据 |
|---|---|---|
| T1 | 有效北纬东经 | `tests/short_text_test.cpp:37`；`layout.cpp:262`。 |
| T2 | 有效南纬西经和极值 | `tests/short_text_test.cpp:39`、`:45`；两尺寸最长坐标实机图。 |
| T3 | 零坐标是实际有效数据 | `tests/short_text_test.cpp:42`；`tests/telemetry_test.cpp:99`。 |
| T4 | 舍入后为零不显示负零 | `tests/short_text_test.cpp:43`；`short_text.cpp:23`。 |
| T5 | 未解锁且没有返航点 | `tests/telemetry_test.cpp:102`；`layout.cpp:262` 不依赖 flight/home/GPSRAW。 |
| T6 | 无定位或非法数值 | `tests/telemetry_test.cpp:105`；布局清除检查；`*-invalid-position.png`。 |
| T7 | 定位过期而其他遥测正常 | `tests/telemetry_test.cpp:121`；过期快照两坐标一起清空。 |
| T8 | 定位恢复 | `tests/telemetry_test.cpp:126`；`*-recovered.png` 与区域检查。 |
| T9 | 解锁超过三秒且触发停止跟踪 | `tests/layout_test.cpp:196`；`720-armed-over75s.png` 与触发冻结图。 |
| T10 | 未解锁持续可见 | `tests/layout_test.cpp:207` 状态/任务组合；`:348` 的 flight 显示映射不读取跟踪启停；MISSION 状态另有 presenter 测试。 |
| T11 | 冻结画面上发生上锁 | `*-disarmed-frozen.png`；实际冻结中心区保持不变；布局状态转换检查。 |
| T12 | 启动时尚无飞行状态 | `tests/telemetry_test.cpp:97`；`tests/layout_test.cpp:222` 未知区域比较。 |
| T13 | 状态过期或断连 | `tests/telemetry_test.cpp:130`；`tests/layout_test.cpp:224`；`*-disconnected.png`、`*-stale.png`。 |
| T14 | 连接状态恢复 | `tests/telemetry_test.cpp:134`；`tests/layout_test.cpp:227`；实机恢复图。 |
| H1 | 默认画布展示完整有效数据 | `tests/hud_style_test.cpp:64` 输入完整有效数据及区域/颜色断言；短文字原有单位测试。 |
| H2 | 画布尺寸改变 | `tests/layout_test.cpp:115`、`:207` 双尺寸路径对照；实际两尺寸图。 |
| H3 | 新增遥测不改变主字体 | `layout.cpp:142` 默认主字形；软件及实际图中 MAH/坐标/状态一致。 |
| H4 | 有效 Tracking 单目标 | `tests/hud_style_test.cpp:89`、`:98`、`:99` 的绿角框、外角框和菱形断言。 |
| H5 | 输入提供目标名称或空标签 | `tests/layout_test.cpp:127` 对空、短、80 字符标签逐像素比较。 |
| H6 | 保留独立状态文字与其他 OSD 项目 | `software-diff.json` 八例均在允许区域外零差异；目标绘制无代码修改。 |
| H7 | 任务文字覆盖继续有效 | `tests/hud_style_test.cpp:103` 的状态栏及目标不变断言；实机 `*-mission.png`。 |
| H8 | 两条路径输出一致且移动不留残影 | `tests/layout_test.cpp:112`、`:115`；`tests/hud_style_test.cpp:186`；统一缓存清理。 |
| H9 | 目标丢失或停止 | `tests/layout_test.cpp:144`、`:147`；`tests/hud_style_test.cpp:188` 冻结目标清除。 |
| H10 | 电池数据不完整 | `layout.cpp:244` 及 `short_text` 缺失值测试；`tests/hud_style_test.cpp:172` 未知电量条断言。 |
| H11 | 冻结时仍有告警 | `tests/hud_style_test.cpp:188`；实际触发/冻结/FAILSAFE 组合图。 |
| H12 | OFF 时实时告警 | `tests/presenter_test.cpp:514` 持续预览 OFF 保持 Live 和视频递增；`:297` 最新遥测继续传播；`tests/hud_style_test.cpp:128` Live 无框预览显示告警。共享渲染路径未改。 |
| H13 | 触发提示和持续解锁状态并存 | `tests/layout_test.cpp:202`；`tests/presenter_test.cpp:374` 两秒期限；实际触发及过期图。 |
| H14 | 默认布局与最大长度坐标 | `layout.cpp:267`；最长坐标软件图及 720 扫描图。 |
| H15 | 冻结与告警共存 | 双尺寸组合对照；`720-armed-trigger-frozen-warning.png`。 |
| H16 | 非默认画布 | `tests/layout_test.cpp:207`；1280×720 软件及生产扫描图。 |
| H17 | 已解锁且触发提示生效 | `tests/layout_test.cpp:202`；两秒期限测试及独立位置。 |
| H18 | 未解锁且任务覆盖生效 | `tests/layout_test.cpp:207` 对 mission=true 的未解锁转换；共享状态栏测试。 |
| H19 | 未知状态仍有固定占位 | `tests/layout_test.cpp:208`、`:216` 的 Starting/未知组合；未知软件图。 |
| H20 | 状态变短及坐标失效 | `tests/layout_test.cpp:212` 六次转换、双尺寸 legacy/缓存/全新缓存比较。 |
| H21 | 时间经过但状态未变 | `tests/layout_test.cpp:196`、`:232`；缓存 field 计数只允许既有时钟变化。 |

## 独立执行与原始验收复算

核验代理使用独立 `/tmp/osd-gps-independent-verification-20261016` 目录重新执行真实软件绘制流程：

```sh
bash openspec/changes/add-osd-coordinates-and-persistent-arm-state/evidence/generate-software.sh /tmp/osd-gps-independent-verification-20261016
```

镜像为 `drone-tracker-rk3576:humble-amd64-rknn2.3.2`，身份 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。源挂载只读、容器无网络。新编译、9/9 CTest 及附加示例图生成全部通过；CTest 用时 2.97 秒。独立测试原始日志为 `evidence/verification-native-ctest.log`。这次新执行是 x86_64 原生检查，不是硬件验证。

ARM64 完整集成证据 `evidence/integration.log` 为 57/57 通过，输出架构记录确认为 ARM aarch64。核验没有无必要地再运行完整集成；读取并核对了当前代码对应的既有构建证据。遥测扩展测试由完整 ROS 集成测试执行，原生九项独立 OSD 测试不包含这项 ROS 测试。

核验从 `evidence/guarded-fan-resume-20261016/` 的两个原始压缩包分别解出比较与持续运行数据，使用 conda base Python 重新运行 `analyze_comparison.py` 与 `analyze_endurance.py`。复算结果与保留分析一致：九案例完整、三个缓存案例全部达标、受保护执行完整、同一 PID 的 20 轮与连续 610.130 秒通过。

| 缓存组 | 跟踪 fps | 相对关闭 OSD 中位数下降 | render_total 均值 ms | P95 桶上界 ms | HDMI 新视频 fps | submit_to_display 均值 ms |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 58.561 | 0.576% | 15.797 | 32.768 | 58.469 | 20.645 |
| 2 | 58.607 | 0.498% | 15.259 | 32.768 | 58.444 | 19.286 |
| 3 | 59.131 | -0.390% | 15.605 | 32.768 | 59.055 | 19.945 |

关闭 OSD 中位数为 58.901 fps。每个案例完整负载不少于 75 秒，去掉前 10 秒后仍有约 65 秒。既有门槛分别为吞吐下降不超过 5%、渲染均值不超过 40 ms、P95 不超过 65.536 ms、HDMI 新视频至少 25 fps、提交至显示均值不超过 60 ms。P95 桶上界是计时器统计的近似界限，不是精确百分位。

同一 PID 4566 完成 20 轮并继续运行 610.130 秒。稳定窗口跟踪约 58.723 fps、HDMI 新视频 58.598 fps、渲染均值 15.575 ms、提交至显示均值 20.013 ms。持续执行日志没有失败计时或运行期关闭显示；线程 16、文件描述符 52，分钟内存中位数没有持续增长。比较与持续执行最高温度分别为 66.538°C、63.769°C，均未触发 95°C 保护。

## 两个退出期失败样本的独立判断

原始比较日志不能称为零失败。第二、第三组缓存案例各有一个 `repeat result=failure count=1`；两条均没有新视频样本。核验独立读取了节点日志、JSON 中的窗口结束时间和保护日志的 `node_stop` 事件：

| 案例 | 满负载结束 Unix 时间 | 发送 SIGINT 时间 | 失败计时输出时间 |
|---|---:|---:|---:|
| comparison-5-cached-overlay | 1792142916.005932 | 1792142916.620853 | 1792142916.770863 |
| comparison-8-cached-overlay | 1792143147.403577 | 1792143148.018759 | 1792143148.169932 |

失败发生在停止信号之后约 150 ms，且在测量窗口之后约 765 ms。未改动的 `src/osd/presenter.cpp:55` 先置 stopping，再调用后端 interrupt 并等待显示线程。`src/platform/rk3576/osd/hdmi_backend.cpp:379` 的刷新等待因唤醒事件抛出 `OSD display interrupted`；presenter 的异常路径在 `:464` 计为失败并转到后端 shutdown。这里是代码和事件时序支持的解释，原始日志本身没有直接输出具体异常文字。

主规格 `openspec/specs/rk3576-hdmi-osd/spec.md:85` 的“等待刷新时退出”要求等待可结束或有界超时并安全回收，不要求退出取消计时为零。两个节点退出码均为 0，无运行期 `OSD disabled after display failure`，后续案例及正式服务可重新使用设备。因此这些退出样本不构成缺失实现、运行期性能超限或 CRITICAL 问题，也不据此要求扩大本变更范围修改既有退出诊断。

## 设计与代码模式一致性

四项设计决定逐一得到实现：使用已有快照；主字体和固定右边界；独立持续的可信解锁元素；复用统一元素及变化区域缓存。未增加遥测来源、显示线程或硬件依赖。坐标格式化先做范围和有限性验证，再以固定数字区域设置输出。新增字段沿用相邻的命名空间、可选值、局部初始化和 `addText` 约定。

八幅同输入软件基线差异在允许区域外的变化像素均为零。新元素使用稳定 ID；缓存按照旧新边界清理。目标绘制和层顺序未改。没有发现需要列为 SUGGESTION 的显著模式偏离。

## 生产环境恢复与核验边界

生产目标为 `ssh firefly-production` 的 `HZ-RK3576_WuKong Board`，Ubuntu 22.04.5 ARM64、`6.1.141-rt36`，系统 RKNN Runtime 2.3.2 与 RKNPU 0.9.8。使用隔离新节点、既有真实模型和固定 1920×1200、60 fps 回放；遥测为明确注入的合成数据。私有节点 SHA-256 为 `71be771a0914f3939394dffad120337896ec08c25f27cbbd1ae2bf7aca757ca9`，固定录像 SHA-256 为 `dae6fad1f234a94fea2280a48c590ec17c657cb7901b984d0fcb5edb52ed01e5`。完整模型指纹、配置和最终恢复记录保存在原始基线与 `final-restoration.txt`。

本次核验通过 `view_image` 独立检视了 720×480 的触发/冻结/告警组合图和 1280×720 的未解锁冻结图。两者来自实际 DRM 扫描平面合成，完整坐标和底部状态分别可读。软件图、实际扫描像素和生成参考示意图的身份已明确区分。

17:48:37 再次只读 SSH 检查：`drone-tracker`、`apm-bridge`、`drone-interceptor` 全部 active/running 且 enabled；lightdm 保持 inactive/dead。启动标识仍为 `fbbe7a33-de93-4343-961f-1d4804c8e51c`，发布仍为 `bundle-20260927T070059Z-2281`。相机配置、CPU 策略和 Runtime 指纹保持基线，没有私有验收节点或脚本残留。核验过程没有停止或修改任何服务，记录为 `evidence/verification-board-readonly.txt`。

以下额外事项未执行，不把它们称为通过：不可用的 `clang-tidy`/`clangd` 自动静态分析、Jetson 硬件验证、新的真实相机长测、真实定位精度、实飞准确率，以及显示器物理亮度或观看距离测量。它们不属于本次变更的六项核心核验检查，也不是此 OSD 字符与布局变更新增的验收条件。旧过温中断和误拔电源中断记录完整保留，没有用本次通过覆盖历史失败。

## 按优先级列出的问题

- **CRITICAL：0。** 没有未完成任务或缺失要求实现。
- **WARNING：0。** 没有识别出规格/设计偏离或需要单列的场景缺口。
- **SUGGESTION：0。** 没有显著代码模式问题。

全部适用核心检查通过，可以进入后续归档流程。此判断不执行归档或发布，也不触发对 WARNING/SUGGESTION 的额外修复。后续任务 3 没有需要修复的 CRITICAL 项。
