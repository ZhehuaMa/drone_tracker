# Spec Delta

## MODIFIED Requirements

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
