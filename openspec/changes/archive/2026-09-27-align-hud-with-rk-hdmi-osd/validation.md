# HUD 对齐实施与验证记录

## 固定参考版本

参考仓库为本机 `rk_hdmi_osd`，提交为 `3e605a26c7c135b468ef2a3638ffd47144594603`。以下坐标以 720×480 左上角为原点。参考来源为 `layout/layout.cpp`、`osd/osd.cpp`、`osd/osd.h`，以及 `osd_bridge/src/pipeline.cpp` 和 `bridge_node.cpp`。HUD 是视频上的飞行与跟踪信息层；表内坐标是绘制起点，不是文字外接矩形。

| 区域 | 参考位置与参数 | 真实数据与演示数据边界 |
|---|---|---|
| 字形与主题 | 主字 16×32，小字 8×16；白字黑色 2 像素描边。白色 `(235,235,235)`、琥珀 `(255,200,60)`、红 `(255,70,70)`、绿 `(90,230,90)`、青 `(80,220,255)`、蓝 `(90,150,255)`，表中颜色为 RGB。 | 字形来自固定字库；运行时不读取参考仓库。 |
| 四角与标题 | 四角起点距边 16 像素，角标边长 30、厚 2；机名 `(22,10)`，小字副标题 `(22,48)` 为 `RK3576 OSD NTSC`。 | 机名使用配置；副标题是主题常量。 |
| 左列 | 高度、速度、航向、卫星、温度依次从 `(22,118)` 起，每行相隔 38 像素；温度用小字。 | 地速由 m/s 转 km/h；定位等级须用真实 GPS fix_type，不能由卫星数猜测。 |
| 右列 | 模式 `(554,10)`；跟踪 `(506,48)`；解锁计时 `(570,86)`；电池、电芯、电流、已耗电量从 `(554,162)` 起每行相隔 38 像素。 | 单节电压、已耗电量与剩余比例未知时显示未知或隐藏，不能填零。 |
| 上中与中心 | 返航箭头中心 `(360,58)`、尺寸 22，距离文字 `(312,86)`；红色圆环准星中心 `(360,240)`、半径 22；俯仰梯每 5° 一级，10° 级较长。 | 返航依赖新鲜位置、姿态及返航点；过期即隐藏。准星不是目标锁定信号。 |
| 左下与底部 | 电量条 `(20,330)`、150×12；本地时钟 `(22,390)`，帧号 `(22,430)`；消息 `(200,390)`，解锁或触发 `(296,430)`，告警 `(570,430)`。 | 电量条只用可信剩余比例。帧号使用本项目已消费帧序号；中文冻结/故障提示保留优先显示区。 |
| 目标框 | 普通角厚 2、锁定角厚 3；每角长度为短边三分之一并限制在 10～36 像素。锁定外角框向外扩 6 像素，中心菱形半径 8。标签底板高 38 像素，优先置于框上方。 | 参考测试 `sim_boxes()` 构造 `UAV`、`BIRD` 两个目标，并伪造 `90%`、`T01`、`T02`。本项目仅显示真实单目标及本地状态，绝不复制这些演示值。 |
| 状态与动画 | Detecting 白、TargetFound 青、Tracking 绿、TargetLost 红；严重告警红色闪烁。触发条持续约 2 秒。 | 触发来自已有 `/fpv/tracker_trigger`；只允许当前显示轮次使用。 |

参考项目的 `osd_bridge` 从 MAVROS 电池、GPS、姿态等话题收集遥测，并有独立的双目标模拟分支。该桥接节点和模拟分支不是本项目的数据源。本项目使用已有显示侧快照与同次消费的单目标框。

## 纯 CPU 快照基准

在现有离线 ARM64 容器中执行，参考源码只读挂载。构建和运行不需要 DRM、RGA、相机或 NPU：

