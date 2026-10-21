# target-observations Specification

## Purpose

让 RK3576 的下游消费者获得描述同一目标、同一参考时刻的浮点框与视线方向，并区分目标运动和相机自转。明确消息字段、无效状态、时间选择、复位及停止通知，使观测可以独立于显示图像解释和验证。

## Requirements

### Requirement: 目标框与视线消息具有共同身份

系统 SHALL 在 RK3576 的 `/fpv/target_bbox` 发布 `vision_msgs/msg/Detection2D`，在 `/fpv/target_los` 发布 `geometry_msgs/msg/TwistStamped`。LOS 表示目标视线方向。每次接受的观测 SHALL 同时构造两条时间戳相同的消息。有效 frame_id SHALL 分别为 `camera_frame` 和 `camera_flu`。FLU 表示相机前、左、上坐标。LOS 的 linear.y/z SHALL 分别表示向下为正的俯仰角、向左为正的偏航角，单位 rad；angular.y/z SHALL 表示对应角速度，单位 rad/s。其他分量 SHALL 为零。系统 MUST NOT 将两话题发布返回正常解释为订阅端原子接收。

#### Scenario: 亚像素目标方向
- **WHEN** fx=fy=600、cx=768、cy=480，目标中心为 (828,480)，旋转有效且为首个样本
- **THEN** 框中心保留 828 和 480，LOS 偏航为 atan2(-0.1,1)，俯仰为零，两个角速度为零，时间戳相同

#### Scenario: 两个话题独立传递
- **WHEN** 框发布调用正常返回而 LOS 发布调用失败
- **THEN** 保存两个调用的不同结果，不报告该对已被下游完整接收，不重用旧 LOS 冒充本次结果

### Requirement: 框的浮点精度和有效性明确

发布框 SHALL 保留预测输出的浮点中心与宽高。系统 SHALL 在实际交付图像范围内裁剪框。仅当跟踪有效、框分量有限且裁剪后宽高为正时，检测 frame_id SHALL 为 `camera_frame`，否则两条观测 SHALL 均无效。无效消息 SHALL 使用空 frame_id。框有效但相机姿态缺失或非法时，系统 SHALL 保留有效框并仅使 LOS 无效。整数显示框 MUST NOT 替代浮点发布框。

#### Scenario: 小数中心保留
- **WHEN** 合法预测框中心为 (768.5,480.25)，宽高为 (20.5,30.25)，且未超出图像
- **THEN** 发布框保留这些浮点值，不截断为整数

#### Scenario: 预测完全越界
- **WHEN** 跟踪标记仍有效但预测框与图像无正面积交集
- **THEN** 框与 LOS 的 frame_id 均为空，不报告图像外有效目标

#### Scenario: 有框但无相机姿态
- **WHEN** 已得到有效浮点框，但没有 `/fpv/gimbal` 相机旋转
- **THEN** 框使用 camera_frame，LOS 使用空 frame_id 且数值为零，不以默认单位阵伪造有效 LOS

### Requirement: 角速度补偿相机旋转并按时间平滑

系统 SHALL 用相机像素内参将目标中心转换为视线角度。计算角速度前 SHALL 将上一视线与上一滤波速度转换到当前相机坐标，消除相机自转影响。角度差 SHALL 处理跨界。角速度 SHALL 使用 alpha=1-exp(-dt/tau) 平滑；tau=0 SHALL 关闭平滑。首样本及采样间隔大于 0.3 秒的样本 SHALL 输出零角速度。非有限坐标、非有效旋转和非递增估计时间 SHALL 被拒绝为有效 LOS。

#### Scenario: 静止世界目标与复合相机旋转
- **WHEN** 同一世界视线保持不变，相机先后发生偏航、俯仰和滚转，像素位置据此变化
- **THEN** 补偿后的两个角速度在数值测试容差内为零，不能把相机转动计为目标运动

