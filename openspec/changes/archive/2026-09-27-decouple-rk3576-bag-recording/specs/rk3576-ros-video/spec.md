# Spec Delta

## MODIFIED Requirements

### Requirement: ROS bag 记录与关闭互斥

ROS bag 是 ROS 消息记录文件。`need_save=true` 时节点 SHALL 记录本轮实际接收的姿态、里程计，以及实际生成的检测框消息，保留原话题名、消息内容和时间戳。仅在 `visualized_result=true` 且实际生成图像消息时，节点 SHALL 记录 `/fpv/tracker_image`。`visualized_result=false` 时，bag MUST NOT 包含该话题的消息或话题元数据。`visualized_result` 是现有参数名。视频输入 SHALL 不生成相机图像录像。记录写盘 SHALL 与 ROS 消息回调及跟踪处理分离；提交过程 MUST NOT 等待磁盘写入。待写消息缓冲 SHALL 同时受消息数和总字节数限制。队列溢出 SHALL 计数并将该轮 bag 标记为不完整，不得悄悄宣称完整。记录的创建、写入、关闭和销毁 SHALL 相互同步。OFF、EOF、普通失败和进程退出 SHALL 关闭本轮记录。关闭后到达的回调 MUST NOT 访问已释放 writer。

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
