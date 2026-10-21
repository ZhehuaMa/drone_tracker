## MODIFIED Requirements

### Requirement: 时间与取最新帧保持一致

节点 SHALL 保留帧源的原尺寸灰度像素与系统基准加 PTS 的帧时间。PTS 是视频中帧的显示时刻。节点 SHALL 使用单消费者取最新排队帧。消费落后时 SHALL 允许跳过积压帧，但 MUST NOT 改写保留帧的时间。正在运行的普通取帧超时 SHALL 继续等待或响应停止，不当作 EOF。

观测参考时刻是消息所描述的采集或合格云台姿态时刻。节点 SHALL 按 `target-observations` 选择该时刻及匹配旋转，默认采用采集时刻。开启预测但缺少合格姿态时 SHALL 回退采集参考。帧时刻姿态 SHALL 保留最近邻查询；无姿态时 KF 保留单位旋转矩阵回退，但 LOS SHALL 无效。系统 MUST NOT 自动将历史姿态重标为当前姿态。

#### Scenario: 慢处理
- **WHEN** 视频约每 33 ms 供帧而消费者每帧处理 60 ms
- **THEN** 可以跳过排队帧，保留帧的时间仍来自原视频时间轴

#### Scenario: 等待中停止
- **WHEN** 帧源等待十秒后的下一帧且收到 OFF
- **THEN** 节点结束等待并启动清理，无需等满该十秒间隔；正在执行的推理按资源安全规则完成

#### Scenario: 默认观测不使用处理时间
- **WHEN** 视频帧时间为 10.000 秒、处理开始为 10.050 秒，且 publish_prediction=false
- **THEN** 框与 LOS 使用 10.000 秒，实际发布调用时间另行记录，视频帧时间不被改写

### Requirement: ROS 接口和结果有效性保持兼容

系统 SHALL 保留输入 `/fpv/gimbal`（`sensor_msgs/msg/Imu`）、`/fpv/odom`（`nav_msgs/msg/Odometry`）、`/fpv/tracker_trigger`（`std_msgs/msg/Header`）。系统 SHALL 保留输出 `/fpv/target_bbox`（`vision_msgs/msg/Detection2D`）、`/fpv/tracker_image`（`sensor_msgs/msg/Image`）、`/fpv/tracker_state`（`std_msgs/msg/Int8`）。系统 SHALL 新增 `/fpv/target_los`（`geometry_msgs/msg/TwistStamped`）输出与仅用于记录的 `/mavros/imu/data`（`sensor_msgs/msg/Imu`）输入。QoS 是消息队列与可靠性设置；这些端点及 gimbal、tracker_image SHALL 按 `target-observations` 配置，其他端点保持既有设置。

被跟踪器消费的帧 SHALL 按 `target-observations` 的共同参考时间、浮点精度、有效性和重复抑制规则发布框与 LOS。检测有效时 `frame_id` SHALL 为 `camera_frame`，否则 SHALL 为空。有效框缺少合法姿态时 SHALL 仅使 LOS 无效。图像开关开启时 SHALL 发布独立 BGR8 图像，时间戳为原帧时间，绘图 MUST NOT 修改共享灰度帧。未消费的帧 SHALL 保持既有不发布帧结果行为。显式 OFF 失效通知 SHALL 按 `target-observations` 单独处理，不伪装为消费帧。观测重复抑制 MUST NOT 自动抑制图像或状态变化。

状态 SHALL 保持 `Detecting=0`、`TargetFound=1`、`Tracking=2`、`TargetLost=3`，仅变化时发布。每轮 SHALL 重置上次发布状态，使新轮首次可发布状态不被旧轮抑制。EOF/OFF MUST NOT 被伪装成算法 TargetLost。

#### Scenario: 无目标消息
- **WHEN** 消费了一帧且没有有效跟踪框
- **THEN** 检测与 LOS 的 frame_id 均为空，时间为共同观测参考时刻，不伪造有效目标

#### Scenario: 图像与预测时间不同
- **WHEN** 原帧时间为 T，publish_prediction=true 且合格姿态参考为 T+20000 微秒，图像输出开启
- **THEN** 图像使用 T，检测框与 LOS 使用 T+20000 微秒，共享帧像素不被绘图改变

#### Scenario: 状态重启
- **WHEN** 第一轮结束前为 Detecting，第二轮首次消费也为 Detecting
- **THEN** 第二轮仍发布一次数值 0，轮内相同状态不反复发布

#### Scenario: 文件不会提前预览
- **WHEN** 视频节点初始化成功且 visualized_result=true，但尚未 ON
- **THEN** 文件不提前播放，不产生 ROS 图像、框或 LOS；本变更没有 enable_preview 配置

