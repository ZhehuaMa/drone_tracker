## Purpose

使 RK3576 能通过正式 ROS 2 节点，以本地视频驱动既有检测和跟踪算法，并与原有触发、姿态和结果话题协作。明确配置、实时消息时间、多轮模型复用、异常清理与验收边界，使接入正确性能够独立于遗留模型效果问题验证。

## ADDED Requirements

### Requirement: 正式入口使用本地实时 MJPEG 输入

RK3576 的 `drone_tracker_node` SHALL 接受本地 MJPEG 视频文件。MJPEG 表示各帧独立使用 JPEG 编码；输入格式、像素和 PTS 检查 SHALL 遵守 `rk3576-video-frame-source`。节点 SHALL 拒绝相机、网络输入及其他不支持的视频编码。RK3576 ROS 入口 SHALL 要求 `use_history_timestamp=false` 和 `use_sim_time=false`，违反时报告配置错误。该限制 MUST NOT 移除独立视频帧源的历史能力或改变独立顺序回放。

#### Scenario: 合法启动
- **WHEN** 使用完整参数和本地 MJPEG 文件启动 RK3576 节点
- **THEN** 节点完成初始化并等待 ON，不自动播放

#### Scenario: 历史或模拟时间
- **WHEN** 在 RK3576 ROS 配置中将任一时钟选项设为 true
- **THEN** 初始化失败并报告对应参数，不加载可运行的采集管线

#### Scenario: 不支持输入
- **WHEN** 指定 H.264 文件、网络地址或相机设备
- **THEN** 初始化明确失败，不伪装可播放视频

### Requirement: 模型角色和路径必须明确

RK3576 SHALL 要求 `tracker.yolo_engine`、`tracker.nano_template_backbone`、`tracker.nano_search_backbone`、`tracker.nano_head` 四个非空文件路径。Backbone 是图像特征模型，模板与搜索分别使用不同尺寸的输入；四文件仍对应检测、特征提取和匹配三类职责。RK3576 的 `tracker.nano_backbone` SHALL 为空。Jetson SHALL 保留原三路径配置和默认值，新拆分字段 SHALL 为空。

绝对模型路径 SHALL 直接使用。相对模型路径 SHALL 相对于包 share 下的 `models/` 解析。系统 MUST NOT 根据文件名推断角色。混填、缺角色、模型不兼容或目标类别越界 SHALL 在采集前报错，不留下半初始化可运行节点。

#### Scenario: 外部模型目录
- **WHEN** 四个合法 RKNN 文件使用绝对路径放在包目录外
- **THEN** 节点按显式角色加载，不添加 share/models 前缀

#### Scenario: 缺失搜索模型
- **WHEN** 仅提供 YOLO、模板 Backbone 和 Head
- **THEN** 初始化报告搜索角色缺失，不启动采集

#### Scenario: 旧 Jetson 配置
- **WHEN** 使用原有三个相对路径且未指定新增字段
- **THEN** 仍解析到包 models 目录并使用共享 Backbone

### Requirement: 视频内参与启动配置显式提供

RK3576 SHALL 要求 `camera.fx/fy/cx/cy` 全部显式提供且为有限数值。它们是当前视频像素坐标中的焦距和主点，用于旋转补偿及位置预测。fx/fy SHALL 大于零。系统 SHALL 直接使用这组内参，不按相机输出尺寸再次缩放。Jetson SHALL 保留原内参推导方式。

专用 `rk3576_video.launch.py` SHALL 接受 `params_file` 和显式 `video_source`。launch SHALL 只覆盖所指定视频路径，不隐式改变算法参数。RK3576 本阶段启动配置 SHALL 在节点生命周期内固定；运行期更改模型、输入、内参、算法、记录选项或时钟配置 SHALL 明确拒绝，不返回成功却继续使用旧值。

#### Scenario: 原尺寸内参
- **WHEN** 视频为 640×480，配置 fx=800、fy=810、cx=320、cy=240
- **THEN** 跟踪器使用这些像素值，不按 `mv_camera.output_width_px` 重算

#### Scenario: 漏填或非法值
- **WHEN** 缺少 cy，或 fx 为零、非有限值
- **THEN** 初始化报告具体参数错误

