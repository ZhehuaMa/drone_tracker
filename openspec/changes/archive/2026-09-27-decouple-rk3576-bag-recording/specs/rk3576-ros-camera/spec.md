# Spec Delta

## MODIFIED Requirements

### Requirement: 相机入口和配置显式且固定

正式节点 SHALL 接受 video 加一个或多个十进制数字的相机简写，以及绝对 /dev 设备路径，并核验实际设备能力。专用 rk3576_camera.launch.py SHALL 接受 params_file 和显式 video_source，仅覆盖输入路径。相机 SHALL 显式配置传感器型号（`sensor_model`，取值 `ar0234` 或 `sc233hgs`，未显式设置时默认 `ar0234`）、媒体设备、传感器实体、尺寸、设备帧率及 capture_mode。capture_mode SHALL 为 continuous 或 software_trigger。软件模式 SHALL 提供范围 1 至设备 fps 的有限 trigger_rate_hz，连续模式 SHALL 为零。当 `sensor_model` 为 `sc233hgs` 时，capture_mode SHALL 只能为 continuous；配置为 software_trigger SHALL 在初始化阶段明确拒绝并说明该型号不支持软件触发，不得静默按连续模式启动。参数前缀 SHALL 为 mv_camera，型号、媒体、传感器及缓冲字段分别为 sensor_model、media_device、sensor_entity、buffer_count。

相机 SHALL 要求 use_history_timestamp=false、use_sim_time=false。`need_save` SHALL 仅控制 ROS bag 消息记录，不得因其为 true 而拒绝相机初始化。模型路径 SHALL 遵守现有四文件角色规则。camera.fx/fy/cx/cy SHALL 全部显式且有限，fx/fy SHALL 为正；它们表示实际图像像素坐标中的焦距和主点，MUST NOT 再按输出尺寸缩放。运行期修改输入、模式、频率、内参、模型、算法、时钟、记录及传感器型号配置 SHALL 明确拒绝。

录像 SHALL 使用独立的 `recording.*` 启动参数并遵守 `rk3576-camera-recording`。仅 SC233HGS 相机 SHALL 实际启用 `recording.enabled=true` 指定的原画面录像。RK3576 AR0234 或视频输入设置该值时，节点 SHALL 完成初始化，并在 ON 后明确报告该录像不受支持，不得静默宣称已录像。SC233HGS 的录像专属配置无效时，节点 SHALL 完成初始化，并在 ON 后报告该轮录像失败。运行期修改 `recording.*` SHALL 明确拒绝。关闭录像 SHALL 保持现有输入行为，不创建录像进程或视频文件。新录像 MUST NOT 自动打开 ROS bag。

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
- **THEN** 初始化成功并等待 ON；ON 后尝试创建 ROS bag，不因此启动相机视频录像

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

#### Scenario: 独立录像配置
- **WHEN** SC233HGS 相机配置 need_save=true、recording.enabled=true 且录像配置合法
- **THEN** 节点完成初始化并等待 ON；ON 后分别尝试 bag 与原画面录像，视频录像不受 visualized_result 或 OSD 开关影响

#### Scenario: 运行中修改录像周期
- **WHEN** 节点运行中请求修改 recording.sync_interval_ms
- **THEN** 明确拒绝并说明需重启节点应用

#### Scenario: 其他输入启用新录像
- **WHEN** RK3576 AR0234 或视频文件输入配置 recording.enabled=true
- **THEN** 节点完成初始化，ON 后明确报告独立视频录像不可用；need_save 仍按其值控制 bag

#### Scenario: Jetson 输入行为保留
- **WHEN** Jetson 输入配置 recording.enabled=true
- **THEN** 仍按 Jetson 既有规则拒绝该配置，不因 RK3576 的记录拆分改变参考实现

### Requirement: ON 和 OFF 控制完整运行轮次

ON SHALL 启动整轮采集与跟踪，MUST NOT 解释为单次拍照命令。软件模式 SHALL 在采集就绪后开始周期触发。连续模式 SHALL 不发送触发命令。OFF SHALL 停止后续触发并结束本轮，重复 ON 不增加工作线程，空闲 OFF 安全。启动失败 SHALL 回滚已有资源。首帧超时 SHALL 遵守相机帧源契约。

新轮 SHALL 清空旧队列、统计和跟踪状态，建立新时间基准。模型 SHALL 跨轮复用。收尾 SHALL 清除旧检测请求和结果，并等待在途模型访问结束。相机不报告文件 EOF；普通设备失败后的下一次 ON SHALL 重新打开核验设备。

