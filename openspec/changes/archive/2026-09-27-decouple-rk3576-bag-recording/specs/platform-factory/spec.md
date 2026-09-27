# Spec Delta

## MODIFIED Requirements

### Requirement: 公共帧源保持现有采集与录像行为

帧源是提供图像队列的相机或视频读取器。系统 SHALL 通过不含硬件开发库类型的接口提供启动、停止、运行状态、尺寸查询及取帧能力。Jetson SHALL 保留现有输入选择、尺寸查询值、队列容量、满队列处理、采集时间戳和历史时间戳行为。RK3576 视频 SHALL 按 rk3576-video-frame-source 规格提供原尺寸灰度帧、视频时间及从头重启行为，沿用现有视频队列分配配置和满队列处理。RK3576 AR0234 相机 SHALL 遵守 rk3576-camera-frame-source 的两种采集模式、时间与队列契约。SC233HGS 相机 SHALL 遵守 rk3576-sc233hgs-camera-frame-source 的连续采集、时间和队列契约。系统 MUST NOT 为适配原有取帧接口增加额外图像队列或采集转发线程；软件触发可以使用独立执行上下文，但 MUST NOT 成为第二个图像队列生产者。

`need_save` SHALL 在 RK3576 节点中仅控制 ROS bag，不得作为相机帧源录像请求。Jetson 相机 SHALL 沿用现有图像录像行为及 ROS bag 行为。视频文件输入 SHALL 保留不启动相机图像录像的行为。RK3576 相机 SHALL 接受 `need_save=true`，且不因此改变采集队列或相机视频录像状态。SC233HGS SHALL 另行接受 `recording.enabled` 指定的独立原画面录像，通过不暴露硬件开发库类型的配置与状态边界提供该能力。该独立支路 SHALL 允许新增有界录像队列和录像工作进程，但 MUST NOT 改变公共取帧队列的单消费者规则。RK3576 AR0234 SHALL 明确报告独立视频录像不可用，同时允许健康跟踪初始化与运行。

#### Scenario: 相机启动录像
- **WHEN** Jetson 相机输入收到 ON 触发且 `need_save=true`
- **THEN** 相机使用现有目录与前缀配置录像，节点继续记录原有 ROS bag 话题，无需转换为具体相机类型

#### Scenario: 视频文件启动
- **WHEN** RK3576 视频文件输入收到 ON 触发且 `need_save=true`
- **THEN** 不启动相机图像录像，节点按 `rk3576-ros-video` 的记录和故障隔离规则尝试 ROS bag

#### Scenario: 采集停止后再次启动
- **WHEN** 同一 Jetson 输入依次收到 ON、OFF、ON
- **THEN** 不遗留上一轮采集线程，不增加队列消费者，时间戳及队列处理沿用现有输入语义

#### Scenario: RK3576 视频重新启动
- **WHEN** 调用方停止 RK3576 视频和上一轮消费，再次调用启动
- **THEN** 视频从头播放，旧队列不进入新轮，实时模式建立新基准而历史模式保留录像时间

#### Scenario: RK3576 相机记录请求
- **WHEN** RK3576 相机配置 need_save=true
- **THEN** 节点完成初始化并等待 ON；相机帧源不接收旧录像请求，ON 后由节点独立尝试建立 ROS bag

#### Scenario: SC233HGS 独立录像
- **WHEN** 公共节点为 SC233HGS 提交独立录像配置并启动相机
- **THEN** 平台后端装配原画面录像支路，两支路仅接收相机最终确认有效的合格帧，录像暂存帧在最终检查前不得发布给编码器，公共节点不依赖编码器硬件类型，算法队列仍只有原消费者

#### Scenario: 关闭新录像
- **WHEN** 未启用独立录像配置
- **THEN** 无新增录像图像复制、队列或工作进程，Jetson、AR0234 和视频的原有行为保持不变

#### Scenario: 相机预热与录像配置独立
- **WHEN** SC233HGS 关闭独立录像后启动
- **THEN** 相机仍按自身契约预热，公共取帧接口在预热期间没有图像可取，不通过录像开关绕过预热

#### Scenario: RK3576 不支持视频录像的输入
- **WHEN** RK3576 AR0234 或视频文件输入配置 `recording.enabled=true` 并收到 ON
- **THEN** 节点明确报告独立视频录像不可用，仍按 `need_save` 的值尝试 ROS bag，健康跟踪继续
