# 持续 HDMI 相机预览验证记录

## 目的和证据边界

本记录用于验证 RK3576 正式 ROS 相机入口在 `osd.enabled=true` 时，视频随服务运行，ON/OFF 仅控制跟踪与记录。视频流身份表示一次实际开流；跟踪轮次与录像会话分别表示一次业务请求的独立生命周期。预览没有算法结果。Running 阶段只显示实际消费图像及其同帧框。

本次离线实施位于 Git worktree `4a44`，源码基线为 `fde4f35`，实施修改未提交。离线阶段不连接任何开发板。容器与模拟通过只证明对应自动检查，不证明真实 NPU、相机、HDMI、画质或生产负载通过。生产任务 8.2–8.6 在取得其实际证据前保持未完成。

## 离线环境与复现

- ARM64 镜像：`drone-tracker-rk3576:humble-arm64-rknn2.3.2`，不重建镜像，不下载或安装依赖。
- 构建入口：`./build.sh --test`，显式开启 RK3576、OSD、录像和测试。
- worktree 的 `.git` 引用主仓库的绝对路径。直接运行时，既有基线工具的源码身份收集器无法在容器中读取该路径，首次构建因此失败。
- 复现时为 `docker run` 追加主仓库 `.git` 的同路径只读挂载。临时包装器位于 `.docker-artifacts/log/build-worktree-docker-wrapper.sh`。不修改共享 Git 元数据，也不修改构建脚本。
- 最终复现命令：把该包装器复制为临时目录中的 `docker`，给予执行权限，然后执行 `PATH=<临时目录>:$PATH ./build.sh --test`。包装器内真实 Docker 的路径仅适用于本次开发机，应在其他机器上核对。
- 构建和 CTest 原始日志：`.docker-artifacts/log/build-console.log`、`test-console.log`。额外录像与部署检查日志：`.docker-artifacts/log/offline-recording-deployment.log`。
- 容器没有 `clang-tidy` 或 `clangd`，仓库没有对应检查命令；这两项静态检查未执行。
- 当前开发机没有 Jetson SDK 或硬件；只执行公共 Jetson 配置检查，不声称 Jetson 构建或实机通过。

## 离线检查内容

1. 显示模拟检查保留旧视频 OFF/EOF 冻结行为。新增持续预览检查覆盖流/轮次/控制版本隔离、OFF 后递增图像、预览到配对边界、旧结果拒绝、替换和在途显示所有权。两种显示路径均覆盖无结果提示、MISSION、告警和目标清除。
2. ROS 相机独立测试分组 `ros_continuous_camera_tests` 覆盖未 ON 自动预览、无业务输出及无录像请求；覆盖两种显示路径与图像开关组合。受控阻塞 YOLO 和 NanoTrack 时，OFF 返回且预览继续；快速 ON/OFF/ON 只启动一个后续轮。覆盖遥测更新、晚到结果拒绝、跟踪处理故障、相机故障通知与重复 shutdown。
3. 录像检查使用模拟相机和测试工作进程。真实相机设备未被打开。检查准备中取消、暂存后 OFF、逐帧引用存活、同流不同会话、OFF 后无新接受、旧进程未回收时拒绝新增资源，以及原有有界等待和存储故障。
4. 额外录像检查使用临时 Docker ext4 卷 `drone-tracker-4a44-recording-tests`。ext4 是本项目录像所要求的本地文件系统；此卷用于模拟存储，不是生产介质。运行 `rk3576_recording_session_tests` 和 `rk3576_recording_storage_tests`，二者位于构建目录的 `src/platform/production/recording/`。
5. 部署检查执行 `deployment/tests/test_package.py`、`test_prepare_display.py` 和 `test_cpu_policy.py`。生产模板与基线解析后的 YAML 数据相等，修改只增加行为说明注释。

首次完整自动回归为 43/45。两个失败分别为旧断言将已暂存的 OFF 撤销计作掉帧，以及在算法内部会被按既有规则处理的 NanoTrack 后端错误不能触发节点异常路径。修正后保留禁止接受旧候选的断言，并将其计数改为撤销；节点异常用帧兼容检查边界注入，不改变算法。最终运行结果以原始日志和下方最终结果段为准。