### Requirement: ROS bag 记录与关闭互斥

ROS bag 是 ROS 消息记录文件。`need_save=true` 时节点 SHALL 记录本轮实际接收的 `/fpv/gimbal`、`/fpv/odom`、`/mavros/imu/data` 及实际构造用于发布的 `/fpv/target_bbox`、`/fpv/target_los`。输入消息 SHALL 保留原话题、完整载荷与 header 时间。框与 LOS SHALL 保留共同时间。所有 visualized_result 设置下，bag MUST NOT 包含 `/fpv/tracker_image` 消息或话题元数据。`visualized_result` SHALL 只控制跟踪调试图像发布。视频输入 SHALL 不生成相机图像录像。记录写盘 SHALL 与 ROS 消息回调及跟踪处理分离；提交过程 MUST NOT 等待磁盘写入。待写消息缓冲 SHALL 同时受消息数和总字节数限制。队列 SHALL 保持 256 条消息与 64 MiB 两个容量上限。每对观测 SHALL 消耗两条额度并按合计字节检查容量。容量不足时 SHALL 整对拒绝并分别计数 attempted/dropped，不能只接纳半对。队列溢出 SHALL 将该轮 bag 标记为不完整，不得悄悄宣称完整。磁盘中途故障仍可能写出半对，SHALL 明确报告失败或不完整，而非承诺文件事务。记录的创建、写入、关闭和销毁 SHALL 相互同步。OFF、EOF、普通失败和进程退出 SHALL 关闭本轮记录。关闭后到达的回调 MUST NOT 访问已释放 writer。

记录打开、写入或关闭失败 SHALL 明确报告该轮 bag 失败，并保留失败原因。故障 MUST NOT 阻止健康的采集、跟踪、HDMI 显示或独立相机录像继续运行。系统 MUST NOT 覆盖已有记录目录。每轮 SHALL 分别报告 bag 接受、成功写入和丢弃的消息数量，并区分正常完整关闭与失败或不完整关闭。

#### Scenario: EOF 与姿态回调并发
- **WHEN** 图像处理到 EOF 时同时收到姿态消息
- **THEN** 消息要么在关闭前安全写入，要么因记录已关闭而跳过，不访问释放后的记录器

#### Scenario: 目录不可写或已存在
- **WHEN** ON 需要创建记录但目标不可写或与已有记录碰撞
- **THEN** 报告本轮 bag 打开失败并保留已有记录；跟踪轮次仍启动，且不会把未创建的 bag 报告为完整

#### Scenario: bag 写入失败
- **WHEN** 已开始跟踪和相机录像的一轮在写入 bag 时发生磁盘错误
- **THEN** 节点报告 bag 失败及已接受、已写入和未写入数量，继续健康的跟踪、HDMI 显示及相机录像

#### Scenario: 队列达到容量
- **WHEN** bag 写入落后且待写队列达到消息数或字节数上限
- **THEN** 消息提交不等待磁盘；节点计数丢弃并标记本轮 bag 不完整，跟踪和 HDMI 显示继续

#### Scenario: 禁用调试图像
- **WHEN** 配置 `need_save=true`、`visualized_result=false`，收到 ON 并产生检测框，随后收到 OFF
- **THEN** 关闭后的 bag 可读取，包含已记录的检测框，但不包含 `/fpv/tracker_image` 消息或该话题的元数据

#### Scenario: bag 完整性回查
- **WHEN** `need_save=true` 的正常轮次已收到 OFF 或视频 EOF，且没有记录丢弃或写入失败
- **THEN** 关闭后的 bag 可重新打开并读到末尾；各话题的消息数与本轮成功接受数相符，时间戳和可识别载荷与原消息相符，报告标记完整

#### Scenario: 开启图像也不写入 bag
- **WHEN** need_save=true、visualized_result=true，ON 期间实际发布调试图像并收到 MAVROS IMU，随后 OFF
- **THEN** bag 可包含原始 IMU、框与 LOS，但没有 tracker_image 数据或元数据；实时图像发布不受这一记录策略禁止

#### Scenario: 仅余一个消息位置
- **WHEN** 未封存的记录队列只剩一条消息额度，尝试接纳一对 bbox/LOS
- **THEN** 两条均拒绝并分别增加丢弃计数，轮次标为不完整，队列不越限

#### Scenario: 无记录请求
- **WHEN** need_save=false 且收到 MAVROS IMU、生成目标框与 LOS
- **THEN** 正常观测仍可发布，但不创建 bag；独立 recording.enabled 仍只控制原画面录像
