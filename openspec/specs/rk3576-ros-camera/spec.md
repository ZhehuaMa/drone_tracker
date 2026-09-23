# rk3576-ros-camera 规格

## Purpose

使 RK3576 正式 ROS 节点能够通过原有 ON/OFF 消息控制真实相机，并在周期软件触发或连续采集下运行既有检测跟踪算法。明确参数、图像时间、线程收尾及实机证据边界，使接入稳定性与模型识别质量分别可验证。

## Requirements

### Requirement: 相机入口和配置显式且固定

正式节点 SHALL 接受 video 加一个或多个十进制数字的相机简写，以及绝对 /dev 设备路径，并核验实际设备能力。专用 rk3576_camera.launch.py SHALL 接受 params_file 和显式 video_source，仅覆盖输入路径。相机 SHALL 显式配置传感器型号（`sensor_model`，取值 `ar0234` 或 `sc233hgs`，未显式设置时默认 `ar0234`）、媒体设备、传感器实体、尺寸、设备帧率及 capture_mode。capture_mode SHALL 为 continuous 或 software_trigger。软件模式 SHALL 提供范围 1 至设备 fps 的有限 trigger_rate_hz，连续模式 SHALL 为零。当 `sensor_model` 为 `sc233hgs` 时，capture_mode SHALL 只能为 continuous；配置为 software_trigger SHALL 在初始化阶段明确拒绝并说明该型号不支持软件触发，不得静默按连续模式启动。参数前缀 SHALL 为 mv_camera，型号、媒体、传感器及缓冲字段分别为 sensor_model、media_device、sensor_entity、buffer_count。

相机 SHALL 要求 use_history_timestamp=false、use_sim_time=false、need_save=false。模型路径 SHALL 遵守现有四文件角色规则。camera.fx/fy/cx/cy SHALL 全部显式且有限，fx/fy SHALL 为正；它们表示实际图像像素坐标中的焦距和主点，MUST NOT 再按输出尺寸缩放。运行期修改输入、模式、频率、内参、模型、算法、时钟、记录及传感器型号配置 SHALL 明确拒绝。

#### Scenario: 合法软件模式
- **WHEN** 配置 `sensor_model=ar0234`、1920×1200、设备 fps=60、software_trigger、trigger_rate_hz=20，以及合法模型与内参
- **THEN** 初始化后等待 ON，不自动拍摄，不把触发频率当作设备 fps

#### Scenario: 多位设备号
- **WHEN** video_source=video10 或 /dev/video10 且设备满足其余配置
- **THEN** 按相机设备处理，不作为视频文件打开

#### Scenario: 标定前使用模拟内参
- **WHEN** 1920×1200 图像显式配置用户确认的模拟内参 fx=fy=1000.0、cx=960.0、cy=600.0
- **THEN** 系统 SHALL 允许继续接入测试；配置与验收记录 SHALL 标明模拟值来源，MUST NOT 将结果作为实测标定或旋转补偿精度验收；正式标定后 SHALL 替换四值并重启节点

#### Scenario: 记录请求
- **WHEN** 相机配置 need_save=true
- **THEN** 初始化失败且说明首版不支持记录，不开始拍摄，不创建部分 ROS bag

#### Scenario: 运行期改频率
- **WHEN** 已初始化的节点收到修改 mv_camera.trigger_rate_hz 的参数请求
- **THEN** 返回失败并说明需重启节点应用配置

#### Scenario: 合法 SC233HGS 连续配置
- **WHEN** 配置 `sensor_model=sc233hgs`、capture_mode=continuous、trigger_rate_hz=0，以及合法模型与内参
- **THEN** 初始化后等待 ON，不发送任何触发命令，不要求提供触发相关参数

#### Scenario: SC233HGS 配置软件触发被拒绝
- **WHEN** 配置 `sensor_model=sc233hgs` 且 capture_mode=software_trigger
- **THEN** 初始化明确失败，说明该型号不支持软件触发，不开始采集，也不静默改为连续模式