SC233HGS 每轮 SHALL 遵守 `rk3576-sc233hgs-camera-frame-source` 的有界启动预热。预热是交付图像前的时间递增观察期。预热观察帧 MUST NOT 提交算法，也 MUST NOT 产生对应的检测框或图像消息。预热阶段 SHALL 能响应 OFF，不等待预热期限结束。预热未稳定或致命元数据错误 SHALL 作为相机失败结束本轮，MUST NOT 伪装成算法 TargetLost。预热 SHALL 只增加内部诊断阶段，不改变四个 TrackerState 数值或六个业务话题。AR0234、Jetson 与视频 SHALL 保持原启停和时间语义。

启用录像时，每次 ON SHALL 尝试建立独立录像会话，从相机完成归还、停止及预热期限最终检查后的合格帧边界开始录制，包括没有目标的期间。OFF SHALL 停止录像接收并结束该会话，不继续采集。重复 ON MUST NOT 创建第二个录像会话。录像专属配置无效或不受支持、录像资源故障或运行故障 SHALL 只使该轮录像失败并报告，MUST NOT 阻止节点初始化或停止仍健康的采集、跟踪或 OSD。其他相机、模型及跟踪配置仍按原规则校验。相机/跟踪本身故障 SHALL 按原规则结束整轮。录像收尾 SHALL 有有限等待预算并与相机停止、模型等待分别报告；超时 MUST NOT 无限阻塞 ON/OFF。

录像准备与相机预热 SHALL 使用独立起点和预算；录像准备耗时 MUST NOT 缩短开流后的预热期限。预热中 OFF 或失败 SHALL 按 `rk3576-camera-recording` 保存零帧取消或失败结果，不伪造视频。录像时间起点及基于视频输入的健康检查 SHALL 从合格帧边界建立，MUST NOT 把等待预热误报为编码或持久化停滞。

#### Scenario: 首次启动失败后重试
- **WHEN** 首次 ON 时设备不可用，设备恢复后再次 ON
- **THEN** 首轮无残留工作线程，第二轮重新打开设备并使用新轮状态

#### Scenario: 检测中停止再启动
- **WHEN** 旧轮 YOLO 尚在推理时收到 OFF，随后清理完成并再次 ON
- **THEN** 旧轮帧安全存活到访问结束，新轮不获取旧结果，模型无需重新加载

#### Scenario: 没有目标仍录制
- **WHEN** ON 后一直处于 Detecting，随后收到 OFF
- **THEN** 录像覆盖该轮预热后的合格采集期间，OFF 后停止接收，视频不包含检测框或 OSD

#### Scenario: 预热中 OFF
- **WHEN** SC233HGS 已开流但尚未输出合格帧时收到 OFF
- **THEN** 及时停止并回收本轮，不发布观察帧对应的框或图像，不因没有首帧误报预热失败

#### Scenario: 预热失败后新轮
- **WHEN** 一轮因时间重复未能在界限内稳定，清理后再次收到 ON
- **THEN** 新轮重新预热，旧轮无帧或失败状态不占用新轮时间基准，不自动发送 TargetLost

#### Scenario: 录像准备后相机预热失败
- **WHEN** 录像资源已准备，相机随后因预热未稳定结束本轮
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


## ADDED Requirements

### Requirement: 短时比较 bag 开关对跟踪和 HDMI 帧率的影响

RK3576 生产板 SHALL 使用同一型号板卡、同一相机输入、相同模型、独立录像开关和 HDMI 配置，对比 `need_save=false` 与 `need_save=true`。生产配置及帧率对照 SHALL 保持 `visualized_result=false`；该参数只控制调试图像输出。跟踪帧率 SHALL 按测量窗口内实际消费帧数除以单调时钟经过秒数计算。HDMI 视频帧率 SHALL 按实际成功显示的新视频帧数除以同一时间基准的经过秒数计算，不得将仅更新叠加层的刷新计作新视频帧。报告 SHALL 同时给出两个开关状态的原始计数、测量时长、帧率、绝对差值、相对差值和采集、bag 队列及 HDMI 丢帧数。任何单次性能或负载测试的总运行时间 MUST NOT 超过 180 秒；不得将较长历史测试拼接成此次验收。

#### Scenario: 同板成对对照
- **WHEN** 同一生产板对同一 SC233HGS 场景分别运行 `need_save=false` 和 `need_save=true`，两轮均设置 `recording.enabled=true`、`visualized_result=false` 并启用 HDMI，且成对测试在 180 秒内结束
- **THEN** 报告分别列出跟踪实际消费帧率和 HDMI 实际显示新帧率及其差值，并列出两轮的丢帧、bag 状态、输入和模型身份；不得只用参数设定帧率或 HDMI 刷新率替代实测值