```sh
docker run --rm --network none --platform linux/arm64 \
  -v /Users/mazhehua/projects/rk_hdmi_osd:/src:ro \
  -v /private/tmp/rk-hud-reference-build:/build \
  drone-tracker-rk3576:humble-arm64-rknn2.3.2 \
  bash -lc 'cmake -S /src -B /build -DOSD_WITH_HW=OFF -DCMAKE_BUILD_TYPE=Release && cmake --build /build --target test_layout_snapshot -j4 && mkdir -p /build/snapshots && /build/test_layout_snapshot /build/snapshots && sha256sum /build/snapshots/*.ppm'
```

测试的 10 项断言全部通过。第 100 帧是正常快照，第 150 帧是严重告警快照；两帧的模拟框已移动。再次运行相同测试到另一目录，四张原始 PPM 文件逐字节相同。将 PPM 无损转为本变更 `reference/` 中的 PNG，仅为节省仓库空间。

| 快照 | 原始 PPM SHA-256 | 入库 PNG SHA-256 |
|---|---|---|
| `layout_hud_100` | `e2032b6baf77bb0c960600119edf66b15a80c98a86ac11fcba159bc832c63225` | `cae3eb13b80a86e7683eb84b8982ef662295ca4b648c048af06eea3c9c6b9c39` |
| `layout_det_100` | `4a210ee7bd1581cce9e0dac6d517e24dbc4573867b641e3206c463afc6bcb408` | `f6e36a40aef2f54ffc489177364f883c924fe27c065fbccd4e8db6b4d15f9ad8` |
| `layout_hud_150` | `43468d3d9aba42bc266f162480fbf5d0601840b63b8e56f3e31527142f011cc4` | `cd4f9d01fe746b7c7949389fe50313d4874b8f175f91614a7f248e0cdfc0afe5` |
| `layout_det_150` | `aea995357602e6751f9bb4cffb8daaaf1dde50a0b9066731632e42204de45494` | `0dfd6dd7c7e26acd205fabd0237de9e2133388689bf41582024f2bc9d3f7a327` |

## 字体来源与许可

参考 `gen_font.py` 将 DejaVu Sans Mono 的 ASCII 32～126 字符生成 16×32 和 8×16 抗锯齿字库。用 SHA-256 为 `39c29931201f08dd89fdba4129c76288e6baeecf7c94fe4f6a757f2b50718b1b` 的 `DejaVuSansMono.ttf` 和 Pillow 11.2.1 重新生成，得到与参考 `osd/osd_font.h` 相同的 SHA-256：`8472577789f343ff7d22a504aa25a763c355d7ad2cc92786451e89c4987b1db4`。因此字形来源已逐字节核实。

所需字库已放入本仓库 `src/osd/reference_font.h`。字体版权与允许软件捆绑再分发的原文保存在 `src/osd/DEJAVU_FONT_LICENSE.txt`。字库是源码内资源，编译与运行均不访问 `rk_hdmi_osd` 目录。现有中文安全提示字形及其独立许可仍保留在 `src/osd/status_glyphs.h` 和 `FONT_LICENSE.txt`。

## 实施快照与逐区域差异

本仓库快照由 `osd_hud_style_tests` 生成，产物保存在 `comparison/`。运行 `python comparison/compare.py`（需 Pillow）即可重算下表；脚本把本仓库透明 HUD 合成到参考测试使用的 RGB `(100,100,100)` 背景，把目标层合成到 RGB `(20,20,20)` 背景。差异图的非黑像素表示两者 RGB 不同。两侧输入统一使用 `OFFBOARD`、22.2 V、4.3 A、410 mAh、102 m、8.3 m/s、214°、12 颗卫星且 3D 定位、3° 俯仰、-2° 横滚、240 m 返航、75 秒解锁时长与固定本地日历时间。告警场景使用 20.1 V、10400 mAh、95 m、220°、无返航与触发 ON。目标框尺寸与位置取参考测试第 100、150 帧的第一模拟框，并经过本仓库相机到视频画布的映射。

