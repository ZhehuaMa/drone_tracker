# rk3576-ros-video 规格

## Purpose

使 RK3576 能通过正式 ROS 2 节点，以本地视频驱动既有检测和跟踪算法，并与原有触发、姿态和结果话题协作。明确配置、实时消息时间、多轮模型复用、异常清理与验收边界，使接入正确性能够独立于遗留模型效果问题验证。

## Requirements

### Requirement: 正式入口使用本地实时 MJPEG 输入

RK3576 的 `drone_tracker_node` SHALL 接受本地 MJPEG 视频文件。MJPEG 表示各帧独立使用 JPEG 编码；输入格式、像素和 PTS 检查 SHALL 遵守 `rk3576-video-frame-source`。节点 SHALL 拒绝网络输入及其他不支持的视频编码。相机设备请求 SHALL 按 rk3576-ros-camera 处理，不再一律拒绝。RK3576 ROS 入口 SHALL 要求 `use_history_timestamp=false` 和 `use_sim_time=false`，违反时报告配置错误。该限制 MUST NOT 移除独立视频帧源的历史能力或改变独立顺序回放。

#### Scenario: 合法启动
- **WHEN** 使用完整参数和本地 MJPEG 文件启动 RK3576 节点
- **THEN** 节点完成初始化并等待 ON，不自动播放

#### Scenario: 历史或模拟时间
- **WHEN** 在 RK3576 ROS 配置中将任一时钟选项设为 true
- **THEN** 初始化失败并报告对应参数，不加载可运行的采集管线

#### Scenario: 不支持输入
- **WHEN** 指定 H.264 文件或网络地址
- **THEN** 初始化明确失败，不伪装可播放视频

#### Scenario: 相机请求
- **WHEN** 使用完整相机参数选择合法相机设备
- **THEN** 按相机规格初始化并等待 ON，不将设备作为 MJPEG 文件解码

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

A ROS bag is a recording of ROS messages. With `need_save=true`, RK3576 SHALL record active-round messages according to `rk3576-session-topic-recording`, using its frozen application catalog and explicit sampling policies. Its recording inputs SHALL include `/fpv/gimbal`, `/fpv/odom`, and `/mavros/imu/data`. It SHALL record the `/fpv/target_bbox` and `/fpv/target_los` messages actually constructed for publication as a pair. Retained input messages SHALL preserve their resolved topic names, complete payloads, and valid source timestamps. Bbox and LOS SHALL preserve their shared timestamp. Published `/fpv/tracker_image` messages SHALL be eligible for sampled recording under that capability. When no image is admitted, a bag MUST NOT contain image topic metadata. `visualized_result` SHALL remain solely the switch for publishing tracking debug images. Recording MUST NOT enable debug-image publication. Video-file input SHALL NOT create camera-video recordings.

Disk writes SHALL be separate from ROS callbacks and tracking processing. Admission MUST NOT wait for disk writes. Retained recording work SHALL remain bounded by both 256 message slots and 64 MiB of accounted payload, including the writer's in-flight item. Each observation pair SHALL consume two slots and its combined payload allowance. Insufficient capacity SHALL reject the whole pair and increment each member's attempted and dropped counts. It MUST NOT accept only one member. Unexpected queue overflow SHALL mark the session incomplete. Intentional policy sampling SHALL be reported separately from unexpected loss. A mid-write disk failure can still leave only one pair member in the file; the recorder SHALL report failure or incompleteness rather than promise a file transaction.

Creation, admission, writing, closing, and destruction SHALL be synchronized. OFF, EOF, round-ending failures, and normal service exit SHALL close the current session. The authoritative OFF message and final invalid observation pair SHALL be offered before sealing. Callbacks after sealing MUST NOT access a destroyed writer or attach old-session work to a new session. Bag open, write, and close failures SHALL report the session's failure and original reason. Such failures MUST NOT stop otherwise healthy acquisition, tracking, HDMI, or independent camera-video recording. Existing recording directories MUST NOT be overwritten. Each session SHALL report accepted, written, intentionally omitted, unexpectedly dropped, and accepted-but-unwritten totals. Successful policy-complete close SHALL be distinguishable from incomplete, failed, and storage-limited results.