#### Scenario: 非等比焦距
- **WHEN** fx=600、fy=300，目标位于主点右方 60 像素、下方 30 像素
- **THEN** 两方向分别按自己的焦距换算，不假定 fx=fy

#### Scenario: 长间隙重新观察
- **WHEN** 上一有效样本位于 1.0 秒，下一有效样本位于 1.300001 秒
- **THEN** 下一样本角度有效，但角速度清零；恰好 0.3 秒间隔仍按正常差分规则处理

### Requirement: 参考时刻与相机姿态一致

系统 SHALL 默认使用 `publish_prediction=false`，使框和 LOS 描述采集时刻。开启预测时，系统 SHALL 仅在最新姿态满足 frame_time <= pose_time <= node_now，且 node_now-pose_time <= 300000 微秒时，使用该姿态时间与对应旋转。否则 SHALL 回退采集时间及采集姿态。系统 MUST NOT 把旧姿态重标为当前系统时间。图像时间 SHALL 始终保留采集时间。300 ms 限制 MUST NOT 被解释为采集姿态最近邻查询已具有新年龄保证。

#### Scenario: 默认采用采集时间
- **WHEN** 帧为 10.000 秒，最新姿态为 10.030 秒，当前系统时间为 10.050 秒，预测关闭
- **THEN** 框和 LOS 均使用 10.000 秒及采集姿态

#### Scenario: 使用合格姿态时间
- **WHEN** 上述时间条件相同且预测开启
- **THEN** 框与 LOS 均使用 10.030 秒及该姿态，不使用 10.050 秒

#### Scenario: 新鲜度边界及不合格姿态
- **WHEN** 最新姿态恰好距当前时间 300000 微秒且不早于帧，或分别测试 300001 微秒、未来姿态、早于帧的姿态、空历史
- **THEN** 第一种可用于预测，其余均回退采集参考

### Requirement: 重复抑制与速度复位各自生效

同一轮观测时间 SHALL 严格递增。时间重复或倒退时系统 SHALL 抑制整对消息并记录原因。抑制 MUST NOT 自动抑制同帧图像或状态变化。新轮 SHALL 清空发布时间守卫及 LOS 历史。目标无效、姿态缺失或非法、成功 YOLO 重锚定 SHALL 清空 LOS 速度历史。重锚定复位 SHALL 在重复时间检查前生效，不能因该帧不发布而遗失复位。一般检测确认 MUST NOT 被当作成功重锚定。

#### Scenario: 多帧选到同一姿态
- **WHEN** 预测开启，两次有效更新选择相同姿态参考时间
- **THEN** 仅第一对观测发布，第二次计为重复抑制；调试图像与变化状态继续按各自规则处理

#### Scenario: 重复时间上发生重锚定
- **WHEN** 一次成功重锚定恰好使用已经发布过的参考时间
- **THEN** 本次不重复发布，下一次递增时间的有效 LOS 从零角速度开始

#### Scenario: 新轮允许较早时间
- **WHEN** 旧轮最后消息为 100 秒，旧访问已结束后新 ON 轮次从 50 秒开始
- **THEN** 新轮可发布 50 秒的观测，不继承旧轮时间守卫

### Requirement: 姿态重复和回退不制造倒序观测

相等时间的姿态 SHALL 被去重。姿态时间倒退时系统 SHALL 清空旧姿态历史并清空 LOS 速度历史，再保留新姿态。该动作 MUST NOT 自动清除仍在运行轮次的观测时间守卫。真实系统时间倒退造成的较早观测 SHALL 被抑制并可诊断，系统 MUST NOT 伪造递增采集时间。后续 OFF/ON SHALL 按轮次隔离规则重建时间基准。

#### Scenario: 姿态进入新时间序列
- **WHEN** 历史最新姿态为 1.05 秒，收到相等时间样本后又收到 0.5 秒样本
- **THEN** 相等样本不增加历史，0.5 秒开始新的姿态历史；不能据此在当前轮发布早于最后消息的观测

