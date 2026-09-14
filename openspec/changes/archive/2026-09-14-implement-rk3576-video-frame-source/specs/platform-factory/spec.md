## MODIFIED Requirements

### Requirement: 公共帧源保持现有采集与录像行为

帧源是提供图像队列的相机或视频读取器。系统 SHALL 通过不含硬件开发库类型的接口提供启动、停止、运行状态、尺寸查询及取帧能力。Jetson SHALL 保留现有输入选择、尺寸查询值、队列容量、满队列处理、采集时间戳和历史时间戳行为。RK3576 视频 SHALL 按 rk3576-video-frame-source 规格提供原尺寸灰度帧、视频时间及从头重启行为，沿用现有视频队列分配配置和满队列处理。系统 MUST NOT 为适配接口增加额外队列或额外采集线程。

录像配置指 `need_save`、保存目录和文件名前缀。系统 SHALL 允许公共节点在启动前提交现有录像配置。Jetson 相机 SHALL 沿用现有图像录像行为。视频文件输入 SHALL 保留不启动相机录像的行为。节点 SHALL 保留现有 ROS bag 消息记录逻辑。

#### Scenario: 相机启动录像
- **WHEN** 相机输入收到 ON 触发且 `need_save=true`
- **THEN** 相机使用现有目录与前缀配置录像，节点继续记录原有 ROS bag 话题，无需转换为具体相机类型

#### Scenario: 视频文件启动
- **WHEN** 视频文件输入收到 ON 触发且 `need_save=true`
- **THEN** 不启动相机图像录像，节点的既有 ROS bag 行为保持不变

#### Scenario: 采集停止后再次启动
- **WHEN** 同一 Jetson 输入依次收到 ON、OFF、ON
- **THEN** 不遗留上一轮采集线程，不增加队列消费者，时间戳及队列处理沿用现有输入语义

#### Scenario: RK3576 视频重新启动
- **WHEN** 调用方停止 RK3576 视频和上一轮消费，再次调用启动
- **THEN** 视频从头播放，旧队列不进入新轮，实时模式建立新基准而历史模式保留录像时间

## ADDED Requirements

### Requirement: RK3576 独立工厂提供视频但保留生产构建边界

RK3576 工厂 SHALL 对合法本地 MJPEG 视频请求返回可用公共帧源，不在创建时启动生产线程。创建失败 SHALL 包含平台、输入路径和原因。相机请求 SHALL 继续明确返回不支持。视频帧源 SHALL 忽略相机专属尺寸、fps、翻转、镜像配置，并返回实际视频尺寸。根目录生产构建 SHALL 继续仅支持 Jetson；独立视频目标 SHALL 不引入 CUDA、TensorRT、VPI 或 Tegra 依赖。视频帧源自身 SHALL 不要求加载模型。

#### Scenario: 工厂创建视频
- **WHEN** 通过 RK3576 工厂提交合法 MJPEG 视频路径及任意相机输出尺寸
- **THEN** 返回未启动的真实帧源，尺寸查询为原视频尺寸，可通过公共接口启动及取帧

#### Scenario: 相机和根目录入口仍不开放
- **WHEN** 请求 RK3576 相机或从根目录配置 DRONE_TRACKER_PLATFORM=rk3576
- **THEN** 前者报告相机不支持，后者在 Jetson SDK 发现前继续报告仅支持 Jetson

#### Scenario: 软件视频目标依赖隔离
- **WHEN** 构建和运行独立视频帧源测试
- **THEN** 不链接 Jetson 设备库，也不需要加载真实模型或提交 NPU 推理
