## MODIFIED Requirements

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