## 生产阶段执行约束

只能通过 `ssh firefly-production` 访问指定生产板。每个非交互 SSH 命令先加载 `/opt/ros/humble/setup.bash`。正式服务可暂停，但全部测试结束或失败退出后必须恢复原正式服务。使用隔离测试安装目录；不把测试产物覆盖为正式 current release。

实测前记录：板卡和主机身份、Ubuntu 与内核、V4L2/媒体拓扑及稳定相机节点、显示设备和实际模式、桌面服务与 DRM 占用、程序/配置/三个模型的实际文件指纹、系统 Runtime 2.3.2 文件指纹和驱动 0.9.8。三个模型指 YOLO、NanoTrackV3 Backbone 和 Head；本配置的 Backbone 分为模板和搜索两个文件，因此共核对四个 RKNN 文件。采集前执行 `v4l2-ctl --list-devices` 与 `media-ctl -p`。不运行前台常驻 `rknn_server` 查询版本，不覆盖系统 `/usr/lib/librknnrt.so`。

## 三个定向窗口（待实测）

| 窗口 | 输入和时长边界 | 完成条件 | 当前证据 |
|---|---|---|---|
| cached-overlay 联合窗口 | 同一生产节点；至少十秒未 ON 预览、20 轮 ON/OFF、至少六十秒连续跟踪/录像/HDMI 有效负载、最终 OFF 至少十秒；单次不超过 180 秒 | 相机仅开流一次；OFF 清框不超过 500 ms；无资源持续累积；录像正确结束；最终设备释放 | 未执行 |
| legacy 行为窗口 | 同板同相机同模型；单次不超过 180 秒 | 与 cached-overlay 的预览、OFF 和同帧配对语义一致 | 未执行 |
| OSD 关闭基线 | 同板同相机同模型；ON 有效窗口；单次不超过 180 秒 | 取得与 cached-overlay 联合有效窗口可比的跟踪吞吐基线 | 未执行 |

cached-overlay 的有效 ON 窗口仍需核对：`render_total` 均值 ≤40 ms、P95 ≤65.536 ms、新视频 ≥25 fps、`submit_to_display` 均值 ≤60 ms、相对 OSD 关闭基线的跟踪吞吐下降 ≤5%。P95 是 95% 样本不超过的值；直方图日志给出近似桶上界，合并桶后计算，不能平均每秒 P95。没有可比有效负载时保留未完成。

## 生产记录需要补充的字段

- 首个合格帧时间与流预热证据；没有首帧时写未知，不用零冒充延迟。
- OFF 接受时间、`OSD display` 的 `control_to_display_us`、模型/bag 清理时间和录像准备/收尾时间。显示完成不是屏幕实际发光时间。
- `Stream counters` 的累计采集/分发、跟踪替换、预览/配对包数量及轮次窗口差值；`OSD timing` 的流身份、轮次、窗口时长及真实新视频数量。
- 进程内存、线程、文件描述符和受管线程/进程的实际 CPU 掩码；未测 NPU 设备内存或功耗须明确标注。
- 录像段通用解码结果、独立会话/共同流身份、首帧 PTS=0、起止计数与 OFF 后无新接受。NV12 原画面录像不得包含 OSD。bag 必须可回读，关闭图像开关时没有图像话题数据。
- 退出后相机/HDMI 占用和正式服务恢复结果。

## 结论限制

短测不等于历史十分钟、三十分钟或长期稳定性验收。可解码不等于目标准确率或未测场景画质合格。模拟姿态或未标定内参不等于旋转补偿精度通过。时间字段不是已经证明的曝光时间。不执行断电、拔电或损坏性试验，也不声称掉电缓存损失已经验证。

## 离线最终结果

2026-10-13 的最终源码构建成功，45/45 项 CTest 通过，总运行时间 24.63 秒。新增二十轮快速显示控制检查确认计时窗口保持有界；切换跟踪轮次时冲刷分组，不重置健康视频的间隔链。额外录像会话/存储检查和部署检查均通过。临时 ext4 测试卷已删除。

