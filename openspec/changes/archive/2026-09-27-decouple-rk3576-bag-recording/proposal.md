# Proposal

## Why

当前 RK3576 相机只要设置 `need_save=true` 就无法初始化，因此运行时无法同时跟踪目标和保存 ROS 消息。即使在视频输入中成功开启记录，ROS bag（按话题保存 ROS 消息的文件）写盘故障也会结束跟踪轮次，且同步写盘可能拖慢跟踪和 HDMI 显示。

## What Changes

- 将 `need_save` 限定为 ROS bag 开关，将 `recording.enabled` 限定为 SC233HGS 相机原始视频录像开关；两项可独立启用，也可同时启用。
- RK3576 相机设置 `need_save=true` 时允许节点初始化。bag 的目录或写入故障只使 bag 会话失败，并明确报告，不停止健康的采集、跟踪、HDMI 显示或独立视频录像。RK3576 上无效或不受支持的 `recording.*` 配置也不阻止节点初始化；收到 `ON` 后明确报告独立视频录像不可用。Jetson 参考实现的输入行为保持不变。
- 将 bag 写入移至独立工作线程，使用按消息数和字节数双重限制的队列。队列溢出必须报告丢弃和 bag 不完整。保持既有话题、时间戳、文件不覆盖和 ON/OFF 分轮规则。
- 验证 bag 可关闭、可读取、消息内容与节点实际接受的消息一致。`visualized_result=false` 时，bag 中不得包含 `/fpv/tracker_image` 消息。`visualized_result` 是源码中的真实参数名；验收脚本不得误写为 `visualize_result`。
- 生产程序保持 `visualized_result=false`，不为帧率对照开启调试图像。在同一生产板、同一输入与同一 HDMI 配置下，对比 `need_save` 开启和关闭时的跟踪帧率、HDMI 实际显示帧率及丢帧。每次性能或负载测试不超过 3 分钟，并报告测试条件及差值，不预设未经实测支持的性能门槛。

例如，SC233HGS 配置 `need_save=true`、`recording.enabled=true` 后，节点应完成初始化并等待 `ON`。收到 `ON` 后分别创建 bag 和视频会话。若 bag 目录不可写，节点报告 bag 失败，仍继续跟踪与视频录像；收到 `OFF` 后分别收尾。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `rk3576-ros-camera`：允许相机启用 bag；两个记录开关和故障状态独立；增加跟踪与 HDMI 帧率对比验收。
- `rk3576-ros-video`：修改 bag 失败与收尾语义，并要求完整性和禁用图像输出的读取验证。
- `rk3576-camera-recording`：将录像专属配置故障从节点初始化失败改为本轮录像失败，保持健康跟踪运行。
- `platform-factory`：从 RK3576 相机帧源解除旧 `need_save` 录像请求，保持 Jetson 与视频输入各自的帧源行为。

## Impact

- 节点及记录边界：`src/drone_tracker_node.*`、`src/platform/frame_source_ops.h`、RK3576 相机帧源与相关测试。
- 配置和说明：`parameters/`、`src/platform/README.md`；不修改现有 ROS 话题、消息类型、跟踪状态或模型结果。
- 验证：bag 读取回查、失败注入、并发启停、短时生产板帧率对照。Jetson 参考行为需通过静态审查和适用的测试保留；没有 Jetson 实机环境时不得宣称实机通过。