#### Scenario: 运行期改传感器型号
- **WHEN** 已初始化的节点收到修改 `mv_camera.sensor_model` 的参数请求
- **THEN** 返回失败并说明需重启节点应用配置

### Requirement: ON 和 OFF 控制完整运行轮次

ON SHALL 启动整轮采集与跟踪，MUST NOT 解释为单次拍照命令。软件模式 SHALL 在采集就绪后开始周期触发。连续模式 SHALL 不发送触发命令。OFF SHALL 停止后续触发并结束本轮，重复 ON 不增加工作线程，空闲 OFF 安全。启动失败 SHALL 回滚已有资源。首帧超时 SHALL 遵守相机帧源契约。

新轮 SHALL 清空旧队列、统计和跟踪状态，建立新时间基准。模型 SHALL 跨轮复用。收尾 SHALL 清除旧检测请求和结果，并等待在途模型访问结束。相机不报告文件 EOF；普通设备失败后的下一次 ON SHALL 重新打开核验设备。

SC233HGS 每轮 SHALL 遵守 `rk3576-sc233hgs-camera-frame-source` 的有界启动预热。预热是交付图像前的时间递增观察期。预热观察帧 MUST NOT 提交算法，也 MUST NOT 产生对应的检测框或图像消息。预热阶段 SHALL 能响应 OFF，不等待预热期限结束。预热未稳定或致命元数据错误 SHALL 作为相机失败结束本轮，MUST NOT 伪装成算法 TargetLost。预热 SHALL 只增加内部诊断阶段，不改变四个 TrackerState 数值或六个业务话题。AR0234、Jetson 与视频 SHALL 保持原启停和时间语义。

#### Scenario: 首次启动失败后重试
- **WHEN** 首次 ON 时设备不可用，设备恢复后再次 ON
- **THEN** 首轮无残留工作线程，第二轮重新打开设备并使用新轮状态

#### Scenario: 检测中停止再启动
- **WHEN** 旧轮 YOLO 尚在推理时收到 OFF，随后清理完成并再次 ON
- **THEN** 旧轮帧安全存活到访问结束，新轮不获取旧结果，模型无需重新加载

#### Scenario: 预热中 OFF
- **WHEN** SC233HGS 已开流但尚未输出合格帧时收到 OFF
- **THEN** 及时停止并回收本轮，不发布观察帧对应的框或图像，不因没有首帧误报预热失败

#### Scenario: 预热失败后新轮
- **WHEN** 一轮因时间重复未能在界限内稳定，清理后再次收到 ON
- **THEN** 新轮重新预热，旧轮无帧或失败状态不占用新轮时间基准，不自动发送 TargetLost

### Requirement: 图像与预测消息保持原有语义

系统 SHALL 保留输入 /fpv/gimbal（sensor_msgs/msg/Imu）、/fpv/odom（nav_msgs/msg/Odometry）、/fpv/tracker_trigger（std_msgs/msg/Header）。系统 SHALL 保留输出 /fpv/target_bbox（vision_msgs/msg/Detection2D）、/fpv/tracker_image（sensor_msgs/msg/Image）、/fpv/tracker_state（std_msgs/msg/Int8）。QoS，即消息队列与可靠性设置，SHALL 保持不变。

节点 SHALL 取最新排队帧，保留其原尺寸像素与帧源时间。图像开启时 SHALL 发布独立 BGR8 图像，时间为原帧时间，绘图不修改共享灰度帧。框 SHALL 使用当前预测时刻，跟踪有效时 frame_id 为 camera_frame，否则为空。未消费帧 SHALL 保持既有不发布结果行为。姿态查询和缺姿态时的单位旋转 SHALL 保持现有规则。

状态 SHALL 保持 Detecting=0、TargetFound=1、Tracking=2、TargetLost=3，仅变化时发布，每轮首次状态仍可发布。OFF 和设备失败 MUST NOT 被伪装为算法 TargetLost。没有曝光证据时 MUST NOT 宣称图像时间代表曝光时刻。