| HUD 区域 | 正常差异像素 | 告警差异像素 | 解释 |
|---|---:|---:|---|
| 标题与副标题，22500 像素 | 0 | 0 | 位置、字形、描边与颜色相同。 |
| 左侧遥测，55000 像素 | 0 | 0 | 同值输入完全一致。 |
| 右侧遥测，71300 像素 | 58 | 58 | 仅右上角角标横线。参考实现向左绘制时传入负宽度，CPU 裁剪掉横线；本仓库保留规格要求的完整四角角标。 |
| 返航箭头与距离，17100 像素 | 0 | 0 | 返航有效时箭头、文字一致；告警时均隐藏箭头并显示未知标记。 |
| 准星与俯仰梯，36900 像素 | 75 | 75 | 差异位于俯仰梯线段端点，来自本仓库双精度姿态到参考单精度线段取整的边界；中心圆环与级数位置一致。 |
| 电量条，16500 像素 | 147 | 147 | 差异集中在百分号右侧。参考布局的俯仰梯恢复区域与电量文字尾部重叠，后续恢复擦掉百分号部分像素；本仓库分别缓存两元素，不擦除电量文字。 |
| 底部信息，72000 像素 | 344 | 6210 | 正常差异来自真实帧号 `F000100` 与参考约 10 Hz 字段刷新留下的 `F000096`，以及角标。告警额外差异来自参考演示消息 `LINK LOST 12S` 和第 150 帧不可见的告警闪烁相位；本仓库显示真实可得的 `BATTERY CRITICAL` 与独立告警。触发 ON 位置相同。 |

目标层的整区差异不能当成纯绘图误差：参考测试同时画 `UAV` 和 `BIRD` 两个人造目标，标签带 `90%`、`60%`、`T01`、`T02`；它还把第一目标固定为红色。本仓库只画与视频同帧配对的一个目标，Tracking 用绿色，标签为本地状态，不造置信度和编号。对照图可见第一目标的角框、外角框、菱形和底板位置按相同框几何落位；其余差异由上述演示字段、第二目标及状态色解释。冻结快照 `comparison/hud-frozen.png` 没有参考同义画面，因为参考演示没有本仓库必须保留的中文冻结提示。

参考代码的角标负尺寸、百分号擦除和约 10 Hz 帧号刷新是固定版本的实际行为。本仓库按本变更规格保持四角完整、百分比可读和真实已消费帧号。它们是已定位的差异，不是未比较区域或生产 HDMI 验收结论。

## ARM64 构建与无硬件回归

在本机离线 ARM64 容器 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 中运行 `./build.sh --test`，启用 RK3576、HDMI 和录像的 Release 构建成功。CTest 为 **42/42 通过**，日志位于 `.docker-artifacts/log/build-console.log` 与 `.docker-artifacts/log/test-console.log`。`osd_hud_style_tests` 对正常、字段缩短、告警闪烁、移动框、冻结及 720×480/1280×720 切换逐像素比较 `legacy` 与 `cached-overlay`；稳定画面缓存计数不增加。`osd_presenter_tests` 检查帧框配对、旧轮拒绝、OFF 清框与显示故障清理；`osd_telemetry_tests` 检查遥测新鲜度和过期；`osd_short_text_tests` 检查数值与时钟格式。OpenSpec 验证通过。

构建容器只挂载本仓库，不挂载 `rk_hdmi_osd`。源码、CMake、启动和参数目录的路径检查没有找到对参考目录的引用。安装树含 `DEJAVU_FONT_LICENSE.txt` 和原有 `FONT_LICENSE.txt`。容器没有目标工具链的 `clang-tidy` 或 `clangd`，因此没有运行匹配 ARM64 编译数据库的静态检查；这不影响 C++ 编译与 CTest 结论。

## 生产板部署与实屏证据

