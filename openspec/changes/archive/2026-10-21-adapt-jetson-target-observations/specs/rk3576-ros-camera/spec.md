## MODIFIED Requirements

### Requirement: ON 和 OFF 控制完整运行轮次

非持续预览模式的 ON SHALL 启动整轮采集与跟踪，MUST NOT 解释为单次拍照命令。软件模式 SHALL 在采集就绪后开始周期触发。连续模式 SHALL 不发送触发命令。OFF SHALL 停止后续触发并结束本轮，重复 ON 不增加工作线程，空闲 OFF 安全。启动失败 SHALL 回滚已有资源。首帧超时 SHALL 遵守相机帧源契约。

非持续预览模式的新轮 SHALL 清空旧队列、统计和跟踪状态，建立新时间基准。模型 SHALL 跨轮复用。收尾 SHALL 清除旧检测请求和结果，并等待在途模型访问结束。相机不报告文件 EOF；普通设备失败后的下一次 ON SHALL 重新打开核验设备。

非持续预览模式 SC233HGS 每轮 SHALL 遵守 `rk3576-sc233hgs-camera-frame-source` 的有界启动预热。预热是交付图像前的时间递增观察期。预热观察帧 MUST NOT 提交算法，也 MUST NOT 产生对应的检测框或图像消息。预热阶段 SHALL 能响应 OFF，不等待预热期限结束。预热未稳定或致命元数据错误 SHALL 作为相机失败结束本轮，MUST NOT 伪装成算法 TargetLost。预热 SHALL 只增加内部诊断阶段，不改变四个 TrackerState 数值或既有六个业务话题的名称与类型。Jetson SHALL 保持原有行为。RK3576 AR0234、SC233HGS 与视频 SHALL 保持各自启停规则，新增观测时间按 `target-observations` 执行。

非持续预览模式启用录像时，每次 ON SHALL 尝试建立独立录像会话，从相机完成归还、停止及预热期限最终检查后的合格帧边界开始录制，包括没有目标的期间。OFF SHALL 停止录像接收并结束该会话，不继续采集。重复 ON MUST NOT 创建第二个录像会话。录像专属配置无效或不受支持、录像资源故障或运行故障 SHALL 只使该轮录像失败并报告，MUST NOT 阻止节点初始化或停止仍健康的采集、跟踪或 OSD。其他相机、模型及跟踪配置仍按原规则校验。相机/跟踪本身故障 SHALL 按原规则结束整轮。录像收尾 SHALL 有有限等待预算并与相机停止、模型等待分别报告；超时 MUST NOT 无限阻塞 ON/OFF。

非持续预览模式录像准备与相机预热 SHALL 使用独立起点和预算；录像准备耗时 MUST NOT 缩短开流后的预热期限。预热中 OFF 或失败 SHALL 按 `rk3576-camera-recording` 保存零帧取消或失败结果，不伪造视频。录像时间起点及基于视频输入的健康检查 SHALL 从合格帧边界建立，MUST NOT 把等待预热误报为编码或持久化停滞。

持续预览模式 ON SHALL 建立跟踪及显式启用的记录会话，MUST NOT 重开相机或重置流时间基准。OFF SHALL 先关闭任务与结果接收、清框并切无框预览，再安全收尾模型与记录。收尾 MUST NOT 阻塞预览或遥测。停止中 SHALL 仅保留最后期望 ON/OFF。SC233HGS SHALL 只在流启动时执行原预热，OFF MUST NOT 取消预热。相机故障 SHALL 非零退出，恢复不依赖 ON；跟踪故障 SHALL 结束本轮并保持健康预览。模型 SHALL 跨轮复用，旧访问结束前不得重置。


新轮在旧模型访问结束后 SHALL 清空观测时间守卫和 LOS 速度历史。普通与持续预览两种轮次 SHALL 使用相同的观测复位规则。重复 ON MUST NOT 清空正在运行轮次的观测状态。OFF SHALL 按 target-observations 发送一次显式失效通知，先拒绝旧帧正常输出，再安全封存记录；不能让该通知重开相机或等待推理、写盘。

#### Scenario: 首次启动失败后重试
- **WHEN** 非持续预览模式首次 ON 时设备不可用，设备恢复后再次 ON
- **THEN** 首轮无残留工作线程，第二轮重新打开设备并使用新轮状态

#### Scenario: 检测中停止再启动
- **WHEN** 旧轮 YOLO 尚在推理时收到 OFF，随后清理完成并再次 ON
- **THEN** 旧轮帧安全存活到访问结束，新轮不获取旧结果，模型无需重新加载

#### Scenario: 没有目标仍录制
- **WHEN** ON 后一直处于 Detecting，随后收到 OFF
- **THEN** 录像覆盖该轮预热后的合格采集期间，OFF 后停止接收，视频不包含检测框或 OSD

#### Scenario: 预热中 OFF
- **WHEN** 非持续预览 SC233HGS 已开流但尚未输出合格帧时收到 OFF
- **THEN** 及时停止并回收本轮，不发布观察帧对应的框或图像，不因没有首帧误报预热失败

#### Scenario: 预热失败后新轮
- **WHEN** 非持续预览一轮因时间重复未能在界限内稳定，清理后再次收到 ON
- **THEN** 新轮重新预热，旧轮无帧或失败状态不占用新轮时间基准，不自动发送 TargetLost

#### Scenario: 录像准备后相机预热失败
- **WHEN** 非持续预览录像资源已准备，相机随后因预热未稳定结束本轮
- **THEN** 保留相机失败原因与零帧录像结果，有限回收录像资源，不把失败归因于编码器没有输出