#### Scenario: 帧时间与当前时间不同
- **WHEN** 保留帧时间为 T、预测时间为 T+20000 微秒且图像输出开启
- **THEN** 图像使用 T，框使用 T+20000，姿态按已有查询规则处理，不把图像改标为预测时间

#### Scenario: 无目标新轮
- **WHEN** 两轮首次消费都处于 Detecting 且没有有效目标
- **THEN** 每轮都可发布状态 0，框 frame_id 为空，不为验收伪造捕获

### Requirement: 每轮故障和采集量可诊断

节点 SHALL 报告相机终止原因、模式、请求/实际触发频率、调用错误、周期跳过、收到/入队/丢弃/消费帧数和时间来源。常规统计 SHALL 每秒最多汇总一次，结束时 SHALL 保存最终统计到输出日志，错误 SHALL 及时报告。相机停止耗时与整个节点停止耗时 SHALL 分别报告，模型推理等待不得隐藏。

SC233HGS 诊断 SHALL 增加预热阶段、阈值、耗时、检查量、丢弃量、重复次数、最终连续计数和首个合格帧身份。收到的合格帧与全部成功出队 SHALL 使用不同计数。预热丢弃、终止处置、驱动序号缺口和队列满丢弃 SHALL 分别报告。未交付首帧时 SHALL 标记首帧时间未知，不输出伪造的零延迟。预热失败 SHALL 区分无帧超时、未稳定时间超限、检查量超限及致命元数据错误。

#### Scenario: 推理慢于拍摄
- **WHEN** 采集持续而模型消费落后并出现队列满
- **THEN** 分别报告采集、队列丢弃与消费数量，不自动降低触发频率，不把队列丢弃称为相机拒绝触发

#### Scenario: 正常启动丢弃观察帧
- **WHEN** 一轮出队 11 帧后完成预热，其中 10 帧被观察丢弃且没有驱动缺帧
- **THEN** 报告预热丢弃 10、合格帧 1、出队总量 11，不把预热丢弃计为驱动或队列丢帧

### Requirement: ROS 实机验收不替代模型质量

开发板 2 SHALL 对同一 YOLO 和纯 FP16、纯 INT8 两组 NanoTrack 模型分别完成软件及连续模式基础 ROS 检查。FP16/INT8 分别指 16 位浮点和八位整数；每组模板、搜索和 Head 文件采用同一精度。报告 SHALL 记录程序、模型、配置的文件指纹、设备身份及 Runtime/驱动版本。

固定 INT8 软件模式 SHALL 在同一节点进程完成至少 20 轮 ON/OFF，并额外至少 30 分钟持续负载。验收 SHALL 覆盖重复 ON/OFF、启动失败后恢复、采集失败、主动退出、图像开关、无目标消息与旧轮隔离。资源报告 SHALL 包含主机内存、线程和文件描述符；持续增长或停止失败 SHALL 解释并解决。相机实机停止 SHALL 在 1 秒内完成；节点整体停止 SHALL 单独测量。退出后 SHALL 核对设备占用和控制恢复。未测 NPU 设备内存 SHALL 明确标注。

系统 SHALL 通过可控模型与真实公共算法验证状态转换和旧轮隔离。真实模型未捕获 SHALL 单独报告，MUST NOT 用降低阈值、改变类别或人工首框替代。前序回放 7.2、7.3 和 Jetson 等价性 MUST NOT 因本变更完成而标记通过。

#### Scenario: 输入稳定但没有识别目标
- **WHEN** 真实相机正确供帧、ROS 输出和资源检查通过，但真实模型一直未捕获
- **THEN** 对应接入检查可以通过，报告保留模型质量未通过或未评价结论

#### Scenario: 只做短测
- **WHEN** 20 Hz 完成有限触发检查，但尚未完成同进程启停和持续负载
- **THEN** 保留对应验收任务未完成，不以历史 shell 测试替代新实现负载
