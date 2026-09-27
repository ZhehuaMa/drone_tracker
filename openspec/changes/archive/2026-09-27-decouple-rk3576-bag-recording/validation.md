# RK3576 bag 与独立录像解耦实施验证

## 范围和状态

本记录对应 `decouple-rk3576-bag-recording`。ROS bag 是按话题保存 ROS 消息的目录；独立录像是 SC233HGS 相机生成原画面视频的支路。源码、无设备测试、生产板成对帧率对照及生产 bag 只读回查均已完成。四份增量规格已同步到主规格，变更已于 2026-09-27 归档。Jetson 实机未验证。

## 实现与规格对照

| 规格 | 实现与证据 |
| --- | --- |
| `platform-factory` | RK3576 的 `need_save` 仅建立节点 bag；`configureFrameSourceRecording` 仅在 Jetson 路径调用。可控相机测试确认初始化等待 ON，帧源未收到旧录像请求。Jetson 配置目标已在 RK3576 容器内编译和测试，但未在 Jetson 实机运行。 |
| `rk3576-camera-recording` | RK3576 不支持的输入及无效录像参数不阻止节点初始化。SC233HGS 录像会话保留本轮失败原因，相机启动继续；相机专属测试已覆盖无效码率。 |
| `rk3576-ros-video` | 每轮 bag 由单线程独占写入器。队列上限为 256 条和 64 MiB；容量拒收留下按话题计数及不完整状态。测试覆盖打开、写入、关闭故障、重复 ON/OFF、EOF 与迟到回调、原时间戳、可识别消息载荷以及禁用图像话题时的元数据。 |
| `rk3576-ros-camera` | 可控 SC233HGS 测试同时启用 `need_save` 与 `recording.enabled`；bag 写入故障时跟踪、模拟 HDMI 新视频帧和独立录像状态继续。生产板已完成同板成对帧率及丢帧测量，见下节。 |

## 构建和无设备测试

- 平台：本机 Docker ARM64 镜像 `drone-tracker-rk3576:humble-arm64-rknn2.3.2`。镜像使用 RKNN Runtime 2.3.2，匹配生产板系统库版本；容器不提供生产板相机、NPU 驱动或 HDMI 设备。
- 命令：仓库根目录执行 `./build.sh --test`。最后一次完整运行构建成功，CTest 为 43/43 通过，包含 `ros_video_tests`、`sc233hgs_camera_tests`、`jetson_config_tests` 与 OSD 相关测试。
- 日志：`.docker-artifacts/log/build-console.log` 和 `.docker-artifacts/log/test-console.log`。这些是本地构建产物，不提交版本库。
- 规格检查：`openspec validate decouple-rk3576-bag-recording --strict` 通过；`git -c core.fsmonitor=false diff --check` 通过。
- 容器未提供 `clang-tidy` 或 `clangd`，未执行这两项静态检查。独立录像的存储与进程测试需要已有 ext4 测试目录，未在本机容器运行；本次已执行适用的 SC233HGS 相机测试。

## 生产板同板成对对照

2026-09-27 在主机名 `hzhy` 的 **HZ-RK3576_WuKong Board** 上执行。内核为 `6.1.141-rt36`。输入为 `/dev/video22` 上的 SC233HGS，实查格式为 1920×1200 NV12。两轮使用相同场景、模型、CPU 绑定和 HDMI 配置；`recording.enabled=true`、`osd.enabled=true`、`visualized_result=false` 均保持不变，仅切换 `need_save`。模型身份如下，SHA-256 是文件内容指纹：