2026-09-24 在生产 `HZ-RK3576_WuKong Board`（主机 `hzhy`，Ubuntu 22.04.5 ARM64，内核 `6.1.141-rt36`）部署本次 HUD 节点。部署前使用 `v4l2-ctl --list-devices` 和 `media-ctl -p -d /dev/media2` 核对：SC233HGS 的 RKISP 主路径仍为 `/dev/video22`。原服务在部署前处于 `active`，桌面服务停止，显示设备由节点独占。部署包只替换通过 ARM64 容器 42/42 测试的主节点，并随包加入 DejaVu 字体许可；`deploy.sh --check` 检查 ARM64 程序、动态库、模型、设备和 CPU 策略后通过。正式部署包为板端 `/tmp/drone-hud-bundle/drone-tracker-hud-20260924T132045Z.tar.gz`，SHA-256 为 `cac4445b32d2b314ada488267668d792be155e220a8acafe42597dfdd9e7124e`。

| 身份项 | 生产板实测值 |
|---|---|
| 当前发布 | `/opt/drone_tracker/releases/bundle-20260924T132146Z-3350`；旧发布 `/opt/drone_tracker/releases/bundle-20260924T093717Z-9343` 保留；回退备份 `/opt/drone_tracker/backups/20260924T132146Z-3350` |
| 主节点 SHA-256 | `6057d9393e655519c9c5dd6e0862d297204f8c37e39014653bca880a19534b3d` |
| 相机配置 SHA-256 | `91e6b85400545dc4a1498251bf132c1cace64b6654b5e02237dbfd81214fdb69`；输入 `/dev/video22`，1920×1200、60 fps，媒体设备 `/dev/media2` |
| CPU 策略 SHA-256 | `4f46293201355131c1e72c8a72cdfaafa2c3121eaa48fe0b5ad69ac442e4f01b`；role-pinned，显示线程 CPU 6 |
| 模型 SHA-256 | YOLO `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7`；NanoTrack template `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2`、search `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44`、head `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |
| 显示与系统依赖 | `/dev/dri/card0`、`HDMI-A-1`、720×480、实测 60 Hz；系统 RKNN Runtime 2.3.2，`/usr/lib/librknnrt.so` SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`（与部署前相同）；RKNPU 驱动 0.9.8 |

部署后服务正常启动，日志确认 DRM 独占控制权、720×480/60 Hz 及系统模型加载。通过 `/fpv/tracker_trigger` 的 `ON`、`OFF` 触发真实相机轮次；ON 时 `OSD submitted` 按约 60 fps 增长，OFF 后不再增加，新画面冻结。生产板自带 `kmsgrab` 从 HDMI 的 DRM 视频平面 58 与 HUD 平面 103 读回扫描缓冲区。本目录 `comparison/production-overlay-off.png` 为实际 HUD 平面待机截图；`production-on.png` 与 `production-frozen.png` 由同一时刻的实际两平面像素按透明度合成。该步骤读取显示缓冲区，不调用主机布局测试或参考仓库模拟画面。当前真实相机环境没有可辨识目标，后续目标框观察使用板端固定真实视频回放。

生产板的固定回放冒烟测试使用 `/home/hzhy/hdmi-osd-optimization-20260924/fixed-drone-1920x1200-650s-60fps.avi`，本次节点工作 15 秒，收到 468 条框消息，其中 453 条框有效；测试后正式服务恢复。下文记录完整的性能、视觉状态和持续负载验收。

## 生产板固定输入 A/B 性能对照

同板固定回放文件 SHA-256 为 `dae6fad1f234a94fea2280a48c590ec17c657cb7901b984d0fcb5edb52ed01e5`，尺寸 1920×1200、60 fps。为与已有无人机测试输入匹配，YOLO 使用板端真实模型 `/opt/drone_tracker/models/yolo.rknn`（SHA-256 `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3`），NanoTrack 三模型的指纹与上述生产配置相同。测试只修改进程私有参数文件，不改正式 `/opt/drone_tracker/config/camera.yaml`；每组结束后检查该配置和 `current` 发布路径逐字节不变，并恢复原服务。

