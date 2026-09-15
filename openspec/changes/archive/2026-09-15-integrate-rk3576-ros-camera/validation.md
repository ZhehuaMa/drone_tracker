# RK3576 ROS 相机接入验证记录

## 2026-09-15 完整实机验收续做

当前进度 **38/38**，尚未归档。独立相机矩阵、10 分钟慢消费、能力补查、ROS 八组基础配置、四组故障恢复、同进程 20 轮及额外 1806 秒负载均已完成。基础矩阵中接收旧消息的异常已定位为验收端 ROS 传输积压；按每轮独立接收窗口补查 30 轮通过，未修改生产节点或话题 QoS。完整证据汇总见 [最终实机记录](final-board-validation.md)。下文保留历史阶段，旧进度与阻塞不代表当前状态。

## 2026-09-15 内参临时方案：最新配置

用户确认先用理论值或模拟值代替，正式标定镜头后再修改。由于当前镜头焦距没有可靠来源，本次选用明确标注的模拟值，而不假设旧镜头参数适用。仓库 `parameters/rk3576_camera_params.yaml` 已配置 `fx=fy=1000.0、cx=960.0、cy=600.0`，单位为像素，对应 1920×1200 图像。前两值人为选定，后两值暂取图像中心。

这解除后续 ROS 集成测试对实测内参的前置要求；配置和报告必须保留模拟来源。旋转补偿精度及正式跟踪质量不能据此验收。标定完成后替换四值并重启节点，不修改运行期参数锁定或有限数值校验。此前“缺少有效相机内参”的描述为历史阶段，以本节的用户确认更新为准。

本次只更新仓库配置、使用说明和相关规格，未启动 ROS 或模型。任务进度仍为 28/38，不把配置占位替换视为任务 7.1 的模型及部署核验完成。

## 2026-09-15 控制读回超时修复：最新结论

用户要求在超时内等待写入值生效。已为初始化设置和原值恢复统一增加确认等待：每项 100 ms，以单调时钟计时，每隔 5 ms 重读，首次一致即成功。等待期间只读取，不重复写入。超时错误包含设备、控制名、预期值、最后读回值和耗时；读写系统调用报错仍保留原错误。恢复失败继续记录未恢复项，并允许释放句柄后再次停止重试。

设计、相机帧源规格和任务 2.5 已同步。当前进度为 **28/38**，其中新增任务 2.5 已完成，其他未完成实机项仍保留。本节覆盖下文“控制恢复错误仍阻塞”的历史状态；完整 ROS 验收仍缺少有效相机内参，未因此视为完成。

### 构建与替身测试

使用既有 ARM64 临时容器，镜像未重建。独立相机/公共测试 14/14、ROS/公共测试 12/12 通过，ROS 节点与安装树已重建。日志位于 `.docker-artifacts/log/control-readback-build.log` 和 `control-readback-ros-build.log`。

新增可控时钟测试覆盖立即生效、20 ms 延迟、恰好 100 ms 成功、持续不一致超时、初始化回滚、恢复超时后的句柄释放及再次重试、读回系统调用错误。测试断言等待期间写入次数不增加，超时诊断包含预期/实际值和耗时。镜像仍无 clang-tidy/clangd，Jetson 构建及实机检查未执行。

### 开发板 2 复测

新工具位于 `/home/firefly/ros-camera-control-timeout-20260915/`，未覆盖旧失败产物。原始报告和程序指纹已复制到本地 `.docker-artifacts/log/control-readback-board2/`，指纹文件为 `binary.sha256`。测试使用实际 1920×1200 GREY 相机，未加载模型。

沿用下文设备参数，执行两组各三轮的有限触发：

| 用例 | 每轮命令/帧数 | 停止耗时 | 控制恢复 |
|---|---|---|---|
| 1 Hz、单次命令 | 三轮均为 1/1 | 528.280、522.598、532.448 ms | 全部成功 |
| 20 Hz、60 次命令 | 三轮均为 60/60 | 534.776、526.529、527.287 ms | 全部成功 |