#### Scenario: 录像故障后下一轮重试
- **WHEN** 合法录像目录当前不可写，ON 启动相机，随后 OFF，权限恢复后再次 ON
- **THEN** 首轮报告录像失败但跟踪继续，第二轮在旧录像资源已回收时建立新会话，不混入旧轮帧

#### Scenario: 录像收尾超时
- **WHEN** OFF 后录像写盘不能在收尾预算内完成
- **THEN** 报告录像未正常完成，停止提交，隔离旧会话资源，不无限等待写盘，也不改变模型在途访问的安全等待规则

#### Scenario: 录像参数无效但跟踪可运行
- **WHEN** SC233HGS 配置 `recording.enabled=true`，但录像目录或码率无效，随后收到 ON
- **THEN** 节点已完成初始化；本轮明确报告录像失败，健康的相机、跟踪和 HDMI 显示继续运行

#### Scenario: 持续预览反复启停
- **WHEN** 相机持续预览中反复 ON/OFF
- **THEN** 相机只开流一次，视频时间递增，每次实际新轮重置跟踪且不混入旧结果，录像仍逐轮建立和结束

#### Scenario: 持续预览预热中 OFF
- **WHEN** 持续预览 SC233HGS 仍在合法预热且收到 OFF
- **THEN** 跟踪和记录关闭，流预热继续，观察帧不提交任何像素输出，之后合格帧用于预览

#### Scenario: 持续预览相机故障
- **WHEN** 未发送 ON 的服务开流失败，或运行预览时相机失败
- **THEN** 保存原因并安全非零退出，不等待下一次 ON 重开设备

#### Scenario: 两种轮次均清空观测历史
- **WHEN** 分别在 osd.enabled=false 与合法持续预览模式完成两轮 ON/OFF
- **THEN** 两模式的新轮首次 LOS 均从零角速度开始，旧轮时间守卫和消息不能进入新轮；持续模式仍只开流一次

### Requirement: 图像与预测消息保持原有语义

系统 SHALL 保留输入 /fpv/gimbal（sensor_msgs/msg/Imu）、/fpv/odom（nav_msgs/msg/Odometry）、/fpv/tracker_trigger（std_msgs/msg/Header）。系统 SHALL 保留输出 /fpv/target_bbox（vision_msgs/msg/Detection2D）、/fpv/tracker_image（sensor_msgs/msg/Image）、/fpv/tracker_state（std_msgs/msg/Int8）。系统 SHALL 增加 /fpv/target_los（geometry_msgs/msg/TwistStamped）输出和仅供记录的 /mavros/imu/data（sensor_msgs/msg/Imu）输入。QoS，即消息队列与可靠性设置，SHALL 按 target-observations 更新 gimbal、tracker_image 及新增端点；其他业务端点保持不变。

节点 SHALL 通过唯一帧源消费者取最新排队帧，保留其原尺寸像素与帧源时间。图像开启时 SHALL 发布独立 BGR8 图像，时间为原帧时间，绘图不修改共享灰度帧。框与 LOS SHALL 按 target-observations 采用同一参考时刻，默认采集时间。框 SHALL 保留浮点精度，检测有效时 frame_id 为 camera_frame，否则为空；无有效姿态时 SHALL 仅使 LOS 无效。未消费帧 SHALL 保持既有不发布结果行为。持续预览帧 MUST NOT 产生检测框、LOS、状态或 /fpv/tracker_image。调试图像 SHALL 仍仅在 visualized_result=true 且当前轮次实际消费图像时发布；OFF 后旧轮不得启动新的帧结果发布，唯一例外是按 target-observations 发送一次显式无效控制通知；已交给 ROS 中间件的消息允许延迟送达。姿态参考与重复抑制 SHALL 按 target-observations 执行，缺姿态时 KF 可保留单位阵回退，但不能据此生成有效 LOS。调试图像 MUST NOT 写入 bag，且与 LOS 的有效性和重复抑制独立。

状态 SHALL 保持 Detecting=0、TargetFound=1、Tracking=2、TargetLost=3，仅变化时发布，每轮首次状态仍可发布。OFF 和设备失败 MUST NOT 被伪装为算法 TargetLost。没有曝光证据时 MUST NOT 宣称图像时间代表曝光时刻。

#### Scenario: 帧时间与当前时间不同
- **WHEN** 保留帧时间为 T、publish_prediction=true 且合格姿态时间为 T+20000 微秒，图像输出开启
- **THEN** 图像使用 T，框与 LOS 使用 T+20000 及该时刻姿态，不把图像改标为观测参考时间

#### Scenario: 无目标新轮
- **WHEN** 两轮首次消费都处于 Detecting 且没有有效目标
- **THEN** 每轮都可发布状态 0，框与 LOS 的 frame_id 均为空，不为验收伪造捕获

#### Scenario: 预览不扩展 ROS 图像
- **WHEN** 持续预览 visualized_result=true，ON 前和 OFF 后持续有图像
- **THEN** HDMI 更新但没有预览帧对应的业务输出；OFF 只允许一次显式失效观测。ON 有效消费窗口才可发布调试图像，图像保留采集时间和独立 BGR8 像素

#### Scenario: 缺姿态不阻止独立显示
- **WHEN** 当前轮获得有效跟踪框但缺少相机姿态，且开启 OSD 和 visualized_result
- **THEN** 框可有效、LOS 无效，同帧 HDMI 与调试图像继续按各自规则处理，不新增预览线程或相机消费者
