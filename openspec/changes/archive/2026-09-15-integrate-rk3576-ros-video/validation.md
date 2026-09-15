# RK3576 ROS 视频接入验证记录

## 范围与结论

截至 2026-09-15，进度为 34/34。已修复首次 ON 复用已移走文件句柄的问题，完成实机 6.3–6.7。最终同一节点完成前置 20 轮，再额外完成 181 轮、1801.933221 秒有效播放，退出码 0，无残留进程。后文按日期保留初次测试、失败复现及修复后的证据；历史暂停和未完成负载不计入最终通过结果。本记录不证明真实模型检测质量、NPU 设备内存稳定性或 Jetson 实机等价性。

### 2026-09-14 无设备验证范围

当日在 macOS 开发机上的既有 ARM64 ROS 2 Humble 临时容器完成实现与无设备验证。实机任务 6.3–6.7 当时按用户要求未执行。

模型使用测试指定输出的替身，运行真实公共 `HybridTracker`。未加载真实 RKNN 模型、未调用 NPU（神经网络处理器）推理、未连接 RK3576 开发板。输入包括生成的灰度帧，以及测试生成的 MJPEG 文件；MJPEG 表示各帧独立采用 JPEG 编码。

## 构建与环境

使用原镜像 `roc-rk3576-ros2-build:humble-arm64`，镜像身份为 `sha256:422f87e51551f014e12a46c6307c192c87341e739b9ee5d47e302a9984b932a2`。所有容器保留 `--rm`，退出后自动删除。仓库和 build/install/log 按 `AGENTS.local.md` 挂载，产物位于 `.docker-artifacts/`，不提交版本库。

旧 CMake 缓存导致首次 `spdlog` 误报缺失，清理配置缓存后恢复正常。现有镜像缺少三个已有 ROS 依赖，在当次临时容器内补装 `ros-humble-vision-msgs`、`ros-humble-mavros-msgs`、`ros-humble-cv-bridge` 后构建通过；没有修改或重建 SDK 镜像。详细本机约定已补入 `AGENTS.local.md`。

容器中执行：

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-args \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_PLATFORM=rk3576 \
  -DDRONE_TRACKER_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
source install/setup.bash
ctest --test-dir build/drone_tracker --output-on-failure

