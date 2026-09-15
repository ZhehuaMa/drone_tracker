## MODIFIED Requirements

### Requirement: 构建明确选择一个已实现平台

平台后端是适配特定硬件的采集、图像处理和模型执行实现。系统 SHALL 通过构建选项 `DRONE_TRACKER_PLATFORM` 选择生产后端。未指定该选项时 SHALL 使用 `jetson`。生产构建 SHALL 支持 `jetson` 和 `rk3576`。系统 SHALL 在启用硬件编译语言或查找硬件开发库之前拒绝未知选项值。失败诊断 SHALL 包含请求值及当前支持值。系统 MUST NOT 自动回退到另一后端。

RK3576 生产节点 SHALL 无需 CUDA、TensorRT、VPI、Jetson Multimedia API 或 Tegra 依赖即可构建。Jetson 生产节点 SHALL 不因本变更要求 RKNN 或 RGA。RK3576 构建的缺失依赖诊断 SHALL 说明所选平台和缺失项。RKNN 依赖诊断 SHALL 说明目标 Runtime 2.3.2 与驱动 0.9.8 基线。

#### Scenario: 使用既有构建入口
- **WHEN** 未设置平台选项并在满足依赖的 Jetson 环境配置生产构建
- **THEN** 选择 Jetson 后端，既有运行参数无需增加平台参数

#### Scenario: 请求 RK3576
- **WHEN** 在满足 ROS、通用依赖及 RKNN SDK 的 ARM64 环境设置 `DRONE_TRACKER_PLATFORM=rk3576`
- **THEN** 可以构建和安装 ROS 节点，不查找 Jetson 硬件库

#### Scenario: 请求尚未实现的平台
- **WHEN** 设置 `DRONE_TRACKER_PLATFORM=other`
- **THEN** 首先报告请求值不受支持及支持值 jetson/rk3576，不以缺少 CUDA 作为失败原因

#### Scenario: RKNN 依赖缺失
- **WHEN** 选择 RK3576 但编译所需 RKNN 头文件或库不存在
- **THEN** 配置明确报告 RK3576、缺失项和目标版本基线，不回退 Jetson

### Requirement: RK3576 独立工厂提供视频但保留生产构建边界

RK3576 工厂 SHALL 对合法本地 MJPEG 视频请求返回可用公共帧源，不在创建时启动生产线程。创建失败 SHALL 包含平台、输入路径和原因。相机请求 SHALL 继续明确返回不支持。视频帧源 SHALL 忽略相机专属尺寸、fps、翻转、镜像配置，并返回实际视频尺寸。根目录生产构建 SHALL 支持 RK3576 的 ROS 视频入口，相机能力 SHALL 继续不开放。独立视频目标 SHALL 不引入 CUDA、TensorRT、VPI 或 Tegra 依赖。视频帧源自身 SHALL 不要求加载模型。

#### Scenario: 工厂创建视频
- **WHEN** 通过 RK3576 工厂提交合法 MJPEG 视频路径及任意相机输出尺寸
- **THEN** 返回未启动的真实帧源，尺寸查询为原视频尺寸，可通过公共接口启动及取帧

#### Scenario: 相机和根目录入口仍不开放
- 此场景保留历史标识以便规格增量匹配；本次更新后的边界为相机仍不开放，根目录视频入口开放。
- **WHEN** 从根目录构建 RK3576 节点后分别请求相机和合法 MJPEG 文件
- **THEN** 相机请求报告不支持，视频请求返回可启动的帧源

#### Scenario: 软件视频目标依赖隔离
- **WHEN** 构建和运行独立视频帧源测试
- **THEN** 不链接 Jetson 设备库，也不需要加载真实模型或提交 NPU 推理

## ADDED Requirements

### Requirement: 公共帧源可查询终止原因

系统 SHALL 提供不含硬件类型的帧源状态快照。快照表示查询时的状态，不消费帧或改变队列。RK3576 视频快照 SHALL 区分就绪、运行、主动停止、正常结尾和失败。适用的诊断 SHALL 包含路径、原因、帧序号和本轮读取、入队、满队列丢弃计数。查询 SHALL 可以与生产线程并发执行。未提供详细状态的平台 SHALL 返回未知，保持其既有取帧行为。

布尔取帧接口 SHALL 保留。普通超时 MUST NOT 被记录为正常结尾。EOF 表示文件正常结束。停止清理 SHALL 保留本轮已确定的 EOF 或错误原因；重新启动 SHALL 建立新一轮状态和统计。

#### Scenario: 长间隔仍在运行
- **WHEN** 后续视频帧尚需等待十秒且本次取帧超时
- **THEN** 查询仍报告运行，不误报结尾或错误

#### Scenario: 失败后清理
- **WHEN** 中间 JPEG 解码失败，随后执行停止清理并查询
- **THEN** 仍能获得本轮失败原因及对应路径和帧序号

#### Scenario: 状态查询不吃掉尾帧
- **WHEN** 视频已到结尾而队列仍保留尾帧，多次查询状态后再消费
- **THEN** 查询报告结尾，尾帧仍可被原消费者读取

### Requirement: 复用模型时隔离不同运行轮次

运行轮次指一次开始送帧到停止送帧的期间。系统 SHALL 在没有并发跟踪更新时提供轮次收尾能力。收尾 SHALL 撤销尚未执行的检测请求。收尾 SHALL 等待正在执行的检测及其帧访问结束。收尾返回前 SHALL 清除旧结果和旧请求持有的帧。收尾 SHALL 保留已加载模型供下一轮使用。新轮 MUST NOT 使用旧轮检测结果，即使两轮帧号相同。

轮次清理 MUST NOT 改变正常运行期间的检测调度、阈值或仲裁规则。最终销毁 SHALL 等待检测线程退出后释放其资源。设备无法确认停止访问时 SHALL 遵守既有致命失败契约，不强制释放在用内存。

#### Scenario: 正在推理时结束一轮
- **WHEN** 旧轮检测已开始但尚未完成，调用方停止送帧并请求收尾
- **THEN** 收尾等待检测和帧访问结束，返回后无可供新轮获取的旧结果，模型仍可复用

#### Scenario: 尚未执行的请求
- **WHEN** 一个旧请求尚未开始执行且调用方请求收尾
- **THEN** 请求被撤销，所持帧释放，不在新轮执行

#### Scenario: 已就绪结果与重复帧号
- **WHEN** 旧轮帧号 1 的检测结果已就绪，收尾后新轮也从帧号 1 开始
- **THEN** 新轮只能使用其自身的检测结果，不发布旧轮的框