20 Hz 实际命令频率约 19.9984–19.9986 Hz，无控制错误、周期跳过、驱动序号缺口或队列丢弃。两组尾帧等待均为 500 ms。最终独立读回控制值为 0/1/60，设备无占用，测试进程已退出。

原立即读回失败用例已通过复测，说明该等待策略在本次检查中有效；没有测出固件内部的确切生效延迟，不声称设备根因已经确定。上述六轮不替代完整频率矩阵、100 次销毁或持续负载验收。每项确认等待不代表能强制中断阻塞的 ioctl；本次实测整体停止均小于 1 秒。

## 2026-09-15 重启后复测与 ROS 依赖补齐

用户重启开发板 2 后要求重试，并明确授权安装缺失 ROS 依赖。本节为最新状态，覆盖下文“尚未安装缺失依赖”的历史结论。

- 已安装 `ros-humble-vision-msgs`，版本 `4.1.1-1jammy.20260907.225757`。加载 Humble 环境后，对已部署节点执行 `ldd -r`，没有缺失库或未解析符号。系统 RKNN Runtime SHA-256 保持 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。安装及复核日志分别为本地证据目录中的 `install-vision-msgs.log`、`ros-dependencies-after-install.log`。
- 在板卡启动约 1 分钟、相机无占用且控制值为 0/1/60 时，重新执行下节的单次三轮命令，仅将输出目录改为 `trigger-hz1-after-reboot`。仍在创建验证阶段报告 `restore /dev/v4l-subdev2 readback mismatch trigger_src`，没有完成任何轮次。重启没有消除该问题。测试后再次查询为 0/1/60，无设备占用；日志及 CSV 已保存到本地证据目录。
- 按用户提供的路径检查 `/home/firefly/workspace/ar0234_osd_hdmi`。其中 README、`hw/camera.*` 和演示代码只提供采集尺寸、帧率、设备与显示缩放配置，没有找到 fx/fy 或相机矩阵、畸变系数、标定结果。`cx/cy` 的命中是 OSD 绘图中心，例如 `draw_crosshair` 的坐标，不能当作相机主点。源码摘录保存在 `osd-camera-source.txt`。

本次没有修改相机实现、曝光设置或用猜测内参启动 ROS。恢复读回错误仍阻塞后续验收，内参来源仍待确定；任务进度保持 27/37。

## 2026-09-15 实机续做：遇到控制恢复失败

本节更新下文历史暂缓状态。用户重新调用 apply 后，已在开发板 2 开始新实现验收；当前仍为 **27/37**，没有新增完成勾选。单次触发检查在相机创建验证阶段失败，因此暂停后续矩阵及负载测试。

### 设备能力查询

原始日志保存在本地 `.docker-artifacts/log/ros-camera-board2-continued/controls-read.log`，厂家脚本副本为同目录的 `vendor-tool.sh`。板端工具来自 `/home/firefly/workspace/rk35xx_veye_bsp/mv_tools_rockchip/i2c_tools/`。只复制脚本与读取程序到新目录，未修改原工具，也未写曝光或未知寄存器。

执行 `sudo -n bash mv_mipi_i2c_new.sh -r <功能名> -b 4`。其中 `-r` 表示读取，4 是媒体拓扑确认的相机 I²C 总线编号。结果如下：

| 功能 | 读回结果与边界 |
|---|---|
| model、version | RAW-MIPI-AR0234M；C 01.16、L 00.00 |
| trgmode、trgsrc | 0、1，与 V4L2 的连续模式、硬件来源一致 |
| trgnum、trginterval | 每次 1 帧、间隔 0；本次没有修改 |
| expmode、exptime | 模式 2，当前曝光 14715 微秒；仅为读取时的值 |
| trgcount | 总数 0、丢失数 0；尚未证明此计数器随新软件触发有效递增 |
| framecount | 传感器及输出均为 -1，不能用作有效帧数 |
| trgmodecap | 0x3 |
| trgcycle | 当前连续模式下最小/最大周期均为 0 微秒，不能据此判定任意频率可用；软件模式下仍待核验 |

