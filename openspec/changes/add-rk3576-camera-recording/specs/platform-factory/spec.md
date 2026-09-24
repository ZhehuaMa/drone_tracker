# 规格增量

## MODIFIED Requirements

### Requirement: 公共帧源保持现有采集与录像行为

帧源是提供图像队列的相机或视频读取器。系统 SHALL 通过不含硬件开发库类型的接口提供启动、停止、运行状态、尺寸查询及取帧能力。Jetson SHALL 保留现有输入选择、尺寸查询值、队列容量、满队列处理、采集时间戳和历史时间戳行为。RK3576 视频 SHALL 按 rk3576-video-frame-source 规格提供原尺寸灰度帧、视频时间及从头重启行为，沿用现有视频队列分配配置和满队列处理。RK3576 AR0234 相机 SHALL 遵守 rk3576-camera-frame-source 的两种采集模式、时间与队列契约。SC233HGS 相机 SHALL 遵守 rk3576-sc233hgs-camera-frame-source 的连续采集、时间和队列契约。系统 MUST NOT 为适配原有取帧接口增加额外图像队列或采集转发线程；软件触发可以使用独立执行上下文，但 MUST NOT 成为第二个图像队列生产者。

录像配置指 `need_save`、保存目录和文件名前缀。系统 SHALL 允许公共节点在启动前提交现有录像配置。Jetson 相机 SHALL 沿用现有图像录像行为。视频文件输入 SHALL 保留不启动相机录像的行为。视频和 Jetson 节点 SHALL 保留现有 ROS bag 消息记录逻辑。RK3576 相机 SHALL 继续拒绝 need_save=true，遵守 rk3576-ros-camera 的旧消息记录边界。SC233HGS SHALL 另行接受 rk3576-camera-recording 的独立配置，通过不暴露硬件开发库类型的配置与状态边界提供录像。该独立支路 SHALL 允许新增有界录像队列和录像工作进程，但 MUST NOT 改变公共取帧队列的单消费者规则。AR0234 SHALL 继续拒绝启用录像。

#### Scenario: 相机启动录像
- **WHEN** Jetson 相机输入收到 ON 触发且 `need_save=true`
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

#### Scenario: RK3576 相机记录请求
- **WHEN** RK3576 相机配置 need_save=true
- **THEN** 初始化明确拒绝，不开始采集或创建部分记录

#### Scenario: SC233HGS 独立录像
- **WHEN** 公共节点为 SC233HGS 提交独立录像配置并启动相机
- **THEN** 平台后端装配原画面录像支路，两支路仅接收相机最终确认有效的合格帧，录像暂存帧在最终检查前不得发布给编码器，公共节点不依赖编码器硬件类型，算法队列仍只有原消费者

#### Scenario: 关闭新录像
- **WHEN** 未启用独立录像配置
- **THEN** 无新增录像图像复制、队列或工作进程，Jetson、AR0234 和视频的原有行为保持不变

#### Scenario: 相机预热与录像配置独立
- **WHEN** SC233HGS 关闭独立录像后启动
- **THEN** 相机仍按自身契约预热，公共取帧接口在预热期间没有图像可取，不通过录像开关绕过预热