最终原始证据已另存于 `.docker-artifacts/log/continuous-preview-offline-final/`，包括构建、CTest、额外录像/部署日志及安装程序 SHA-256。安装程序为 ARM aarch64 ELF，OSD 与录像工作程序/检查器均已安装。节点指纹为 `c493b99d3c08d8b6fc59bc3ded0f1767b8d3ca63cb3b54e5056c3783f448f9a3`。

`openspec validate decouple-rk3576-hdmi-stream-from-tracker-trigger --strict` 与 `git diff --check` 通过。任务进度为 34/39；剩余 8.2–8.6 必须由指定生产板的新证据完成。尚未连接生产板，正式服务状态未被本离线阶段更改。

## 2026-10-13 生产板实测身份与复现

以下结果来自本次新实现，前文“待实测”和 34/39 描述离线阶段结束时的状态。原始证据根目录为 worktree 的 `.docker-artifacts/log/continuous-preview-production/`。该目录不提交 Git。板端证据目录为 `/home/hzhy/continuous-preview-4a44-20261013/`。`board/<窗口>/` 保存取回的日志、配置、每秒资源快照、事件、录像和 bag；`analysis.json` 保存逐会话核对、逐段解码及合并计时数据。

仅使用 `ssh firefly-production` 连接。板卡为 `HZ-RK3576_WuKong Board`，主机名为 `hzhy`，Ubuntu 22.04.5 ARM64，内核为 `6.1.141-rt36`。系统 RKNN Runtime 为 2.3.2，RKNPU 驱动为 0.9.8。动态链接检查确认节点使用系统 `/lib/librknnrt.so`，没有覆盖系统 Runtime。系统库指纹为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。MPP 库指纹为 `f03882324fddb343ff714e77b1fe37587ab70a42d3e40057327e5fa9afa46261`，RGA 库指纹为 `56b2d92aae277095064546b3b0d63a96c3b7d7b4a82c602377747a5192fa1d97`。

测试节点指纹与离线安装一致，为 `c493b99d3c08d8b6fc59bc3ded0f1767b8d3ca63cb3b54e5056c3783f448f9a3`。隔离安装在板端 `install/` 中。仅从原正式 release 读取已有的 `bundled-ros` 私有 ROS 库。程序、模型和算法没有在实测期间修改。

| 模型角色及文件 | SHA-256 |
|---|---|
| YOLO：`balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| Backbone 模板：`fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| Backbone 搜索：`fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| Head：`fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

实测前重新执行 V4L2 设备枚举及媒体拓扑查询。SC233HGS 传感器实体为 `m00_b_sc233hgs 0-0030`，经 CSI 链路进入 `rkisp-vir0`，媒体设备为 `/dev/media2`，采集节点为 `/dev/video22`。格式为 1920×1200 NV12，录像保留此原尺寸。跟踪使用灰度分流。连接的 HDMI-A-1 支持并实际选择 720×480、60 Hz；目标 DRM 设备为 `/dev/dri/card0`。初始化日志确认一个 DRM 客户端和独占控制权，不只依赖名义刷新率。