cmake -S src/drone_tracker/src/platform/rk3576 -B build/rk3576-standalone \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON
cmake --build build/rk3576-standalone -j4
ctest --test-dir build/rk3576-standalone --output-on-failure
```

ROS 节点与测试构建通过。独立 `rk3576_yolo_probe`、`rk3576_nanotrack_probe`、`rk3576_tracker_replay` 均构建成功。两入口共享 `backend.cmake` 中的后端库清单，生产节点不链接 `replay_common` 报告模块。

## 测试证据

| 层次 | 结果 | 覆盖范围 |
|---|---|---|
| 根目录 ROS 与公共测试 | 12/12 通过 | 含素材生成、ROS 集成、正式入口初始化错误、Jetson 配置策略及 8 项公共测试 |
| 独立 RK3576 入口 | 27/27 通过 | 模型契约、错误注入、视频时钟/队列/重启、历史模式、顺序回放及旧工具参数错误 |
| 平台选择脚本 | 通过 | 默认 Jetson、未知值提前拒绝、RKNN 缺失诊断含 Runtime 2.3.2/驱动 0.9.8，RK3576 不进入 CUDA 发现 |
| OpenSpec 严格校验 | 通过 | `openspec validate integrate-rk3576-ros-video --strict` |

公共 `preprocess_contract_tests` 新增的轮次测试验证：撤销尚未执行请求并释放帧；推理阻塞时收尾不得提前返回；预处理开始失败、区域预处理失败和普通推理失败后能够完成收尾；已就绪结果被清除；相同帧号在新轮可重新使用。会话释放计数用于检查收尾返回前不再持有旧预处理会话。原 ROI、全图、切片顺序和目标类别数值回归仍通过。

视频测试新增公共状态查询覆盖：并发查询不破坏统计，十秒 PTS 间隔中的超时仍为 Running，EOF 查询不消耗尾帧，stop 保留失败原因，重启统计不累积。PTS 是视频帧的显示时刻。

`ros_video_tests` 通过实际 ROS 订阅验证：

- 四模型路径缺项/混填、类别越界、非有限内参和时钟配置被拒绝。绝对路径不加前缀，相对路径基于包 share/models，内参不再次缩放。初始化后更新相关参数明确失败。
- 真实公共跟踪器依次进入 Detecting、TargetFound、Tracking、TargetLost，并恢复 Tracking。无目标时检测 frame_id 为空；有效框为 `camera_frame`。状态数值仍为 0、1、2、3。
- 图像为独立 BGR8、时间与来源帧一致；检测时间晚于输入帧时间，表示当前预测时刻。绘制不会修改共享灰度帧。图像关闭时不发布。
- 通过触发话题检查重复 ON/OFF，并通过可测试控制入口覆盖 EOF 后直接 ON、阻塞检测期间 OFF、旧有效框不能进入新轮、尾帧状态竞争和启动/输入/线程创建失败后的重试。跨轮组件只装配一次。
- 在姿态、里程计持续发布时执行 OFF 或等待 EOF。关闭后的 bag 可重新打开读取，包含原四类消息。四元数到相机旋转矩阵的映射保持原行为。记录目录碰撞不覆盖；注入打开或写入失败后可清理和重试。
- 在同一测试节点中接入真实 `Rk3576VideoFrameSource`，使用生成的 `long.mov` 检查十秒 PTS 间隔中不会误结束，OFF 在一秒内返回；使用 `420.mov` 验证 EOF、灰度图像发布及再次 ON。模型仍为可控替身。

`jetson_config_tests` 在 ARM64 容器只编译和执行公共 ROS 配置代码。它验证 Jetson 原三模型默认相对路径、外部绝对路径及拆分字段拒绝，不链接 Jetson 设备后端。另以 `git show jetson:src/drone_tracker_node.cpp` 核对话题、消息类型、队列设置、时间和姿态映射。以上都不算 Jetson 实机验证。

`ros_initialization_tests` 验证正式可执行文件在非法历史/模拟时钟配置下，以退出码 1 返回并包含具体参数诊断。独立回放入口原有错误退出测试继续通过。

原始日志保存在 `.docker-artifacts/build/drone_tracker/Testing/Temporary/LastTest.log` 和 `.docker-artifacts/build/rk3576-standalone/Testing/Temporary/LastTest.log`；构建日志在 `.docker-artifacts/log/`。

## 安装与依赖

`readelf -h` 确认正式节点是 AArch64，即 ARM64 可执行文件。`ldd` 确认节点未链接 CUDA、TensorRT、VPI、Tegra，独立回放未链接 ROS。完整依赖记录在 `.docker-artifacts/log/rk3576-node-ldd.txt` 和 `rk3576-replay-ldd.txt`。

安装后的节点需要运行环境能够找到 `librknnrt.so`。容器内检查时显式将既有 SDK 的 lib 目录加入 `LD_LIBRARY_PATH` 后无缺失库，非法初始化检查按预期退出。部署时必须使用开发板的 Runtime 2.3.2，不能把编译 SDK 2.3.0 当成目标运行版本。本次没有部署任何库或修改开发板环境。

安装目录中运行 `ros2 launch drone_tracker rk3576_video.launch.py --show-args` 成功，显示可选 `params_file` 和必填 `video_source`。launch 只覆盖视频路径，不覆盖算法。参数示例故意保留空模型路径和无效内参，用户填写实际模型与标定后才能启动。

## 未执行与证据边界

- 6.3–6.7：未部署独立验收前缀，未收集实机模型/视频 SHA-256，未执行纯 FP16/INT8 基础集成、退出失败检查、同节点 20 轮启停或额外 30 分钟负载。
- 未测真实 NPU、设备内存、实机 RSS、线程和文件描述符趋势。可控测试中的等待上界不代表设备推理停止耗时保证。
- 缺少 Jetson SDK/硬件，未编译完整 Jetson 后端或执行 Jetson 实机回放。
- 镜像没有 clang-tidy/clangd，仓库没有相应 lint 或语言服务器检查配置。本次使用编译器检查、契约/并发测试和 diff 检查，静态分析工具检查未执行。
- 未评价真实模型检测质量，没有改变阈值以制造捕获。`assemble-rk3576-tracker-replay` 的 7.2、7.3 继续未完成，本次没有修改其工件。

## 工件一致性核对

proposal 与 design 中的构建共用、显式配置、实时消息语义、单一帧源控制、跨轮检测清理、记录互斥均有实现和上述证据。两份增量规格（本次对行为要求的补充和修改）中的非实机要求分别由公共测试、ROS 测试与安装检查覆盖；设备验收保持未完成。任务只按各自证据勾选。

旧回放变更的增量规格 仍包含“拒绝视频/根目录 RK3576”的历史限制。后续同步时，应以已经实现的视频帧源和本 change 的开放范围协调这些条款，不得把主规格覆盖回旧限制。本次未同步或归档任何 change。

## 2026-09-15 实机续做：独立部署

本次继续执行此前暂停的 6.3–6.7。6.3 已完成：将既有 ARM64 安装产物部署到开发板 `/home/firefly/drone_tracker-ros-acceptance-20260915/drone_tracker`，保留原安装副本。回退时停止此目录的节点，再使用原安装前缀即可。未替换系统 RKNN 库。

固定输入 `fixed.mov` 由已有 AirSim 30 FPS 视频的前十秒转为 MJPEG，尺寸 960×968、30 FPS。生成命令为 `ffmpeg -v error -nostdin -i /home/firefly/2025-08-15_airsim_tracking_test_30fps.mp4 -t 10 -an -c:v mjpeg -q:v 3 -pix_fmt yuvj420p fixed.mov`。配置沿用此前诊断内参 fx=fy=800、cx=480、cy=484；这是接口诊断配置，不是实测相机标定。目标类别为 2，置信度阈值保持 0.5，DCF 开启。

两组配置分别为 `fp16.yaml`、`int8.yaml`，使用同一个 YOLO 和对应纯精度 NanoTrack 三文件。节点真实加载日志报告 Runtime 2.3.2、驱动 0.9.8；驱动 debugfs 查询也为 0.9.8。完整模型绝对路径、SHA-256 文件指纹及配置保存于板端同一验收目录，并已回收至本地 `.docker-artifacts/ros-board-20260915/` 的 `identity.json` 和两个 YAML 文件。

| 文件角色 | SHA-256 |
|---|---|
| 固定 MJPEG | `71ab715ab26e3297b56d89834244f2a54be86d3f8de2e26fa3b5684f87c85eda` |
| 正式节点 | `64461e44b2f094a6ef9cea406753c1fb0f1eb862f5d052e3b68facccf86e5e5c` |
| 系统 Runtime | `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` |
| YOLO | `4d8a22bc8da9fbbc43f190b7ea90c27d9980f77e3e27064ec32cf6566c2680f3` |
| FP16 模板 | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| FP16 搜索 | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| FP16 Head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |
| INT8 模板 | `848ec40a659b5a3484c0d76ba6ded353250f524c7f384fec95a0f85cf2682bdf` |
| INT8 搜索 | `ecabaefdeadba721b466d942f76195bb9a5184e8e0a7d2a8262cc2d86e38ccdd` |
| INT8 Head | `f4fc6259eaeb83860515773df49391c860a3be31507f613b2c15fcde6430e736` |

### 6.4 两组基础集成通过

在隔离 ROS 域 87、仅本机通信下，运行 `run_checks.py`。它启动正式节点，通过 ROS 话题发送 ON/OFF、单位姿态与里程计，并订阅检测、图像和状态。每种精度在同一进程完成一轮 EOF 和重启后 OFF，另外启动图像关闭配置完成 EOF。脚本使用 ROS Humble 配套系统 Python。

| 精度 | 完整轮检测/图像消息 | 有效框消息 | 图像关闭时检测/图像 | 完整轮状态 |
|---|---:|---:|---:|---|
| FP16 | 293 / 293 | 89 | 294 / 0 | 0→1→2→3→0 |
| INT8 | 297 / 297 | 92 | 295 / 0 | 0→1→2→3→0 |

每组重启前后首张 BGR8 图像指纹一致、时间戳更新。四份 bag 关闭后 SQLite 完整性检查通过，均包含 `/fpv/gimbal`、`/fpv/odom`、`/fpv/target_bbox`、`/fpv/tracker_image`。这同时证明姿态与里程计进入了记录回调；单位姿态输入不证明物理旋转补偿正确。OFF 从发送到收到停止状态并等待 0.2 秒后的观测耗时分别为 0.706、0.686 秒；状态发布有定时器延迟，不将此数值视作纯推理清理耗时。

两组在完整轮中捕获后丢失。重启后的短轮尚未捕获即主动停止。以上仅为接入结果，未评价检测质量或改变前序回放任务状态。原始结果为 `basic-results.json`，命令与各轮控制台日志保存在独立验收目录。

### 6.5 首次启动失败检查发现阻塞

`run_failures.py` 在第一项断言失败后停止，尚未执行其后损坏输入、模型缺失和记录目录失败检查。复现步骤：

1. 将固定视频复制为验收目录内的 `failure-input.mov`。
2. 用该路径初始化正式 INT8 节点，等待触发订阅建立。
3. 将该副本重命名为 `failure-input.saved`，使配置路径不存在。
4. 发送第一次 ON。

预期为输入启动失败且不发布成功开始。实际日志进入 Running、发生目标捕获，并收到 `Drone Tracker Started`。`Rk3576VideoFrameSource::start()` 仅在 `attempted` 已为 true 时重新打开文件；第一次启动复用构造阶段已打开的 reader。Linux 中重命名文件不会使已打开的文件句柄失效，因此首次 ON 未检查当前路径是否仍可用。这违反增量规格“启动时文件不可读”场景。

测试的 finally 清理向该节点发送 SIGINT，进程正常退出。随后 `pgrep -af '[d]rone_tracker_node'` 无结果。未改动原视频或旧产物；重命名的只是独立验收副本。日志已回收至 `.docker-artifacts/ros-board-20260915/input-failures.log`。建议修复首次 start 的重新打开行为，补充对应帧源回归，再构建部署后重跑 6.5–6.7。

本次在该实现问题处按 apply 技能暂停。6.3、6.4 已完成，当前进度 31/34；6.5、6.6、6.7 未完成，20 轮与额外 30 分钟负载尚未启动。OpenSpec 严格校验与 `git diff --check` 通过，它们不替代失败的行为验收。

## 2026-09-15 修复后续验收

用户选择修复并继续。帧源构造阶段仍校验输入与尺寸，但校验后释放 reader 和缓存首帧。每次 start 都重新打开路径，包括第一次；保持模型常驻和其他播放语义。新增真实文件回归覆盖初始化后移走文件、首次启动失败、不暴露缓存帧、恢复文件后重试成功。原重开尺寸与中途 I/O 错误测试同步调整注入时机。

约定 ARM64 临时容器构建通过，ROS/公共测试 12/12、独立测试 27/27 通过。首次测试执行曾因未加载安装环境、测试试图写入只读素材目录而失败；分别补上 `source install/setup.bash`，并将可变输入放到系统临时目录后通过。构建日志为 `.docker-artifacts/ros-board-20260915/rebuild.log`；最终测试日志为其 `fix1/retest.log`。没有重建 SDK 镜像。clang-tidy/clangd 仍不可用，未执行该静态检查。

修复版使用新的板端前缀 `/home/firefly/drone_tracker-ros-acceptance-20260915-fix1/drone_tracker`。节点 SHA-256 为 `fb084963b11b5eaf96cf43b20e655eebd772fa021da7c6880b326e19e8660352`。固定视频、四模型和系统 Runtime 的指纹与修复前一致；修复前副本、日志和配置均保留。新脚本、配置及证据保存于板端 `fix1` 验收目录及本地 `.docker-artifacts/ros-board-20260915/fix1/`。

### 6.5 修复后失败与退出检查通过

`run_failures.py` 在正式 INT8 节点完成以下检查：

- 首次 ON 前移走验收副本：明确报告 `Input start failed`，没有发布启动成功。恢复文件后同一节点成功重试并 OFF。
- 修改副本第 21 个视频包的 JPEG 头部：读取到该帧时报告 `Input failed ... frame=21: JPEG decode`，未报告 EOF。恢复副本后同一节点成功重试并 OFF。
- 播放期间 SIGINT：正常退出，退出码 0。
- 将独立记录目录权限设为不可写：两次 ON 均报告 `Round start failed`，节点保持可用。恢复权限后同一节点成功开始记录，再 OFF 收尾。
- Head 路径不存在：正式节点初始化退出码 1。

每个被启动的测试子进程均由脚本等待退出；随后进程检查仅见正在执行下一阶段的负载节点，没有上述失败测试的残留节点。原始证据为 `failure-results.json`、`input-failures.log`、`recording-failure.log`、`missing-model.log`，已回收本地。修复版基础集成也已重跑通过，结果为 `fix1/basic-results.json`：FP16 完整轮 292 条检测/图像、79 条有效框；INT8 为 297 条检测/图像、94 条有效框。两组图像关闭、记录和 EOF/重启检查继续通过。

### 6.6 同一节点 20 轮通过

`run_load.py` 使用固定纯 INT8 配置，DCF 和图像输出开启、记录关闭。在同一 PID 4298 中交替执行十轮主动 OFF 与十轮 EOF，共 20 轮、3635 条已消费帧对应的检测消息。首张图像指纹各轮相同，首帧时间戳逐轮更新。日志仅有四次模型身份输出和一次 YOLO 工作线程启动，确认模型跨轮复用。

轮间空闲采样：线程数始终 20；文件描述符从第一轮的 40 增至第二轮的 48，随后保持 48。该一次性增量发生在首次完整跟踪轮附近，未对新增句柄逐项归因；需结合后续长负载判断是否持续增长。RSS 从第一轮 145536 KiB 至第 20 轮 157476 KiB，中间有波动。RSS 表示进程驻留物理内存，不包括对 NPU 设备内存的独立测量。

主动停止的最大观测耗时为 0.744 秒，包含停止状态定时发布及测试端额外 0.2 秒等待，不是纯设备清理时间。20 轮完成后在同一进程开始独立长负载计时。原始轮次数据为 `reuse-results.json`，阶段摘要为 `twenty-summary.json`。额外负载未完成前不勾选 6.7。

### 6.7 首次长负载未完成，重新独立执行

`fix1` 的图像订阅在少数轮次出现超过固定视频跨度的首尾消息时间差，因此不能直接累计原始时间差作为有效播放时长。为避免高估，最终计时应对每轮按固定视频首尾 PTS 跨度 9.966666 秒封顶。

尝试保留节点并交接测试驱动时，向原 Python 驱动发送 SIGTERM。ROS Python 已安装该信号处理器，原驱动进入 finally 并正常关闭了 PID 4298 的正式节点，交接失败。本次额外负载未足 30 分钟，不计为 6.7 完成，也不与下一次累加。原始数据及失败驱动日志保留在 `fix1` 目录；此前 6.5 与 20 轮结果仍有效。

随后使用 `/home/firefly/drone_tracker-ros-acceptance-20260915-fix2` 独立目录重新开始。二进制、模型和视频保持相同，仅将计时驱动改为从第一轮起按上述上限累计。新 PID 5773 重新执行 20 轮，再独立累计至少 1800 秒，不交接驱动。修复版产品代码没有因测试计时调整而改变。

### 6.7 独立重跑通过：完整时长与最终退出

`fix2/run_load.py` 在同一 PID 5773 中完成以下两段。配置为固定纯 INT8 NanoTrack、同一 YOLO、DCF 开启、图像开启、记录关闭。输入与模型身份和前述修复版一致。前次 `fix1` 未完成负载没有计入。

| 阶段 | 轮数 | 已观测检测消息对应的处理帧数 | 结果 |
|---|---:|---:|---|
| 前置交替 OFF/EOF | 20 | 3647 | 首帧图像一致、时间基准更新，全部完成 |
| 额外完整播放 | 181 | 53700 | 修正后的有效时长 1801.933221 秒，全部 EOF |

额外负载循环的实际墙钟时长为 1918.450480 秒。原始图像首尾时间差之和为 1812.003307 秒，其中 15 轮出现大于固定视频跨度的观测差，不能直接使用。每轮以 `min(观测时间差, 9.966666 秒)` 计入，排除轮间等待并防止延迟消息高估时长；封顶后的 1801.933221 秒仍超过 30 分钟。记录中保留全部原始差值与异常轮号。这些 ROS 传输观测不用于推断算法跨轮使用了旧检测结果，算法隔离另由可控回归验证。

模型身份输出共 4 次，YOLO 工作线程启动 1 次；视频启动 201 次，EOF 191 次，对应 20 轮中的十次 EOF 加 181 次长负载 EOF。最后由驱动向正式节点发送 SIGINT，`wait()` 得到退出码 0，`/proc/5773` 已不存在。最终 `pgrep` 检查没有正式节点、旧驱动或补跑驱动残留。

前置 20 轮的主动停止最大观测耗时为 1.686 秒，包含状态定时发布和额外 0.2 秒测试等待，不等同于纯推理停止耗时。额外负载中每轮结束后采样的线程数始终 20、文件描述符始终 48。

RSS 全段范围为 158856–186364 KiB。中途出现约 21 MiB 的台阶式增加，但后续均值没有继续上升：

| 长负载轮次 | 平均 RSS（KiB） |
|---|---:|
| 1–20 | 161350.0 |
| 61–80 | 161440.0 |
| 101–120 | 182909.2 |
| 121–140 | 182948.6 |
| 141–160 | 183094.2 |
| 161–180 | 182792.4 |

未对这次单次内存增量逐项分配归因；后续约 80 轮维持平台状波动，未观察到持续增长或停止失败。本结论限于该配置、该输入和本次时长。采样为每轮结束后的进程 RSS，不是运行峰值分析，也没有测量 NPU 设备内存。

原始逐轮数据为 `fix2/reuse-results.json`，摘要为 `fix2/resource-summary.json`，节点日志为 `fix2/reuse-load.log`，脚本输出为本地 `fix2/board-run.log`。全部已回收至 `.docker-artifacts/ros-board-20260915/`。最终复核节点、视频与系统 Runtime 的 SHA-256 与修复版身份一致。

真实模型的长负载输出中有 17712 条有效框消息，但未进行质量标注评估；不据此改变前序回放 7.2、7.3 的未完成状态。Jetson 实机、NPU 设备内存和缺失的 clang-tidy/clangd 检查仍未执行。

### 最终规格核验

首次 ON 前文件消失场景已由真实文件回归和实机失败/恢复重试覆盖。6.3–6.7 均有独立部署、基础集成、失败清理、同进程复用及完整时长证据。当前任务进度 34/34。最终执行 `openspec validate integrate-rk3576-ros-video --strict` 与 `git diff --check`；未同步、归档或修改前序回放变更。