#### Scenario: 禁止运行期时钟切换
- **WHEN** 节点初始化后请求 `use_sim_time=true`
- **THEN** 参数更新失败并说明本入口仅支持固定实时系统时钟

### Requirement: 时间与取最新帧保持一致

节点 SHALL 保留帧源的原尺寸灰度像素与系统基准加 PTS 的帧时间。PTS 是视频中帧的显示时刻。节点 SHALL 使用单消费者取最新排队帧。消费落后时 SHALL 允许跳过积压帧，但 MUST NOT 改写保留帧的时间。正在运行的普通取帧超时 SHALL 继续等待或响应停止，不当作 EOF。

节点 SHALL 在当前系统时间上生成预测时刻。帧时刻姿态和当前姿态 SHALL 沿用现有查询行为；无姿态时 SHALL 保留单位旋转矩阵的既有行为。系统 MUST NOT 自动将历史姿态重标为当前姿态。

#### Scenario: 慢处理
- **WHEN** 视频约每 33 ms 供帧而消费者每帧处理 60 ms
- **THEN** 可以跳过排队帧，保留帧的时间仍来自原视频时间轴

#### Scenario: 等待中停止
- **WHEN** 帧源等待十秒后的下一帧且收到 OFF
- **THEN** 节点结束等待并启动清理，无需等满该十秒间隔；正在执行的推理按资源安全规则完成

### Requirement: ROS 接口和结果有效性保持兼容

系统 SHALL 保留输入 `/fpv/gimbal`（`sensor_msgs/msg/Imu`）、`/fpv/odom`（`nav_msgs/msg/Odometry`）、`/fpv/tracker_trigger`（`std_msgs/msg/Header`）。系统 SHALL 保留输出 `/fpv/target_bbox`（`vision_msgs/msg/Detection2D`）、`/fpv/tracker_image`（`sensor_msgs/msg/Image`）、`/fpv/tracker_state`（`std_msgs/msg/Int8`）。既有 QoS（消息队列与可靠性设置） SHALL 保持不变。

被跟踪器消费的帧 SHALL 按既有规则发布检测消息，其时间戳为预测时刻。跟踪有效时检测 `frame_id` SHALL 为 `camera_frame`，否则 SHALL 为空。图像开关开启时 SHALL 发布独立 BGR8 图像，时间戳为原帧时间，绘图 MUST NOT 修改共享灰度帧。未消费的帧 SHALL 保持既有不发布结果行为。

状态 SHALL 保持 `Detecting=0`、`TargetFound=1`、`Tracking=2`、`TargetLost=3`，仅变化时发布。每轮 SHALL 重置上次发布状态，使新轮首次可发布状态不被旧轮抑制。EOF/OFF MUST NOT 被伪装成算法 TargetLost。

#### Scenario: 无目标消息
- **WHEN** 消费了一帧且没有有效跟踪框
- **THEN** 检测消息 frame_id 为空，时间为当前预测时刻，不伪造有效目标

#### Scenario: 图像与预测时间不同
- **WHEN** 原帧时间为 T，预测时刻为 T+20000 微秒，图像输出开启
- **THEN** 图像使用 T，检测框使用 T+20000 微秒，共享帧像素不被绘图改变

#### Scenario: 状态重启
- **WHEN** 第一轮结束前为 Detecting，第二轮首次消费也为 Detecting
- **THEN** 第二轮仍发布一次数值 0，轮内相同状态不反复发布

### Requirement: 多轮启停复用模型并完整收尾

触发消息 `frame_id=ON` SHALL 从视频首帧开始一轮运行，`frame_id=OFF` SHALL 结束当前轮。重复 ON SHALL 不增加工作线程；空闲时 OFF SHALL 安全。普通 EOF SHALL 自动结束本轮，不自动循环。EOF、OFF 或普通输入失败后 SHALL 可以再次 ON。每次新轮 SHALL 使用新的实时基准，并遵守平台的旧检测清理契约。

系统 SHALL 先回收旧消费线程，再启动新线程。清理 SHALL 停止帧源、结束消费和旧检测访问、关闭本轮记录。模型 SHALL 跨轮复用。普通启动失败 SHALL 回滚本轮并报告失败。节点初始化失败 SHALL 非零退出，不留下半初始化节点。最终退出 SHALL 回收消费和检测线程后释放模型。