正式配置与 CPU 策略分别为 `/opt/drone_tracker/config/camera.yaml` 和 `cpu-affinity.yaml`。原指纹分别为 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c` 和 `4f46293201355131c1e72c8a72cdfaafa2c3121eaa48fe0b5ad69ac442e4f01b`。三个测试配置只替换隔离的 bag/录像路径及对应显示选择。保留类别 0、置信度阈值 0.5、全部跟踪配置、`visualized_result=false`、`need_save=true` 和录像 12 Mbps/60 秒分段/1000 ms 同步。测试目录位于本地 ext4 文件系统，保留原存储保护阈值。

| 窗口 | 配置 SHA-256 | 实际节点总时长 | 业务窗口 |
|---|---|---:|---|
| cached-overlay | `7a928739c42425301a0b7f02053a47c24ff5a251c38355abef21ee190eda6156` | 150.120 秒 | 启动后等待 15 秒；19 轮各 2 秒 ON、1 秒 OFF；第 20 轮 ON 65 秒；最后 OFF 12 秒 |
| legacy | `cc3704d44dcc27d48e3ae4eaca5eed61cb01d24ea62f8953a8f93d98310240d9` | 39.781 秒 | 初始 15 秒、ON 12 秒、最后 OFF 12 秒 |
| OSD 关闭 | `6c5692e7a14e781ad85737dfb9dffcd4c13041ce89eca2be4dfc3438190cd65f` | 92.726 秒 | 初始 15 秒不采集；ON 65 秒；最后 OFF 12 秒不采集 |

三个节点均在 180 秒以内退出，退出码为 0。复现控制器为证据目录中的 `harness.py`。每个远程入口先加载 ROS；加载隔离安装环境后启动节点。使用隔离 ROS domain 81 和本机通信，模拟单位姿态的 gimbal/odom，不改变模型输入或人工指定首框。单调时钟记录各阶段，175 秒内部限时和外层 `timeout` 共同限制运行。外层退出 trap 与延时 systemd 恢复定时器保护正式服务恢复。

## 输入可比性、负载与性能结果

三个窗口均使用同一相机、系统、CPU 策略、三个模型和静止的室内天花板场景。没有调整阈值、类别、算法或物理相机输入。模型在三个窗口均进入 `Detecting → TargetFound → Tracking`。cached-overlay 的 20 轮均各发布这三个状态；联合窗口持续处于 Tracking 64.850 秒，没有 TargetLost。OSD 关闭基线持续处于 Tracking 64.233 秒。Head 与搜索 Backbone 因此具有真实的持续跟踪工作量，不将纯 Detecting 窗口当作完整负载。

抽取 cached-overlay 联合录像第 30 秒、legacy 第 5 秒及 OSD 关闭第 30 秒原画面核对场景。以 64×40 灰度缩略图比较，相对 cached-overlay 的平均绝对像素差分别为 0.073 和 0.000（8 位范围 0–255）。这只佐证本次静止场景可比，不证明每帧像素完全相同。原尺寸抽帧可见室内天花板、照明及固定物体，没有业务叠加。

真实模型给出有效框和 Tracking 状态不等于识别到了有真值的气球。没有人工核对目标身份或框准确率。本报告仅以真实模型执行、状态持续时间及消费计数认定推理负载，不宣称目标质量、识别正确性或实飞通过。

| 指标 | cached-overlay 实测 | 门槛和结论 |
|---|---:|---|
| 配对新视频速率 | 59.851 fps | ≥25 fps，通过 |
| 实际跟踪消费速率 | 59.851 fps | 对照基线 60.021 fps；下降 0.283%，≤5%，通过 |
| `render_total` 均值 | 15.301 ms | ≤40 ms，通过 |
| `render_total` P95 桶上界 | 32.768 ms | ≤65.536 ms，通过 |
| `submit_to_display` 均值 | 19.055 ms | ≤60 ms，通过 |

吞吐使用节点累计消费/显示计数的差值除以单调时钟窗口差值。cached-overlay 稳态窗口为 59.431 秒、3557 次消费和新显示；OSD 关闭为 58.613 秒、3518 次消费。两窗口都排除开始约三秒和结束前至少一秒，完整连续有效负载则超过 64 秒。业务框接收端的采样交叉核对分别为 59.874 和 60.012 fps，不用中间件计数替代节点计数。

计时只合并第 20 轮的成功配对新视频组；排除预览、重复底图、失败和混合切换窗口。64.032 秒的计时窗口内有 3828 个成功新视频样本。均值按样本数加权，P95 从合并后的二次幂桶重新计算，没有平均每秒百分位。日志均值的有限小数精度使加权均值为近似值。legacy 行为窗口的配对显示速率约 29.982 fps，渲染均值 33.166 ms；它用于路径行为回归，不混入 cached-overlay 门槛或基线吞吐。

## 新生命周期、录像与 ROS 边界

cached-overlay 仅创建一条视频流 `stream=1`。Camera service started、开流预热和两个 Capture 职责入口各出现一次；ON/OFF 没有重新开流或预热。流序号最终达到 8898，分发累计 8898，预览提交 2719，配对提交 6084。最终相机合格帧计数为 8901、预热出队总计 8911。分发采用取最新策略，且停止时相机和分发的最终快照有各自边界；相机合格帧与分发数相差三帧，不把这个差值归为驱动缺帧或算法消费。队列满丢弃、驱动序号缺口均为 0；跟踪单槽替换累计 81，第 20 轮替换 12。替换数量不等于缓存长度，实际缓存仍只有一份待处理任务。

cached-overlay 流预热耗时 448.297 ms，检查 11 帧、丢弃 10 帧、重复时间 2 次、最终连续计数 8，首个合格驱动序号为 10。首个实际预览成功提交距节点启动约 1.031 秒；初始无框预览因此超过 14 秒。首帧时间保留驱动映射值 `1791878707654806` 微秒，不称为曝光时间。legacy 预热为 337.339 ms。启动阶段的 `control_to_display_us` 反映等待首张相机帧，不能误计为收到 OFF 的清框延迟。

20 轮真正 OFF 之后的 TRK OFF 日志均为 Live、`boxes=0`，保留视频；控制交接至成功显示最大 29.748 ms。legacy 对应最大值为 41.908 ms，均满足 500 ms。最后 OFF 之后的流计数继续递增，跟踪消费计数保持不变。cached-overlay 收到 6084 条框消息，其中 6010 条标记有效，调试图像为 0。OFF 发送后接收端最多多收到一条已进入中间件或尚在接受边界前的消息；之后静默，没有新增有效轮次或状态。

三个必需窗口产生 22 个独立视频会话和 22 个 bag。cached-overlay 的 20 会话都关联同一流身份 1，tracking round 为 1–20，各自 PTS=0。共接受和保存 6019 视频帧，第 20 轮为 3894。每会话 `attempted=accepted=frames`，主动 dropped=0，首帧身份与视频时间起点一致，帧身份/时间严格递增且不串轮。`camera.json` 的接收窗口起止快照分别保存，本轮量由差值得到；结束原因为 `user_off`，`camera_running_at_end=true`，预热证据标记 `warmup_from_stream=true`。第 20 轮最后接受数 3894 在最后 12 秒预览期间保持不变。初始预览的事件和资源快照均确认无任何会话目录、录像工作进程请求或业务消息。

录像准备最大 189.434 ms、父进程正常收尾最大 31.282 ms；legacy 分别为 151.786 和 33.237 ms，基线为 97.378 和 28.931 ms。均低于各三秒预算。三个必需窗口共 24 个 TS 段在本机通过 `ffmpeg -v error -xerror -i <段> -f null -` 全段独立解码。`ffprobe` 确认 H.264、1920×1200。录像检查器另核对 JSON/TS 结构，但没有把其 `decoded=false` 当作解码证明。原 NV12 录像没有框、文字、准星或 OSD；抽帧与分流代码共同核对，不能据此宣称画质场景通过。

每秒诊断中已知同步位置的最大 lag 分别为 949.693、733.097、649.785 ms；尚无同步位置时保留 unknown，不计为零。此采样只描述已观测的同步落后，不推断未采样瞬间的最大值或重新宣称历史同步 P99 验收。

生产板使用 `rosbag2_py.SequentialReader` 回读 22 个 bag，并按实际消息类型反序列化全部 17227 条消息。SQLite integrity_check 全部为 ok。`board/bag-readback.json` 确认 bag 话题为 `/fpv/gimbal`、`/fpv/odom` 和 `/fpv/target_bbox`，均没有 `/fpv/tracker_image` 或 `/fpv/tracker_state`；各 bag 的 `states` 均为 `{}`，不能用 bag 回读证明算法状态。状态值 0、1、2 来自各窗口 `events.jsonl` 对 `/fpv/tracker_state` 的实时订阅记录，并由 `node.log` 的状态发布日志交叉核对。生产的关闭图像配置没有调试图像消息；开启图像、预览不发布和迟到结果拒绝由离线相机模拟组合覆盖，没有把它们写成开启图像的生产实测。

## 资源、亲和性及恢复

原 CPU 策略保持 role-pinned：Control/Tracking=4、YOLO=5、Display=6、Capture/Recording=7，总允许范围 4–7。启动日志保存每个受管入口的 requested/actual 掩码确认；常驻采集和分发没有在 ON/OFF 时重建，录像工作进程逐轮确认 CPU 7。节点每秒 `/proc` 快照保存线程实际掩码，并与日志中的线程号关联。共享 CPU 7 的实际系统忙占比在联合有效窗口约为 27.2%，legacy 为 28.4%，基线为 25.3%；这些是该核心的整体负载，不单独归因给采集或编码器。

cached-overlay 各 OFF 快照始终为 19 个主进程线程、54 个文件描述符。19 个短轮的主进程 RSS 在 245028–249228 KiB 间波动；最后联合负载结束后为 251052 KiB，12 秒保持不变。RSS 是主进程实际驻留内存。联合负载中主进程为 20 个线程、56 个文件描述符，内存分配变化后保持有界。没有逐轮增长的线程或文件描述符，没有录像资源仍占用时追加会话。短测中的有界缓存/分配变化不作为长期内存稳定性证明。

前三个窗口的 `/proc` 采样器没有取得从非主线程创建的录像子进程资源，不能把其中 `processes` 数组长度当作没有子进程。第一次 39.779 秒资源补测仍未取得子进程。随后采用按隔离 worker 完整命令路径查询的采样器，18.716 秒定向补测取得实际 worker：始终至多一个进程，RSS 为 23696–29728 KiB，三个线程实际 CPU 掩码均为 7，文件描述符为 40；最后 OFF 的三个每秒样本均没有 worker。两次补测都使用同一安装、配置和相机，均退出码 0，退出后设备无占用并恢复正式服务。它们仅补充资源证据，不拼接到主联合窗口或性能结果。两个补测录像段亦独立解码通过。

三个必需节点均安全退出；cached-overlay、legacy 的真实相机停止分别为 7.317、7.976 ms，低于一秒。SIGINT 到整个进程退出分别为约 367.8、365.6、315.0 ms。退出后、恢复正式服务之前的 `released.txt` 均为空，表示 fuser 未发现相机或两个 DRM card 的占用。cached-overlay 收尾计时包含一个被 SIGINT 中断的 repeat 显示操作失败样本；稳定联合窗口没有显示失败，不把该收尾样本计入成功渲染门槛。

原正式服务为 `drone-tracker.service`，enabled 且 active/running；current 为 `/opt/drone_tracker/releases/bundle-20260927T070059Z-2281`，桌面 `lightdm` 原本 inactive/dead。每个测试窗口后恢复正式服务，再进行下一个窗口。正式 current、配置、CPU 策略及系统 Runtime 的终态指纹与初态一致，桌面仍保持原停止状态。未进行永久部署或修改正式配置。正式服务恢复后重新占用 DRM 属于预期正式运行，不表示测试资源泄漏。

证据索引：`initial-state.txt` 记录服务、拓扑和系统身份，`install-check.txt` 记录动态链接及库指纹，各 `*-run-console.txt` 记录正式服务恢复；`restored-state.txt` 是最后一次补测后的终态。`board/<窗口>/events.jsonl` 使用单调计时记录触发、实时状态及退出，`node.log` 保存实际状态、计数、控制清框和亲和性；`resources.jsonl` 是每秒资源证据。`analyze.py` 与 `analysis.json` 保留计算方式和结果，`read_bags.py`、`readback-console.txt` 与 `board/bag-readback.json` 保存全部核心 bag 的实际反序列化结果。原始录像及元数据保存在对应 `records/`；三个核心证据包与两个资源补测证据包均保留。

## 本次生产结论的限制

本次完成的是规定的持续预览短窗口、真实推理负载、显示性能、录像边界及 ROS 兼容检查。没有执行历史六轮固定回放、五/十/三十分钟长测或长期稳定性检查。没有人工目标真值、准确率、移动/小目标/暗光画质验收，没有实飞或曝光时间精度证据。姿态输入为模拟值，内参的校准来源未在本次核验，因此不声称旋转补偿精度通过。NPU 设备内存、编码器内部设备内存、进程写入量和功耗未测。没有断电、拔电、强制重启或损坏性试验；此前截断/故障测试的证据没有重新冒充本次实测。Jetson 实机、静态检查工具缺口仍保留。

HDMI 时序证据来自独占 DRM 控制权、成功提交与完成事件及真实新视频计数。没有外部相机或光学设备测量屏幕实际发光时刻、撕裂或屏幕视觉质量；这些不能由软件完成时间冒充。

## 生产阶段完成状态

任务 8.2–8.6 的指定板卡输入、三个定向窗口、生命周期、性能、记录边界和资源汇总证据已取得。整体实施为 39/39 任务完成；后续须由独立验证流程核对代码与规划一致性。本阶段没有同步主规格、归档或提交 Git。最后一次资源补测之后正式服务再次确认为 enabled、active/running，主进程 PID 为 47141；正式版本及配置指纹保持初态。

## 2026-10-13 验证问题修复与离线回归

首次独立验证发现：已经交给显示硬件的新画面在 OFF 期间安全完成时，会被计入成功显示数量，但因跟踪控制版本变化而漏出新画面间隔链。此处的间隔链表示连续视频内每张成功新画面与其前一张画面的完成时间关系。`src/osd/presenter.cpp` 现按视频流身份和间隔代际确认持续预览的已完成画面。间隔代际在流结束、重启或过期等待时改变，仍阻止跨不连续段采样。尚未交给硬件的旧包授权仍使用控制版本；旧模式停止、新轮及旧结果拒绝规则保持。

`src/osd/tests/presenter_test.cpp` 的 `continuousInFlightInterval()` 在 legacy 与 cached-overlay 两条路径使用虚拟单调时钟。三张新画面分别于 10、20、40 ms 完成，第 2 张硬件授权后受控阻塞，在阻塞期间发送 OFF。回归逐步断言第一个间隔为 10 ms，随后两个间隔的合计为 30 ms、最大值为 20 ms，成功显示数为 3。测试同时确认旧跟踪包被拒绝、OFF 清除算法结果和目标装饰、在途硬件不提前复用缓冲、原图及映射引用均已释放。此用例在原实现上失败，在修复后通过；原停止/重启、过期等待、所有权和故障用例仍通过。

修复后的源码使用原 ARM64 镜像 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 执行 `./build.sh --test`，构建与安装成功，45/45 CTest 通过，测试总时长为 24.63 秒。worktree 构建沿用只读挂载原 Git 元数据的本地 Docker 包装脚本，不修改源码构建入口或 SDK。日志有未修改的 NanoTrack 基线静态库 0.013 秒共享挂载时钟偏差提示；编译、安装及全部测试退出码为 0。镜像仍没有 `clang-tidy` 或 `clangd`，仓库没有配置相应检查命令，静态检查缺口保持。

本轮证据保存于 `.docker-artifacts/log/continuous-preview-finding-fixes/`：`regression-before.log`、`regression-after.log` 为失败/通过的定向回归，`build-console.log` 和 `test-console.log` 为最终构建/CTest，`artifacts.sha256` 为修复后源码与安装程序身份。修复后节点 SHA-256 为 `81c8c68e750e3e4bdc143858ee00d21ab7e4fdf36d0ebaae316620cd880e955d`。`openspec validate ... --strict` 和 `git diff --check` 通过，任务仍为 39/39。

本轮没有连接生产板或修改正式服务。前文生产性能、录像和恢复证据仍只对应节点指纹 `c493b99d3c08d8b6fc59bc3ded0f1767b8d3ca63cb3b54e5056c3783f448f9a3`，不作为修复后安装程序的实机运行证明。另已纠正 bag 与实时状态的来源归属，以及 `analyze.py` 的脚本名称。是否消除原验证问题由后续独立复验决定，本节不改写其验证结论。
