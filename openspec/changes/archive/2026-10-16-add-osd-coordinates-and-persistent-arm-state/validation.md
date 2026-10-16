# 实施与验收记录

## 范围与指令

变更 `add-osd-coordinates-and-persistent-arm-state` 在独立工作树 `osd-gps-armed-status` 实施，分支为 `codex/osd-gps-armed-status`。实施前基线为 `306e33b9e06a0cc85a8a84608b18cf6b4a85ec78`。本记录不代表归档或正式版本发布。

当前 CLI 返回的上下文要求英文工件，与用户提供的 AGENTS 工作约定要求 OpenSpec 工件使用简体中文冲突。本次遵循明确的用户约定，新验收工件使用简体中文，代码及一般文档新增内容使用英文；未重写既有规划。

用户本次明确要求完成本变更，并指定唯一可登录的 `ssh firefly-production`、允许停止服务并要求恢复。该指令授权完成必要的隔离板端验收。测试不切换正式 `current`，不改正式配置，不替换系统 RKNN Runtime，不提交或推送代码。

## 规划逐项核对

| 规划要求 | 实施及验证证据 |
|---|---|
| proposal：右侧经纬度、持续解锁、独立触发和两条路径 | `short_text.*` 生成坐标，`layout.cpp` 统一元素定义驱动两条路径；软件快照见 `evidence/README.md`。 |
| design 决策 1：现有快照、六位精度、符号、小数点、负零 | `latitude`/`longitude` 使用固定数字区域设置并防御性验证范围。`short_text_test.cpp` 覆盖正负、极值、零、舍入、缺失、非法和逗号小数点环境。 |
| design 决策 2：主字体和固定边界 | 纬度 y=314、经度 y=350，完整文字框右边界为 x=702；主字形 16×32、描边 4 像素。最长经度起点 x=458。未改缩放或目标层。 |
| design 决策 3：可信状态持续显示 | 连接中按 armed 显示绿色 `ARMED` 或白色 `DISARMED`；缺失或断连显示琥珀 `ARM --`。移除解锁三秒限制与闪烁，不改计时或返航点逻辑。 |
| design 决策 4：稳定元素与缓存清理 | 新增 `latitude`、`longitude` 和独立 `arm` 元素。长短切换、未知、恢复及稳定字段不反复重画均由布局测试覆盖。 |
| osd-ros-telemetry：坐标格式与未知规则 | 扩展遥测测试覆盖无定位、任一坐标非法、合法零值、独立过期、恢复及无 GPSRAW/返航点也能显示位置；生产接收逻辑未改。 |
| osd-ros-telemetry：持续解锁与未知规则 | 布局测试覆盖 0、3.1、75 秒、两个闪烁相位、触发、MISSION、等待与冻结；遥测测试覆盖失联、过期、恢复。 |
| rk3576-hdmi-osd：固定布局和目标装饰 | 保留原 MAH、右上任务栏、目标角框及锁定装饰。只新增坐标与持续状态，将触发移到 (22,80)。现有标签移除测试继续通过。 |
| rk3576-hdmi-osd：组合显示与路径一致 | 720×480 和 1280×720 的任务、触发、启动、冻结、告警及坐标/状态变化，均逐像素对照 legacy、增量缓存和全新缓存。 |
| 无关契约保持 | 跟踪算法、ROS 输入输出、消息、参数、模型、采集及设备生命周期实现均未改。产品改动仅限 `src/osd/short_text.*` 与 `src/osd/layout.cpp`。 |

`openspec validate add-osd-coordinates-and-persistent-arm-state --strict` 已通过。`git diff --check` 已通过。未同步或归档规格。

## 主机检查

主机为 x86_64 Ubuntu 22.04.5；不是 RK3576。原生定向检查使用 `drone-tracker-rk3576:humble-amd64-rknn2.3.2`，镜像身份为 `sha256:025d0aa88983b0646a1b7a4d25cbc13dc96a6206e0148475bbfd5fad15057421`。使用独立产物目录 `/tmp/osd-gps-armed-status-20261016/docker-native`，9/9 CTest 通过。命令及软件图像说明见 `evidence/README.md`，日志为 `evidence/native.log`。镜像及主机均未提供可用 `clang-tidy` 或 `clangd`；静态检查未运行。

完整 ARM64 集成检查从当前工作树执行：

```sh
./build.sh --test --artifacts-dir /tmp/osd-gps-armed-status-20261016/integration
```