`acceptance.py comparison` 以 OSD 关闭、`legacy`、`cached-overlay` 交替运行。各组从首个跟踪结果起测量 75 秒以上，统计排除前 10 秒的完整窗口，因此稳定阶段约 65 秒。P95 是计时器 2 的幂分桶的保守上界，不是精确分位数。结果文件位于板端 `/home/hzhy/hdmi-hud-acceptance-20260924/comparison-analysis.json`，原始窗口日志与资源样本位于该目录 `evidence/`。

| 组别 | 稳定阶段 s | 跟踪消费 fps | HDMI 新画面 fps | `render_total` 均值 / P95 上界 ms | `submit_to_display` 均值 ms | 相对 OSD 关闭下降 |
|---|---:|---:|---:|---:|---:|---:|
| OSD 关闭 | 65.12 | 32.23 | — | — | — | 基线 |
| `legacy` 1 | 65.06 | 30.82 | 20.13 | 46.31 / 131.072 | 63.07 | — |
| `cached-overlay` 1 | 65.19 | 31.89 | 31.55 | 21.04 / 65.536 | 23.33 | 1.07% |
| `legacy` 2 | 65.17 | 30.52 | 19.14 | 48.76 / 131.072 | 64.87 | — |
| `cached-overlay` 2 | 65.19 | 31.69 | 31.48 | 22.06 / 65.536 | 23.83 | 1.68% |
| `legacy` 3 | 65.02 | 30.19 | 19.04 | 48.51 / 131.072 | 64.82 | — |
| `cached-overlay` 3 | 65.05 | 32.77 | 32.47 | 22.00 / 65.536 | 24.36 | -1.67%（提升） |

三组缓存路径均达到 `render_total` 均值 ≤40 ms、P95 上界 ≤65.536 ms、HDMI 新画面 ≥25 fps、提交到显示均值 ≤60 ms，以及跟踪消费下降 ≤5% 的门槛。日志中三组新视频显示失败样本均为 0，正式服务在对照完成后恢复 `active`。P95 恰好等于门槛，按规格的“≤”关系通过；它不表示单个样本没有超过 65.536 ms。

## 生产 HDMI 视觉场景

实际画面由生产板 `kmsgrab --plane 58` 与 `kmsgrab --plane 103` 读取当时 DRM 正在扫描的视频层和透明 HUD 层。板端原始 BGRA 文件、捕获时间、有效/丢失框计数及恢复记录存放在 `/home/hzhy/hdmi-hud-acceptance-20260924/visual/`；本地留档在 `.docker-artifacts/production-hud-20260924/visual/`。`comparison/production-visual-*.png` 是把这两层真实像素按 alpha 无损合成后的 720×480 检视图，不是主机布局模拟。两层读取是顺序执行，动态图像可能相差一个显示刷新；配对语义另由同进程轮次日志及 OSD 测试验证。

为使目标确实消失，以固定真实回放的前 12 秒加 6 秒全黑帧生成独立测试视频；生成脚本为 `production_loss_video.py`，板端文件 `/home/hzhy/hdmi-hud-acceptance-20260924/visual-loss-1920x1200.avi` 的 SHA-256 为 `8d62cf49ed4ce5dda351ed8e02ae9450bc4e04d9eaab3446e7000d6e6dc5a74c`。黑帧是显式测试刺激，不作为真实无人机图像或模型真值。`production_visual.py` 以真实 RKNN 三模型处理该输入，并只通过已有 ROS 遥测话题提供已知值。