#### Scenario: EOF 与姿态回调并发
- **WHEN** image processing reaches EOF while an attitude callback is submitting recording work
- **THEN** the message is either accepted into the old session before sealing or rejected safely, without accessing a released writer or entering a later session

#### Scenario: 目录不可写或已存在
- **WHEN** ON needs a bag in an unwritable directory or would collide with existing evidence
- **THEN** the session reports an open failure, existing evidence is retained, and a healthy tracking round can still run

#### Scenario: bag 写入失败
- **WHEN** disk writing fails during a round with tracking and independent camera recording enabled
- **THEN** the bag reports the original failure, accepted, written, and unwritten counts; healthy tracking, HDMI, and camera recording continue

#### Scenario: 队列达到容量
- **WHEN** disk writing lags and retained recording work reaches a message or byte limit
- **THEN** submission does not wait for disk progress, unexpected drops are counted, and the session is marked incomplete while tracking and display continue

#### Scenario: 禁用调试图像
- **WHEN** `need_save=true` and `visualized_result=false` for an ON/OFF round that produces target observations
- **THEN** the readable closed bag includes retained observations and other received catalog topics, but contains no tracker_image messages or metadata

#### Scenario: bag 完整性回查
- **WHEN** a session closes after OFF or EOF without detected unexpected losses or write errors
- **THEN** a reader can reach the end of every split file, its message counts equal successful writes, retained timestamps and recognizable payloads match their inputs, and intentional sampling is explicitly reported

#### Scenario: 开启图像也不写入 bag
- **WHEN** `need_save=true`, `visualized_result=true`, and ON processing publishes images and receives body IMU
- **THEN** the bag can contain policy-selected original images and retained IMU, bbox, and LOS messages; recording changes neither image publication nor the shared observation reference time

#### Scenario: 仅余一个消息位置
- **WHEN** the available admission class has one remaining message slot and a bbox/LOS pair is offered
- **THEN** both members are rejected, both drop counts increase, and the queue remains within its limits

#### Scenario: 无记录请求
- **WHEN** `need_save=false`, body IMU arrives, and target observations are produced
- **THEN** observations retain their normal behavior, no bag is created, and `recording.enabled` independently controls original-camera video recording

### Requirement: 接入验收与模型质量分别报告

验收 SHALL 使用可控检测输出和真实公共跟踪逻辑验证捕获、跟踪、丢失恢复及旧轮结果隔离。真实模型验收 SHALL 验证 ROS 输入输出、EOF、重启、主动退出和失败清理，不以检测成功为前置条件。报告 SHALL 区分模型质量、构建、公共测试与实机证据。

开发板 SHALL 对固定纯 FP16 和纯 INT8 NanoTrack 配置分别执行基础集成检查，并记录同一 YOLO、四模型文件和视频身份。固定纯 INT8 配置 SHALL 在同一节点进程复用模型完成至少 20 轮启停，以及额外至少 30 分钟重复播放负载。报告 SHALL 记录实际负载、内存、线程、文件描述符和停止情况；持续增长或停止失败 SHALL 解释并解决，未测设备内存 SHALL 明确标注。Jetson 实机等价性和前序回放 7.2、7.3 MUST NOT 因本次接入完成被标记通过。

#### Scenario: 真实视频一直未捕获
- **WHEN** 真实模型没有捕获视频目标，但输入、无效框发布、正常结束和资源检查符合要求
- **THEN** 可将对应接入检查记为通过，同时保留质量未通过或未评价结论，不降低阈值制造捕获

#### Scenario: 多轮稳定性
- **WHEN** 固定模型在同一进程完成规定轮数和额外负载时长
- **THEN** 报告包含实际帧处理与资源曲线，退出后无残留消费或检测进程，部分执行不计为整项完成