| 模型 | SHA-256 |
| --- | --- |
| YOLO `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| NanoTrack template backbone | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| NanoTrack search backbone | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| NanoTrack head | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

bag 存储位于板载 eMMC `/dev/mmcblk0p7` 的本地 ext4 根文件系统，UUID 为 `61b70882-2cc4-46c3-a234-e2becad78040`。试验前可用空间约 29 GiB。系统 RKNN Runtime 文件 `/usr/lib/librknnrt.so` 的 SHA-256 在试验前后均为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。

两轮 ON 间隔各约 62.4 秒；含启动和收尾的整次试验使用单调时钟计为 **132.432 秒**，低于 180 秒上限。为避开开流预热，从每轮约第 5 秒的第一条性能快照到第 60 秒前的最后一条快照测量。`consumed` 是跟踪实际消费帧数；`hdmi_displayed` 是显示线程确认的新视频帧数，均按同一轮的单调时钟差计算。叠加层刷新 `commits` 未计入视频帧率。

| `need_save` | 起点微秒：消费/显示 | 终点微秒：消费/显示 | 窗口秒数 | 消费增量与帧率 | HDMI 新帧增量与帧率 |
| --- | --- | --- | ---: | ---: | ---: |
| false | 5,449,496：299/297 | 59,032,247：3515/3512 | 53.582751 | 3216；60.0193 帧/秒 | 3215；60.0007 帧/秒 |
| true | 5,479,621：299/297 | 59,950,898：3568/3567 | 54.471277 | 3269；60.0133 帧/秒 | 3270；60.0316 帧/秒 |

开启 bag 相对关闭 bag：跟踪消费帧率变化 **-0.0060 帧/秒（-0.0101%）**；HDMI 实际新视频帧率变化 **+0.0310 帧/秒（+0.0516%）**。这些是一次同板短时实测差值，不是通用性能保证或预设门槛。

同一测量窗口内，相机 `read/enqueued` 增量分别为 3216/3216（bag 关）和 3270/3270（bag 开）；相机队列丢弃与序列缺口均为零。HDMI `replaced/rejected` 窗口增量两轮均为 0/0。整轮终态相机 `read/enqueued/queue_dropped/consumed` 为 3602/3602/0/3598（bag 关）及 3595/3595/0/3589（bag 开）。整轮 HDMI 因开流和收尾分别出现 1 次替换加 1 次拒绝（bag 关）、0 次替换加 1 次拒绝（bag 开）；这些发生在选定测量窗口之外。bag 队列丢弃为零，峰值估算暂存字节数为 1008。独立相机录像两轮均以 `finished` 收尾，分别接受并写入 3602 帧和 3595 帧，录像队列丢弃均为零。首次 OFF 的节点收尾耗时分别为 41,751 微秒（bag 关）与 76,960 微秒（bag 开）；其中包含相机停止、模型等待和 bag 收尾，不把它解释为文件系统调用的严格上限。

原始证据在板端 `/home/hzhy/bag-decouple-20260927/` 的 `pair-console.log`、`bag-off.log`、`bag-on.log`；本机副本位于 `.docker-artifacts/production-bag-20260927/`。测试程序与原生产发布目录分开运行。试验前临时停止 `drone-tracker` 服务，结束后自动恢复；复核服务为 active、enabled，原发布指向仍为 `/opt/drone_tracker/releases/bundle-20260927T023306Z-3034`，相机配置与备份逐字节一致。

## 生产 bag 只读完整性回查

第二轮正常关闭的 bag 为 `/var/lib/drone_tracker/recordings/bag-decouple-20260927/drone_rosbag_1790480923_67719069`。使用板端 `rosbag2_py.SequentialReader` 以只读模式读取到文件末尾，逐条反序列化并核对消息头原时间戳与 bag 时间戳一致。`rosbag2_py` 是 ROS 2 提供的 Python bag 读取接口。本轮状态为 `complete`：接受 9618 条，成功写入 9618 条，丢弃 0 条，失败原因为空。

| 话题 | 元数据类型 | 读回条数 | 节点接受/写入/丢弃 |
| --- | --- | ---: | ---: |
| `/fpv/gimbal` | `sensor_msgs/msg/Imu` | 6030 | 6030/6030/0 |
| `/fpv/target_bbox` | `vision_msgs/msg/Detection2D` | 3588 | 3588/3588/0 |
| `/fpv/odom` | `nav_msgs/msg/Odometry` | 0 | 0/0/0 |
| `/fpv/tracker_image` | **元数据不存在** | **0** | 0/0/0 |

姿态时间戳范围为 `1790480923174271731` 至 `1790480983465858079` 纳秒；检测框时间戳范围为 `1790480923585765000` 至 `1790480983436691000` 纳秒。首条检测框可解析，`frame_id` 为空、宽高为零，符合 Detecting 阶段的无效框语义。回查脚本还逐条检查时间戳、检测框宽高非负，并断言图像话题在消息和元数据中都不存在。板端结果为 `bag-inspection.json`，本机保留副本。`/fpv/odom` 本次没有上游消息，因此不能由这次实机试验证明其非空载荷；无设备测试已覆盖该话题的写入与读回。

## 未执行的 Jetson 检查

- 本机没有 Jetson 硬件和 SDK。Jetson 实机编译、采集及 ROS bag 行为检查未执行，不能以容器中的 Jetson 配置测试代替。

## 发布与回退边界

本次没有切换 `/opt/drone_tracker/current` 或修改 `/opt/drone_tracker/config/camera.yaml`。新节点和录像工作进程仅放在 `/home/hzhy/bag-decouple-20260927/` 用于对照；原服务已恢复。bag 与独立录像证据留在 `/var/lib/drone_tracker/recordings/`，未覆盖或删除旧目录。生产板短时运行与 bag 完整性已通过；长期运行、目标真值精度及 Jetson 实机等价性仍不属于本次结论。