| 场景 | 实屏证据与 720×480 参考区域核对 |
|---|---|
| 正常跟踪和返航 | `production-visual-normal-target-home.png`：四角角标、机名和副标题，左/右信息列、准星、俯仰梯、电量条、单目标绿框、外框、菱形和标签均落在参考布局区域。测试遥测设置机头朝东、返航点在西侧，返航箭头按相对方向朝下，距离约 193 m；没有参考演示的第二目标或伪造置信度。 |
| 缺失遥测 | `production-visual-missing-telemetry.png`：停止发布后超过 2 秒，新鲜度到期，GPS、电池、姿态、返航、解锁时长等值恢复 `--` 或隐藏；视频与真实目标框继续。 |
| 告警闪烁 | `production-visual-warning-a.png` 与 `production-visual-warning-b.png` 相隔约 0.3 秒：底部右侧 `FAILSAFE` 从隐藏变为红色，中央实时消息仍保持红色可读。连续六张实屏截图 `warning-a`～`warning-f` 的右下红像素计数依次为 0、607、0、607、607、0，符合约 300 ms 的闪烁节奏。计数在原始 HUD 平面的 `x=570..714, y=420..469` 区域执行，条件为 alpha>100、红通道>150 且大于绿/蓝通道的 1.5 倍。 |
| 目标消失 | `production-visual-target-lost.png`：第 410 帧仍处于真实视频片段，已经连续至少 10 条无效框消息，状态转为扫描，目标角框、外框、菱形与标签全部消失。此例在附加黑帧到来之前自然丢失目标。 |
| OFF 与新轮 | `production-visual-off-frozen.png` 保留最后实际显示的底图并显示中文“画面已冻结：输入已停止”，清除目标装饰；`production-visual-restart-round.png` 的真实帧号从新轮起算且目标框重新出现。 |

参考 CPU 画面与上述真实 HDMI 画面的背景和输入值不同，因此这里按已固定的区域、字形、调色板、图形位置和动态规则逐项核对；逐像素同输入差异另见前文 CPU 对照表。生产板相机的空镜头不会自然产生目标，目标框、消失和返航条件使用该固定回放与明确注入的遥测完成，不把它伪称为真实飞行遥测。

最终视觉复验中，两个 OFF 指令从发布到节点写出 `Round ended: stop requested` 日志分别约 4.05 ms 和 11.82 ms；这包含 ROS 消息送达时间。节点收到 SIGINT 后正常退出耗时 0.365 秒，退出码 0。配置和 `current` 链接未变，正式服务恢复 `active`。视觉复验记录 507 条有效框与 54 条丢失框消息。

## 同一进程轮次与十分钟持续负载

`production_rounds_endurance.py` 在本次生产发布节点的同一 PID 6325 上执行 20 轮 ON/OFF，每轮均收到 75～93 条新框消息；随后第 21 轮额外连续运行 **610.105 秒**。输入、三模型、显示路径与上述 A/B 的 `cached-overlay` 组相同。该长轮收到 19,604 条框消息，其中 18,574 条标记有效；平均跟踪消费约 32.13 fps。日志有 21 条 `Round starting` 和 21 条 `Round ended: stop requested`，无 `Round failed` 或 `OSD disabled after display failure`。原始日志、逐秒资源样本及五秒 CPU 样本位于板端 `/home/hzhy/hdmi-hud-acceptance-20260924/rounds-endurance/`，本地副本为 `.docker-artifacts/production-hud-20260924/rounds-endurance/`。

| 资源与显示计数 | 观察结果 |
|---|---|
| 20 轮 OFF 后 | RSS 230,092→230,976 KiB；线程 17→17；文件描述符 51→51。 |
| 610 秒长轮 | RSS 样本区间 189,996～298,292 KiB，最后 281,792 KiB；线程固定 19，文件描述符固定 52。五秒 CPU 样本 91 个，进程均值约 275%（100% 表示占用一个核心）。 |
| HDMI 计数 | 最后汇总提交 21,160、替换 214、成功显示 20,925、提交拒绝 21。`提交－替换－显示` 为 21，后段连续日志在约 20～21 之间波动，没有随时间积压；拒绝集中于轮次切换边界。 |
| 结束与恢复 | 第 21 轮正常 OFF，节点退出码 0；正式配置与发布链接检查未变，`drone-tracker.service` 恢复 `active`。系统 RKNN 库及节点 SHA-256 与部署时相同。 |

第 5.3 项的 20 轮、十分钟、资源、退出和固定输入性能门槛均已在同一生产板完成。画面与框配对的无硬件逐像素/事件测试和实屏单目标观察一起构成证据；本次没有实际飞行或目标真值标注，不能推断跟踪精度。