该命令使用同一 x86_64 镜像的 ARM64 交叉编译器。ARM 测试由指定 QEMU 模拟执行；原生资源限制测试在 Linux 主机指令环境运行。产物 ELF 已确认为 ARM aarch64。此结果不能替代真实 NPU、相机、HDMI 或时间性能验收。

初次完整测试为 56/57；失败项是打包入口测试假设旧默认安装目录 `.docker-artifacts/install/drone_tracker` 存在。新工作树没有该目录。该测试使用假 Docker，不打包真实文件。首次补充的指向外部构建目录的链接在只读容器内不可解析，第二次仍失败；随后仅建立忽略目录及 `TEST_FIXTURE_ONLY` 标记。未修改测试断言或产品代码，未使用原工作树缓存，未把该目录用于真实部署。原始失败日志保留在独立日志目录。最终完整复跑为 57/57 CTest 通过（158.26 秒），另有原生 OS 资源限制测试通过。完整日志为 `evidence/integration.log`，输出架构记录为 `evidence/output-architecture.log`。

## 板端验收

生产板基线：`HZ-RK3576_WuKong Board`，主机名 `hzhy`，Linux `6.1.141-rt36`。HDMI 已连接，提供 720×480 和 1280×720。原正式发布为 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`。开始前 tracker、apm-bridge、drone-interceptor 均 active/running 且 enabled；lightdm inactive/dead。基线日志位于 `/tmp/osd-gps-armed-status-20261016/logs/board-baseline.log`。

隔离安装目录为 `/home/hzhy/osd-gps-armed-status-20261016/install`。`ldd -r` 无缺失库或未解析符号。系统 `/usr/lib/librknnrt.so` 指纹仍为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。测试使用独立 ROS_DOMAIN_ID=197 且限定本机传输；注入的是明确的合成遥测，不是飞行数据。每个脚本均在 finally 清理私有进程、验证配置及发布链接不变、恢复原 tracker 服务。其他生产服务不停止。

板端显示检查已完成；性能与持续负载验收因后述连接中断而未完成。最终服务恢复无法确认，不能将环境描述或已完成显示检查当成整个板端任务通过。


## 实际显示结果

`evidence/board-visual/` 保存 19 幅实际 DRM 平面合成图、事件、恢复和区域检查。720×480 使用缓存路径，1280×720 使用旧路径。图像检视确认最长坐标、主字体及黑描边完整，解锁 76 秒仍显示绿色 `ARMED`；OFF 提示、冻结提示、告警和解锁状态同时可读。断连与过期改为 `ARM --`，非法位置和过期显示坐标占位，恢复后重新出现实际值。该阶段退出码 0，正式配置、发布链接与系统 Runtime 指纹不变。三个生产服务均 active，lightdm 保持原先 inactive，见 `evidence/board-after-visual.log`。

初次板端启动脚本存在三个环境准备问题：sudo 丢弃已加载的 ROS 环境、覆盖 LD_LIBRARY_PATH 隐藏 RMW 插件，以及未加载隔离安装的包环境。每次失败后均恢复原服务。修正为 SSH root 下直接执行、保留 ROS 库路径、先加载隔离安装的 `local_setup.bash` 后，上述真实显示检查全部完成。没有为这些问题修改产品代码。

## 性能环境复核及未完成项

第一次性能诊断复制了旧验收脚本，业务进程继承当前 SSH 控制器的 CPU 0–3。当前正式 `/opt/drone_tracker/bin/run-camera` 使用既有 CPU 4–7 职责策略，即把控制、跟踪、YOLO、显示等线程分配给指定核心。仓库现行板端验收说明也要求设置 `DRONE_TRACKER_CPU_CONFIG=/opt/drone_tracker/config/cpu-affinity.yaml`。CPU 0–3 运行不是本次要求的生产启动配置，其结果保留为额外诊断，未称为生产验收通过。

该额外诊断的第一组稳定跟踪速率为 OSD 关闭 51.806 fps、缓存路径 48.740 fps，下降 5.918%，超过 5% 门槛；该超限结果保存在 `evidence/preliminary-cpu0-3-analysis.json`，没有删除或改写阈值。缓存完整渲染平均为 18.147 ms，P95 桶上界为 32.768 ms，实际新画面速率 47.005 fps，提交至显示平均 22.581 ms。P95 桶上界表示至少 95% 样本落在该耗时界限以内，不是精确百分位。

发现启动环境不匹配后，通过 SIGINT 中止脚本，finally 完成服务恢复。原始 CPU 分配、正式启动脚本、策略与恢复状态保存在 `evidence/preliminary-affinity-and-restore.log`。板端完整额外诊断目录为 `/home/hzhy/osd-gps-armed-status-20261016/preliminary-cpu0-3`，不计入正式配置测量次数。

正式比较从头按既有策略启动：`taskset -c 4-7`，并设置上述 CPU 策略环境变量。没有修改策略文件、系统频率策略、输入、模型或验收门槛。计划仍为三组 OSD 关闭、legacy 与 cached-overlay 交替运行，每项运行至少 75 秒，排除前 10 秒预热。第一个 OSD 关闭案例完成，稳定跟踪 58.793 fps、退出码 0，主线程实际 CPU 为 4，线程集合为 4、5、7。原始元数据已保存在主机 `/tmp/osd-gps-armed-status-20261016/formal-comparison/comparison-0-off.json`。

2026-10-16 16:22:18（Asia/Shanghai），正式第一组 legacy 案例运行期间，SSH 连接被远端关闭。随后重连先超时，再返回 `No route to host`。尚无内核或启动记录，不能判断是网络、重启、硬件或程序原因。最后一个完成案例在 16:21:21 结束，板端单调时钟为 353710.470 秒，可供恢复访问后与 uptime 比较；未取得故障前 boot ID。

**当前任务 4.4 保持未完成。** 三组正式性能比较没有完成，也未开始 20 轮及 610 秒持续负载检查。当前无法确认最终服务恢复，不能声称 tracker 或其他服务已恢复。连接中断前，正式 tracker 在测试期间停止，私有测试节点运行；apm-bridge 与 drone-interceptor 保持运行，lightdm 原先 inactive。原正式服务均 enabled，但这不能替代实际状态核验。

后续首先需要恢复唯一 SSH 目标 `firefly-production` 的访问，检查私有测试进程及脚本，正常停止它们，并恢复原 tracker 服务。核对三个生产服务 active、lightdm 保持原状态、正式发布与配置及系统 Runtime 指纹不变后，再决定是否继续未完成验收。本次未进入独立 OpenSpec verification，也未同步、归档、提交、推送或切换正式发布。

## 2026-10-16 重启后的续验与再次中断

用户重启板卡并确认可以连接后，本次首先恢复核验环境。16:37（Asia/Shanghai）核对 `drone-tracker`、`apm-bridge`、`drone-interceptor` 全部 active，`lightdm` inactive，没有上次遗留的私有测试节点。新 boot ID 为 `f1ce71c6-4f0b-4e91-8e7c-039005031d9f`，启动记录始于 16:35:29；生产发布链接、正式相机配置、CPU 策略和系统 Runtime 指纹与原基线一致。RKNPU 驱动仍为 0.9.8。恢复核验见 `evidence/resume-recovery.log`、`evidence/resume-identity.log`。

读取旧启动记录时，保留的 journal 最后时间为 14:36:49，早于上次 16:22 断连，`/sys/fs/pstore` 为空。现有证据没有覆盖故障时刻，不能判断上次断连原因，也不能因此排除内核或硬件故障。

旧的不完整正式对照在板端保留为 `interrupted-formal-comparison/`，本轮从头执行九个正式案例。继续使用 `taskset -c 4-7` 和 `DRONE_TRACKER_CPU_CONFIG=/opt/drone_tracker/config/cpu-affinity.yaml`，三个生产模型、固定回放及遥测输入不变。每个案例从首条跟踪结果开始运行至少 75 秒，统计去掉前 10 秒。下表仅列完整结束并已取回元数据的案例，不能视为九轮全部完成。

| 案例 | 稳定窗口（秒） | 跟踪 fps | 退出码 |
|---|---:|---:|---:|
| 第一组 OSD 关闭 | 65.028 | 58.544 | 0 |
| 第一组 legacy | 65.209 | 59.210 | 0 |
| 第一组 cached-overlay | 65.174 | 58.658 | 0 |
| 第二组 OSD 关闭 | 65.042 | 58.685 | 0 |
| 第二组 legacy | 65.061 | 58.822 | 0 |

第一组缓存路径的完整计时日志已取回：`render_total` 平均 15.894 ms、P95 分桶上界 32.768 ms，HDMI 新视频 58.595 fps，`submit_to_display` 平均 20.933 ms。相对第一组 OSD 关闭，吞吐提高 0.195%；该单组满足原门槛。三个已取回完整日志的案例均确认生产 CPU 策略生效，主线程 CPU 4，缓存和 legacy 的线程使用 4、5、6、7。第二组两个已结束案例的 JSON 已保存，但其完整节点日志尚未取回。`comparison-analysis.json` 的整体 `passed=false` 表示完整九案例尚缺，不能把单组通过当成总体通过。

约 16:46，在第二组 cached-overlay（PID 3183）运行期间，新的 SSH 查询和主验收 SSH 均被远端关闭，主连接退出码为 255。随后恢复连接尝试超时。该现象再次阻断任务 4.4；没有证据能确定软件、网络、供电或其他原因。没有启动 20 轮加 610 秒检查，没有进入任务 2 的独立 verification，也没有进入任务 3。

本轮原始控制台、已取回 JSON、第一组完整日志及分析保存在 `evidence/resumed-formal-comparison/` 与 `evidence/resumed-formal-comparison-console.log`。执行脚本为 `evidence/board-comparison.py`、`evidence/board-endurance.py`，分析脚本为 `evidence/analyze_comparison.py`。板端本轮目录仍为 `/home/hzhy/osd-gps-armed-status-20261016/evidence/`。任务 4.4 继续保持未完成。

四次有界 SSH 重连均以连接超时结束，最后两次输出见 `evidence/resume-reconnect-3.log` 与 `evidence/resume-reconnect-final.log`。**本轮结束时无法确认服务恢复。** 正式 tracker 在第二组缓存案例测试期间停止，私有验收脚本是否继续运行或执行 finally 不能确认；另两个服务最后观测为 active，桌面最后观测为 inactive，均不代表当前状态。再次恢复访问后必须先停止私有验收脚本及节点，恢复 tracker，并核对所有服务与原基线一致。不能直接重跑验收或开启后续任务。本轮结束前本地 `git diff --check` 和严格 OpenSpec 验证通过；CLI 仍报告 14/15 完成。

## 2026-10-16 第二次重启后的独立故障调查

16:55–16:58（Asia/Shanghai）独立代理完成只读调查。持久化 `/var/log/kern.log` 与 `/var/log/syslog` 确认两次断连均为内核过温保护关机，时间为 16:22:18 和 16:46:14。上轮五个完整案例显示 little-core 温度从 67.461°C 累积至 111.700°C，当前关键关机点为 115°C。故障时间的大整数报警参数不能当作真实温度读数，也不能据此否定多传感器持续升温的证据。详细结论、原始日志、脚本恢复缺口和后续散热条件见 [outage-investigation.md](outage-investigation.md)。

16:57:36 已确认三个正式服务全部 active/running、lightdm 保持 inactive/dead，且没有残留私有测试进程。正式发布、相机配置、CPU 策略和系统 RKNN Runtime 指纹与基线一致，见 `evidence/outage-investigation/recovery-final.txt`。这项新证据解决了之前“无法确认服务恢复”的状态，但不补足任务 4.4。调查未施加新负载，未修改频率、温控或正式部署。应先确认有效物理散热，再带温度提前终止与无条件服务恢复保护继续原时长验收。

## 2026-10-16 后续验收脚本保护与本地验证

独立实施代理按 `openspec-apply-change` 继续检查本 change；CLI 仍报告 `spec-driven`、14/15，仅任务 4.4 未完成。确认过温原因后没有重新执行板端负载。以下改动仅用于未来的私有验收脚本，不是产品行为变更，也不是新的实机通过证据。

已执行过的旧 `board-comparison.py` 和 `board-endurance.py` 原样保存在 `evidence/executed-before-thermal-guard/`，并附 SHA-256。`evidence/` 顶层同名脚本是后续执行版本，通过同目录 `acceptance_safety.py` 提供以下保护：

- 在停止服务前读取温度；运行时约每秒把全部温区与进程资源写入 JSONL，即每行独立 JSON 的日志。每条记录执行 flush 和 fsync，将进程缓冲内容提交给文件系统，减少中断时丢失的证据。
- 任一温区达到 95°C，或没有可读温度传感器，即提前终止并标记验收未完成。95°C 只是私有测试的提前终止线，明显低于既有 115°C 保护关机点；不修改板端温控、频率或验收性能门槛。
- 记录私有节点 PID，正常发送 SIGINT 停止，超过原有 30 秒等待则只强制结束本脚本创建的节点。SIGINT、SIGTERM 和 SSH 会话中断可能带来的 SIGHUP 都进入相同清理路径。不扫描或终止无关进程。
- 节点停止异常后仍尝试恢复 tracker。正式配置与发布链接断言移到恢复之后，避免断言跳过服务恢复。恢复记录核对 tracker、apm-bridge、drone-interceptor、lightdm 的原始状态。成功完成事件只能在整个清理与恢复成功后写出。
- 比较分析器读取新增日志；已提前终止的执行不能因目录残留九个旧案例 JSON 而被判通过。中断记录保留，不覆盖历史取证目录。

本地主机 `/home/mzh/miniconda3/bin/python -m unittest -v test_acceptance_safety` 通过 8/8，原始输出见 `evidence/safety-tests.log`。测试覆盖实际子进程忽略 SIGINT 后的超时及强制结束、无关进程保留、95°C 提前终止及落盘、传感器缺失、信号处理、清理失败仍恢复、恢复失败仍检查，以及配置断言不跳过恢复。五个相关 Python 文件通过语法解析。这些检查没有导入板端 ROS、没有访问板端相机或 NPU，也不能替代正式验收。

17:02:24 再次通过 `ssh firefly-production` 只读核对：三个正式服务 active/running，lightdm inactive/dead；没有私有验收进程，正式发布、相机配置、CPU 策略和系统 Runtime 指纹与基线一致。温区为 68.384–72.076°C。记录见 `evidence/outage-investigation/post-harness-readonly-check.txt`。

任务 4.4 保持未勾选。恢复负载前仍需要用户确认已解决散热器安装/接触、风扇或风道问题。确认后须按原模型、输入、CPU 策略完成全部九个 75 秒比较案例，以及同一进程 20 轮 ON/OFF 和连续 610 秒负载；所有原性能门槛不变。触发 95°C 保护、关机或中断均不能算通过。后续独立 verification 和修复任务尚未开始。

## 2026-10-16 增加风扇后的受保护续验

用户确认增加风扇并改善散热后，独立实施代理继续任务 4.4。17:11–17:12（Asia/Shanghai）先只读核对生产板：三个正式服务 active/running，lightdm inactive/dead，无私有验收残留。发布链接、相机配置、CPU 职责策略、全部模型和系统 Runtime 指纹与原基线一致。各温区约 35–40°C，显著低于此前未加风扇时的基线。私有节点 SHA-256 为 `71be771a0914f3939394dffad120337896ec08c25f27cbbd1ae2bf7aca757ca9`，与当前工作树 ARM64 构建产物一致；固定录像指纹仍为 `dae6fad1f234a94fea2280a48c590ec17c657cb7901b984d0fcb5edb52ed01e5`。

将受保护版本 `board-comparison.py`、`board-endurance.py` 及共享的 `acceptance_safety.py` 一同传到板端，逐项核对 SHA-256。旧执行脚本和旧中断目录均保留。15 秒缓存路径试运行完成，收到 900 条跟踪结果，其中 896 条有效，节点正常退出；最高温度 50.846°C。脚本确认三个服务恢复、lightdm 保持 inactive，正式配置和发布链接不变。这个短试运行仅用于确认保护和散热，不能替代正式比较或持续负载。

随后按原计划执行九个 75 秒比较案例，保留 `taskset -c 4-7` 和原 CPU 职责策略，未修改频率、温控、门槛、模型、录像或持续时间。前两个案例完整完成：

| 案例 | 去掉前 10 秒后的窗口（秒） | 跟踪 fps | 退出码 |
|---|---:|---:|---:|
| 第一组 OSD 关闭 | 65.175 | 58.627 | 0 |
| 第一组 legacy | 65.160 | 58.901 | 0 |

第三个 cached-overlay 案例未完成。200 条逐秒落盘温度记录覆盖 17:13:12.591–17:16:33.432，全部温区最高值为 59.153°C，最后一条最高为 56.384°C。没有触发 95°C 提前终止线。17:17:02 重新连上板卡时，启动标识已从 `62b4c6ba-0fa7-4996-8aca-f8c522b548f4` 变为 `9e39c5c3-bb12-48da-ae42-7dfcbb2be2ee`，确认中途发生整板重启。保护日志没有 completed 或 restore 事件；这不是脚本正常收尾，不能标记验收通过。分析器返回 `passed=false`、`diagnostics_completed=false`。

停止施加私有负载后进行只读调查。17:19:35 最终服务核对又发现启动标识变为 `fbbe7a33-de93-4343-961f-1d4804c8e51c`，说明没有私有验收节点运行时也发生了另一次重启。目前不能判断是否有人手动重启，或存在供电、硬件复位、看门狗等原因；这些都不是已证实结论。与此前 16:22 和 16:46 的明确过温关机不同，本轮没有对应的新过温日志，温度记录也没有支持同样的热积累趋势。

当前可用证据不能确定这两次新重启的根因：上一轮启动的 journal 没有保存，pstore（跨重启崩溃记录区）为空，未找到可用的复位原因或供电电压记录。systemd 的运行看门狗设置为 0，三个业务服务也未配置看门狗或失败后整板重启动作；这些只是排查结果，不能排除其他硬件复位来源。内核启动日志有文件系统恢复及 RK806 原始状态，但未把未经确认的寄存器数值解释为故障原因。验收脚本没有系统 shutdown/reboot 调用，且本轮开始前没有旧私有脚本残留。

17:19:35 最终确认 `drone-tracker`、`apm-bridge`、`drone-interceptor` 全部 active/running 且 enabled，lightdm 保持原先 inactive/dead；无私有验收残留。发布链接、相机配置、CPU 职责策略、全部模型和 Runtime 指纹仍与基线一致，频率策略未变，各温区 37.923–40.692°C。此时服务恢复已确认，但不保证尚未查明的重启原因已消除。

原始证据保存在 `evidence/guarded-fan-20261016/`，包括基线、完整板端证据压缩包、200 条保护记录、前两个完整案例、分析结果、重启调查和最终恢复记录。板端原始目录未删除。本次未重跑以覆盖中断记录，未启动同一进程 20 轮和连续 610 秒测试。任务 4.4 保持未勾选，整体仍为 14/15；后续独立 verification 和修复任务继续被阻塞。需要先确认这次新的外部重启或硬件复位原因，再继续完整验收。

## 用户确认误拔电源后的重新验收

上述调查后，用户明确说明自己误拔了电源线，并确认已恢复供电。这是用户提供的物理操作证据，能够解释本轮外部断电干扰；没有据此宣称逐次重启都已通过硬件复位寄存器归因。已保留全部中断记录，未把它们改成通过。

17:20:34 再次核对三个生产服务、lightdm、正式配置、发布链接、CPU 策略、Runtime、脚本指纹及私有进程，结果与基线一致；六个温区为 37.923–39.769°C。随后在新的空证据目录从第一个案例重新运行完整九项比较。该次证据在 `evidence/guarded-fan-resume-20261016/`；板端之前的中断目录保留为 `interrupted-guarded-power-20261016T1716/`。执行脚本、95°C 保护、75 秒时长及既有性能门槛均未修改。

### 完整九案例比较

所有九个案例完整运行并正常退出，生产 CPU 职责策略生效。三个关闭 OSD 的稳定跟踪速率为 58.901、58.458、58.978 fps，中位数 58.901 fps。三个 legacy 案例为 58.729、58.792、58.749 fps。每个案例从首条跟踪结果开始运行至少 75 秒，统计排除前 10 秒。HDMI 实际为 720×480、60 Hz；刷新频率与下表实际新视频速率分别记录。

| 缓存路径组 | 跟踪 fps | 比关闭 OSD 中位数下降 | 完整渲染均值 ms | P95 分桶上界 ms | HDMI 新视频 fps | 提交至显示均值 ms |
|---|---:|---:|---:|---:|---:|---:|
| 第一组 | 58.561 | 0.576% | 15.797 | 32.768 | 58.469 | 20.645 |
| 第二组 | 58.607 | 0.498% | 15.259 | 32.768 | 58.444 | 19.286 |
| 第三组 | 59.131 | -0.390% | 15.605 | 32.768 | 59.055 | 19.945 |

三组均满足既有门槛：跟踪下降不超过 5%，渲染均值不超过 40 ms，P95 不超过 65.536 ms，新视频至少 25 fps，提交至显示均值不超过 60 ms。比较期间最高温度 66.538°C，没有触发保护或重启。分析器确认 `passed=true` 与 `diagnostics_completed=true`。脚本收尾确认服务恢复、正式配置及链接不变。

保留了两个退出期计时失败样本，不能把整个原始日志称为“零失败”：第二、第三组缓存路径各有一次 `repeat result=failure count=1`，均不包含新视频。前者满载窗口结束于 Unix 时间 `1792142916.005932`，发送 SIGINT 为 `1792142916.620853`，失败窗口写于 `1792142916.770863`；后者分别为 `1792143147.403577`、`1792143148.018759`、`1792143148.169932`。两者均在 OFF 和正常退出信号之后，不在稳定性能窗口内。

这一时序与现有退出实现一致：`OsdPresenter::shutdown()` 调用后端 `interrupt()` 唤醒等待，`HdmiBackend` 的刷新等待会抛出 `OSD display interrupted`，显示线程将异常计入失败样本再释放资源。主规格“运行显示故障隔离与安全退出”的“等待刷新时退出”场景要求等待可被结束并安全回收资源；没有规定退出时此类取消计时必须为零。此处是代码与时间记录支持的解释，不宣称原始日志记录了更具体的异常文本。节点退出均为 0，没有运行期 `OSD disabled after display failure`，下一案例及正式服务可重新取得显示设备。后续独立核验仍可检查这两条原始样本。

### 同一进程 20 轮与 610 秒持续负载

受保护 `board-endurance.py` 在同一节点 PID 4566 内完成 20 轮 ON/OFF，每轮收到 146–177 条跟踪结果；随后同一进程完成 610.130 秒连续完整跟踪负载。连续阶段收到 35,829 条新跟踪结果，其中 35,613 条目标有效；没有核对目标真值，因此不代表跟踪准确率或实飞验收。日志与保护事件确认只启动一个私有节点。退出码 0，保护日志以 completed 结束，没有失败、超温或信号中断事件。

去掉预热后的 598.913 秒完整计时窗口中，跟踪约 58.723 fps，HDMI 新视频 58.598 fps；渲染均值 15.575 ms、最大 22.954 ms、P95 分桶上界 32.768 ms，提交至显示均值 20.013 ms、最大 33.202 ms。待显示均值 4.437 ms、最大 16.669 ms，新视频间隔最大 182.974 ms，没有持续积压或长时间停止出图的诊断证据。窗口合计提交 35,169、替换 75、显示 35,095、拒绝 0、提交显示层 35,101；窗口边界可能包含上一窗口进入的待显示项，因此这些计数不要求逐项算术相等。

整个持续脚本没有计时失败样本或运行期显示关闭。稳定区间 CPU 约为单核的 186.130%，线程始终 16，文件描述符始终 52，RSS（进程常驻内存）为 200,504–228,216 KiB；十个完整分钟的 RSS 中位数约为 203.8–205.5 MiB，没有持续上升趋势。最高温度 63.769°C。私有节点退出加正式服务恢复共 0.503 秒；这个数值包含两项操作，不当作单独的节点停止耗时。

原始比较与持续证据分别保存在 `comparison-board-evidence.tar.gz` 和 `endurance-board-evidence.tar.gz`；解压后为 `evidence/` 与 `rounds-endurance/`。可复现分析命令如下，第二个脚本只读取证据，不连接设备或施加负载：

```sh
python evidence/analyze_comparison.py <解压目录>/evidence
python evidence/analyze_endurance.py <解压目录>/rounds-endurance
```

### 最终服务恢复与任务状态

17:44:38 最终独立核对，启动标识仍为 `fbbe7a33-de93-4343-961f-1d4804c8e51c`，说明完整重新验收期间没有重启。三个正式服务 active/running 且 enabled，lightdm 保持原 inactive/dead，无私有验收脚本或节点残留。正式发布仍为 `bundle-20260927T070059Z-2281`；相机配置、CPU 策略、全部模型和系统 Runtime 指纹与基线一致，CPU 频率策略未改，各温区 44.384–47.153°C。最终记录见 `final-restoration.txt`。

结合先前已完成的两种分辨率实际显示检查，本次补足任务 4.4 的全部适用性能与持续负载证据。任务 4.4 可以完成。此结论不覆盖 Jetson、真实相机新长测、定位精度或飞行准确率；此次新增代码只调整 OSD 文字与布局，没有改动这些契约。未发布私有验收安装副本，也未提交、推送、同步或归档。后续按用户要求进入独立 verification。