#### Scenario: 系统校时后重新启动跟踪
- **WHEN** 活动轮次出现系统时间回退，随后操作方 OFF 并在旧访问结束后 ON
- **THEN** 回退期间的抑制原因可查，新轮接受新时间基准；不能声称旧轮已自动无缝恢复

### Requirement: OFF 发送一次明确的失效观测

对已经建立的轮次，系统 SHALL 在 OFF 关闭正常结果接收后发送一次 frame_id 均为空的成对失效通知。通知时间 SHALL 大于本轮最后已构造观测时间，采用当前时间与最后时间加一微秒的较大者。通知 SHALL 在本轮 bag 封存前尝试提交。重复 OFF 及首次 ON 前的 OFF MUST NOT 反复创建通知。通知 MUST NOT 被算作输入帧结果或算法 TargetLost。旧推理结果 MUST NOT 在通知后发起有效观测。持续 HDMI 预览 MUST NOT 等待在途推理或写盘完成。

#### Scenario: 推理阻塞时 OFF
- **WHEN** 持续预览模式一轮推理尚未返回而收到 OFF
- **THEN** 无效通知不等待推理完成，HDMI 按原契约清框并继续预览；旧推理返回后不启动有效消息发布

#### Scenario: 重复停止
- **WHEN** 同一已建立轮次连续收到两个 OFF，随后保持预览
- **THEN** 只尝试一对 OFF 通知，不持续发布无效框、LOS 或 ROS 预览图像

### Requirement: 观测启动配置与实际像素一致

RK3576 SHALL 提供固定启动参数 `publish_prediction=false` 和 `los.rate_filter_tau=0.12` 秒。tau SHALL 有限且非负。运行期修改 SHALL 被拒绝。LOS 与 KF SHALL 共用显式 `camera.fx/fy/cx/cy` 像素内参；fx/fy SHALL 为正且所有值有限。系统 MUST NOT 按相机名义输出或 HDMI 尺寸再次缩放内参。实际交付尺寸 SHALL 用于框裁剪。系统 MUST NOT 引入 `enable_preview` 或扩大 ROS 图像发布到 ON 前、OFF 后。

#### Scenario: 原像素内参与不同 HDMI 尺寸
- **WHEN** 输入为 1920×1200、显式 fx=1333.3、fy=1300，HDMI 为 720×480
- **THEN** KF 和 LOS 均使用 1333.3 与 1300，不按 HDMI 比例重算，裁剪范围仍为 1920×1200

#### Scenario: 非法或运行期参数
- **WHEN** 启动 tau 为负数或非有限值，或初始化后请求修改 tau 或 publish_prediction
- **THEN** 非法启动明确失败；运行期更新返回失败并说明需重启，不成功返回后继续沿用旧值

### Requirement: ROS 消息队列和可靠性按观测用途配置

QoS 表示 ROS 消息可靠性及队列深度。RK3576 SHALL 使用 best-effort、depth 10 接收 `/fpv/gimbal`，使用 best-effort、depth 100 接收 `/mavros/imu/data`。后者 SHALL 只用于记录原消息，不替代相机姿态。`/fpv/tracker_image` SHALL 使用 best-effort、depth 1。框与 LOS SHALL 各使用 reliable、depth 10。其他业务端点 SHALL 保留既有 QoS。图像 QoS 改变 MUST NOT 启用 ROS 预览。

#### Scenario: 兼容图像订阅
- **WHEN** visualized_result=true，轮次实际消费图像，接收端请求 best-effort 图像
- **THEN** 接收端可收到跟踪图像；ON 前与 OFF 后只保留现有 HDMI/OSD 预览

#### Scenario: MAVROS 姿态不替代云台
- **WHEN** 只输入有效 `/mavros/imu/data` 而没有相机 `/fpv/gimbal`
- **THEN** 可按记录开关保存原 IMU，但不能因此生成有效相机 LOS