厂家[工具手册](https://wiki.veye.cc/index.php/Mv_mipi_i2c.sh_user_guide)说明 `trgnum` 为单次触发输出帧数，`trgcycle` 用于查询当前触发模式的周期。[相机手册](https://wiki.veye.cc/index.php/Mv_mipi_camera_manual)将 AR0234 列入曝光与读出依次进行的普通触发模式。因此曝光和读出时间是调查方向，但这不是本板 30/60 Hz 数量差的已证实原因。任务 1.2 保持未完成。

### 隔离部署与依赖

新目录为开发板 2 `/home/firefly/ros-camera-acceptance-20260915-continued/`，复制已有 ARM64 工具和 ROS 安装树，未覆盖系统运行库或历史目录。`preflight.log` 已取回本地同名证据目录。

- 工具 SHA-256：`6044377d74b99feb44b7c814ee1f0c45c259d9f8ad8593bb467da04946b047bb`。
- 节点 SHA-256：`e9dba65b927ebab5c90391277ac422279f82bc4d3206bac654375969411fe391`。
- 系统 Runtime 文件指纹与既有 2.3.2 记录一致；驱动实读为 0.9.8。
- 相机拓扑仍为 `/dev/media0`、`m00_b_mvcam 4-003b`、`/dev/video0`。运行前后均未发现设备占用。
- 独立工具动态依赖齐全。加载 ROS Humble 环境后，节点仍缺 `libvision_msgs__rosidl_typesupport_cpp.so` 并有对应未解析符号；尚未安装缺失依赖，6.3 不勾选。

### 新工具运行与阻塞

公共命令参数如下，分别追加两个用例的参数：

```sh
./camera_source_driver --video-device /dev/video0 --media-device /dev/media0 \
  --sensor-entity 'm00_b_mvcam 4-003b' --width 1920 --height 1200 --fps 60 \
  --mode software_trigger --tail-ms 500 --snapshots 0
# 无触发：--trigger-hz 20 --commands 0 --output trigger-zero
# 单次三轮：--trigger-hz 1 --commands 1 --rounds 3 --output trigger-hz1
```

无触发用例正常结束，结果保存在 `trigger-zero/` 与 `trigger-zero.log`。下一用例失败，`trigger-hz1/rounds.csv` 只有表头，没有完成轮次。错误为：

```text
采集失败：rk3576: camera [/dev/video0]: ; restore /dev/v4l-subdev2 readback mismatch trigger_src
```

代码中相机创建会执行配置验证，再调用 `finish()` 恢复控制；本次错误来自恢复后的立即读回检查。尚未确定是设备生效时序还是其他原因，不能通过删除读回检查来认定成功。后续 10/15/20/30/60 Hz 测试未执行。

失败后独立查询为 `trigger_mode=0, trigger_src=1, frame_rate=60`，设备无残留占用。这只能证明查询时已恢复，不能抹去程序报告的恢复失败。全部成功及失败报告已复制到本地证据目录。没有后台验收进程。

后续需先定位并修复恢复读回不一致，再重跑有限触发矩阵。ROS 验收还需要补齐缺失依赖及明确当前镜头的像素内参来源。没有运行真实模型或新增负载，也不改变前序回放、模型质量和曝光同步结论。

### 内参来源调查

用户表示不知道内参位置，并要求自行调查。已检查仓库参数、开发板 2 的 workspace、Documents、Desktop、NanoTrack 配置及 `.ros`，也检查本机 Firefly 项目中的标定相关文件。没有找到能对应当前镜头、1920×1200 图像的标定结果。NanoTrack 项目的 calibration 文件用于模型量化数据准备，不是相机几何标定。

仓库旧配置存在 12 mm 和 6 mm 两种焦距，像元尺寸均写为 3.45 微米；没有证据证明它们对应当前开发板 2 镜头。RK3576 专用内参仍为 `.nan` 占位值，不能直接复制这些旧参数推算后当作实际标定。ROS 任务 7.1 的内参来源条件尚未满足。若后续无法取得原标定，需要对当前镜头采集标定板图像并计算内参；这需要实物标定板和拍摄操作，本次没有执行。

## 范围与当前结论

本轮实施 `integrate-rk3576-ros-camera` 的软件部分。正式工厂、两种采集模式、直接软件触发、控制恢复、周期调度、ROS 参数和独立报告工具已接通。开发板 2 的新程序运行与验收按要求暂缓。用户随后提供三个历史目录，本轮仅通过 SSH 读取和复制已有文件，没有执行相机控制命令、采集、模型推理或部署。

2026-09-15 首次实施只完成任务 2.1，因为本地设备枚举未保存软件触发取值。随后从用户指定目录和此前任务的完整命令记录取得依据，解除阻塞，移除临时的 `Camera software trigger control integration pending` 拒绝分支。该临时行为不属于最终实现。

任务清单以 [tasks.md](tasks.md) 为准。新相机实机验收、模型质量、曝光同步及 Jetson 等价性均未通过本轮验证。前序完整回放 7.2、7.3 保持未完成结论。

## 1.1 历史触发基线归集

### 来源与保存位置

开发板 2 的三个目录均仍存在，已经完整复制到本地 `.docker-artifacts/log/ros-camera-trigger-baseline/` 下的同名目录：

- `/tmp/camera-trigger-20260915-085012`：`single.log` 和五帧 `single.raw`，后者为 12288000 字节。
- `/tmp/camera-trigger-periodic-20260915-085051`：10/30/60 Hz 的日志和 `results.json`，包含命令调用前后时间及原始帧序号/时间数组。
- `/tmp/camera-trigger-confirm-20260915-085156`：15/20 Hz 日志、计数 JSON 和恢复连续采集日志。

使用 `scp -pr` 复制原文件。`SHA256SUMS` 保存本地副本的 SHA-256（文件内容指纹），便于后续核验。原目录本身没有测试脚本，执行命令从此前任务“确定 RK3576 下一步工作”（任务 ID `01a0a434-6f56-7983-ab92-2631fb2dbd75`）的命令与输出记录取得，保存为 `task-command-evidence.json`。它同时保留首次等待过长导致 `select timeout` 的失败尝试，不将该次失败记为通过。

设备查询依据来自 `.docker-artifacts/log/camera-board2-20260915/preflight.log` 及前序相机验收记录。历史媒体设备为 `/dev/media0`，实体为 `m00_b_mvcam 4-003b`，控制节点为 `/dev/v4l-subdev2`，采集节点为 `/dev/video0`。格式为 1920×1200 GREY，每行 2048 字节。

### 已核实控制命令

历史成功测试设置软件模式：

```sh
v4l2-ctl -d /dev/v4l-subdev2 --set-ctrl trigger_mode=1,trigger_src=0
v4l2-ctl -d /dev/v4l-subdev2 --set-ctrl soft_trgone=1
```

第二条命令每次执行发送一次拍摄请求。周期测试使用直接 `VIDIOC_S_CTRL`，控制编号依次为 `0x00981901`、`0x00981902`、`0x00981903`，取值分别为 1、0、1。结束时恢复 `trigger_mode=0,trigger_src=1`，读回设备帧率 `frame_rate=60`。当前实现使用对应 Linux 头文件中的 ioctl 标识，不复制历史 Python 脚本里的编码数值。

`trigger_mode` 查询范围为 0–2，`trigger_src` 为 0–1，`frame_rate` 为 0–60。`soft_trgone` 是只写按钮，工具显示的零不作为相机状态读回。新实现会核对控制名、类型、范围与可读写属性。

### 原始结果及限制

| 测试 | 发送命令 | 返回帧 | 历史结论 |
|---|---:|---:|---|
| 不触发等待 0.5 秒 | 0 | 0 | 来自成功单发脚本的触发前输出 |
| 逐次单发 | 5 | 5 | 每次新增 2457600 字节 |
| 10 Hz | 120 | 120 | 实际命令约 9.9998 Hz |
| 15 Hz | 60 | 60 | 帧频约 14.9995 Hz |
| 20 Hz | 60 | 60 | 帧频约 19.9991 Hz |
| 30 Hz | 120 | 60 | 未满足一触发一帧 |
| 60 Hz | 120 | 30 | 未满足一触发一帧 |
| 恢复连续采集 | 不适用 | 60 | 帧频约 59.7328 Hz |

30/60 Hz 驱动序号连续，但不能因此宣称没有丢触发。控制调用最长约 0.874 ms；调用成功不证明已经曝光。历史周期工具停止发送后通过 `select timeout` 结束，JSON 明确保存该字段；不把这种历史退出口径套到新工具。

任务 1.2 尚未执行：未查询每次触发帧数、曝光时长、最短周期或相机内部计数器，也未新增厂家资料调查。已知是一触发一帧的短测结果，不是帧数控制读回。高频差异仍未归因。

## 软件实现与契约对应

| 任务 | 实现与验证位置 |
|---|---|
| 2.1 | `FrameSourceConfig` 末尾追加频率；连续为零，软件为有限 1..fps；旧聚合初始化保持兼容 |
| 2.2–2.4 | `camera_device.*` 查询、快照、直接命令、读回及恢复；设备测试覆盖查询失败、中途设置失败、单发失败、恢复失败与重复清理 |
| 3.1–3.5 | `camera_frame_source.*` 固定起点周期、跳过计数、采集就绪握手、统一停止；测试覆盖 120 ms 恢复后跳到 150 ms、第二线程失败、采集失败时触发在途及重复停止 |
| 3.6 | 2048 行跨度有效像素复制、复制后归还、持帧跨销毁、慢消费丢新帧和非法时间回归 |
| 4.1–4.4 | 公共统计及 `camera_driver_support.*`；独立命令时间 CSV、有限 0/1/120 命令、尾帧等待和 120 命令/60 帧报告测试 |
| 5.1–5.4 | 正式工厂显式开启；ROS 映射模式/频率/拓扑，支持 video10 与绝对设备路径，初始化拒绝记录；复用既有轮次收尾和模型 |
| 5.5–5.6 | 相机 launch 和完整参数文件；沿用 CMake 的目录安装规则，不需要复制安装逻辑 |
| 5.7 | `ros_video_test.cpp` 增加两模式配置、状态转换、失败重试、多轮、图像开关、姿态和在途模型收尾检查 |
| 8.1 | `src/platform/README.md` 的正式相机、有限触发和恢复说明 |

控制恢复仅涉及本次读取并修改的 `trigger_mode`、`trigger_src` 和 `frame_rate`，不表示恢复全部媒体拓扑。所有原值读取成功后才开始修改。恢复失败会关闭本次控制句柄，保留未恢复项；再次停止可重新打开控制节点重试。工作线程不关闭共享设备，停止流程先等待两个线程退出，再恢复并关闭。

公共帧时间保持原驱动单调时间加本轮固定偏移。`clock_offset_delta_us` 仅报告当前系统时钟偏移与固定偏移的差，不修改帧时间。无曝光依据时，不按命令先后顺序配对帧，也不减去经验帧周期。

## 6.1 ARM64 构建与无设备测试

使用既有镜像 `roc-rk3576-ros2-build:humble-arm64`，指纹为 `sha256:422f87e51551f014e12a46c6307c192c87341e739b9ee5d47e302a9984b932a2`。每次使用 `docker run --rm`，仓库挂载为 `/work/ros2_ws/src/drone_tracker`，build/install/log 分别挂载到宿主 `.docker-artifacts/`。镜像未修改或重建。

当次临时容器按 `AGENTS.local.md` 补装 `ros-humble-vision-msgs`、`ros-humble-mavros-msgs`、`ros-humble-cv-bridge`。编译器为 GNU C++ 11.4.0，OpenCV 4.5.4。沿用 `/opt/rknn-runtime` 的既有 RKNN 2.3.0 构建 SDK，不将它作为开发板 Runtime 2.3.2 的证据。运行产物检查时设置该 SDK 库目录的 `LD_LIBRARY_PATH`，没有覆盖开发板系统库。

容器内在 `/work/ros2_ws` 执行：

```sh
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-clean-cache --cmake-args \
  -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_PLATFORM=rk3576 \
  -DDRONE_TRACKER_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
export LD_LIBRARY_PATH=/opt/rknn-runtime/lib:${LD_LIBRARY_PATH:-}
source install/setup.bash
ctest --test-dir build/drone_tracker --output-on-failure
cmake -S src/drone_tracker/src/platform/rk3576 -B build/camera-ros-integration \
  -DDRONE_TRACKER_CAMERA_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-ros-integration -j4
ctest --test-dir build/camera-ros-integration --output-on-failure
```

初次集成构建通过：ROS/公共测试 12/12，独立相机/公共测试 14/14。最终复测结果另见本文件末尾。完整复测脚本位于 `.docker-artifacts/ros-camera-build.sh`；日志为 `.docker-artifacts/log/ros-camera-build.log` 与 `ros-camera-final-build.log`。首轮 shell 的 `set -u` 与 ROS 环境脚本不兼容，在编译前失败；调整为保留失败退出和管道错误检测后通过。设备测试曾因期望错误文本不匹配失败，修正断言后通过；未修改控制行为来绕过测试。

无设备测试使用系统调用替身和合成图像；ROS 测试使用可控模型及真实公共跟踪算法，包含原视频输入和消息记录回归。没有加载真实模型或提交 NPU 推理。既有镜像没有 clang-tidy/clangd，仓库没有对应命令，静态工具检查缺口保留。

## 6.2 依赖、兼容与安装检查

安装目录应包含 `share/drone_tracker/launch/rk3576_camera.launch.py` 与 `share/drone_tracker/parameters/rk3576_camera_params.yaml`。动态依赖记录保存为 `.docker-artifacts/log/ros-camera-dynamic.txt` 和 `ros-camera-ldd.txt`，最终程序与独立工具指纹见 `ros-camera-binaries.sha256`。

公共头文件新增字段仅使用 C++ 通用数值与字符串，不引入 V4L2/RKNN 类型。默认独立工厂仍拒绝相机，旧连续配置仍有效。原 ROS 视频回归和 Jetson 配置策略测试通过；后者只编译通用配置策略，不链接 Jetson SDK，不代表 Jetson 构建或实机验证。未在 Jetson 硬件执行测试。

## 暂缓与未完成项目

- 1.2：相机每次触发帧数、曝光、最短周期和计数器查询，以及高频原因调查。
- 6.3–6.6：开发板 2 新实现部署、有限频率矩阵、资源负载及连续模式实机回归。
- 7.1–7.5：开发板 2 真实模型配置、两精度 ROS 行为、失败恢复、20 轮启停和额外 30 分钟负载。

上述任务不勾选。历史 shell 单发与连续相机验收不能替代新两线程实现。没有测量 NPU 设备内存，也没有新模型质量或曝光同步结论。


## 最终检查

- 现有 ARM64 临时容器构建成功，ROS/公共测试 **12/12**、独立相机/公共测试 **14/14** 通过。最终 ROS 测试包括补充的两模式图像关闭、姿态查询、在途检测等待及旧轮结果隔离。
- 产物安装检查通过，相机 launch 与参数文件均已安装。`readelf` 和 `ldd` 未发现 CUDA、TensorRT、VPI、Tegra 依赖或缺失库；`libicudata` 是 ICU 国际化数据库，名称中的字符串不是 CUDA 依赖。
- Python 标准 CSV 解析器成功读取有限 0/1/120 命令报告。对应帧数为 0/1/60，驱动序号缺口均为 0，恢复结果均为成功。120 命令情形的数量差为 60，没有混入驱动序号缺口。有限工具超时另有针对性测试和日志 `ros-camera-tool-timeout-test.log`。
- launch Python 语法解析、YAML 解析和完整算法参数对比通过；相机示例的算法默认值与视频示例一致。检查使用 conda base Python，仅解析文件，未运行 launch 或模型。
- `git diff --check` 和 `openspec validate integrate-rk3576-ros-camera --strict` 通过。
- `tasks.md` 进度为 **27/37**。任务 1.2、6.3–6.6、7.1–7.5 共 10 项保持未完成。没有归档或提交代码。