#### Scenario: EOF 后直接 ON
- **WHEN** 视频正常读完且本轮清理完成，用户再次发送 ON
- **THEN** 旧线程被回收，视频从首帧开始，模型不重新加载

#### Scenario: 启动时文件不可读
- **WHEN** 节点初始化后视频被移走，收到 ON
- **THEN** 本轮启动失败并清理，恢复文件后可再次 ON，不报告成功开始播放

#### Scenario: 推理中 OFF 再 ON
- **WHEN** 旧轮检测尚未完成时收到 OFF，清理完成后收到 ON
- **THEN** 新轮不会获取旧请求或旧结果，旧帧不被设备继续使用后提前释放

### Requirement: 结束原因与尾帧可诊断

节点 SHALL 区分主动停止、正常 EOF、输入失败及记录失败。EOF 判定 SHALL 在没有可消费队列帧后完成，取最新策略仍可跳过旧积压帧。系统 SHALL 防止状态查询与最后入队竞争造成遗漏最后可用帧。错误结束 MUST NOT 被报告为正常 EOF。清理 SHALL 保留最终原因。

#### Scenario: 带尾帧的结尾
- **WHEN** 生产线程已经 EOF，队列仍有最后一帧
- **THEN** 消费者可处理该尾帧，之后结束本轮，不因 isRunning 为 false 提前丢弃

#### Scenario: 损坏输入
- **WHEN** 中间帧发生解码错误
- **THEN** 节点报告视频路径和原因，完成本轮清理并保留失败分类

### Requirement: ROS bag 记录与关闭互斥

ROS bag 是 ROS 消息记录文件。`need_save=true` 时节点 SHALL 记录现有条件下的姿态、里程计、检测框和图像消息，保留原话题名及时间戳。视频输入 SHALL 不生成相机图像录像。记录的创建、写入、关闭和销毁 SHALL 相互同步。OFF、EOF、普通失败和进程退出 SHALL 关闭本轮记录。关闭后到达的回调 MUST NOT 访问已释放 writer。

记录打开、写入或关闭失败 SHALL 明确报告。打开失败 SHALL 阻止本轮启动；写入失败 SHALL 请求结束本轮并安全清理。系统 MUST NOT 覆盖已有记录目录。

#### Scenario: EOF 与姿态回调并发
- **WHEN** 图像处理到 EOF 时同时收到姿态消息
- **THEN** 消息要么在关闭前安全写入，要么因记录已关闭而跳过，不访问释放后的记录器

#### Scenario: 目录不可写或已存在
- **WHEN** ON 需要创建记录但目标不可写或与已有记录碰撞
- **THEN** 报告启动失败并保留已有记录，不开始无声缺失记录的一轮

### Requirement: 接入验收与模型质量分别报告

验收 SHALL 使用可控检测输出和真实公共跟踪逻辑验证捕获、跟踪、丢失恢复及旧轮结果隔离。真实模型验收 SHALL 验证 ROS 输入输出、EOF、重启、主动退出和失败清理，不以检测成功为前置条件。报告 SHALL 区分模型质量、构建、公共测试与实机证据。

开发板 SHALL 对固定纯 FP16 和纯 INT8 NanoTrack 配置分别执行基础集成检查，并记录同一 YOLO、四模型文件和视频身份。固定纯 INT8 配置 SHALL 在同一节点进程复用模型完成至少 20 轮启停，以及额外至少 30 分钟重复播放负载。报告 SHALL 记录实际负载、内存、线程、文件描述符和停止情况；持续增长或停止失败 SHALL 解释并解决，未测设备内存 SHALL 明确标注。Jetson 实机等价性和前序回放 7.2、7.3 MUST NOT 因本次接入完成被标记通过。

#### Scenario: 真实视频一直未捕获
- **WHEN** 真实模型没有捕获视频目标，但输入、无效框发布、正常结束和资源检查符合要求
- **THEN** 可将对应接入检查记为通过，同时保留质量未通过或未评价结论，不降低阈值制造捕获

#### Scenario: 多轮稳定性
- **WHEN** 固定模型在同一进程完成规定轮数和额外负载时长
- **THEN** 报告包含实际帧处理与资源曲线，退出后无残留消费或检测进程，部分执行不计为整项完成
