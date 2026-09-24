# Spec Delta

## ADDED Requirements

### Requirement: 可选择且可回退的 OSD 渲染路径
系统 SHALL 提供启动参数 `osd.render_path`，用于选择 RK3576 HDMI OSD 的渲染实现。允许值 SHALL 为 `cached-overlay` 和 `legacy`，默认值 SHALL 为 `cached-overlay`。`cached-overlay` 表示以持久化覆盖层和缩短的视频处理链路生成输出；`legacy` 表示保留变更前的渲染实现，用于对照和现场回退。该参数 SHALL 与其他 `osd.*` 参数一样只在启动时生效，运行期修改 SHALL 明确拒绝。

#### Scenario: 默认启用优化路径
- **WHEN** 用户启用 OSD 且未设置 `osd.render_path`
- **THEN** 系统使用 `cached-overlay` 渲染路径

#### Scenario: 启动时选择旧路径
- **WHEN** 用户以 `osd.render_path=legacy` 启动节点
- **THEN** 系统使用变更前的 OSD 渲染路径，并保持已有 HDMI 输出语义

#### Scenario: 拒绝无效路径
- **WHEN** 用户以 `osd.render_path` 的未知值启动节点
- **THEN** 系统以明确的配置错误拒绝启动 OSD，而不是静默选择另一条路径

#### Scenario: 拒绝运行期切换
- **WHEN** 用户请求在节点运行期间修改 `osd.render_path`
- **THEN** 系统拒绝该修改，并继续使用启动时选定的路径

### Requirement: 优化路径保持显示语义
`cached-overlay` 路径 SHALL 遵守 `osd-presentation` 能力定义的帧与遥测严格配对、单待显示项、原子显示、OSD 关闭冻结和状态新鲜度语义。该路径 SHALL 支持已有的 `Gray8` 与 `Bgr8` 输入以及逐行步长。兼容 BGR 输入 SHALL 使用一次同步 RGA 缩放转换；灰度输入 SHALL 允许两次同步 RGA 操作以保持中性色彩。硬件无法表达的行布局 SHALL 在平台边界使用可复用缓冲重排，源读视图 SHALL 保留到同步处理结束。该回退 SHALL NOT 降低以下性能验收门槛。渲染路径选择 SHALL NOT 改变 ROS 话题、消息类型、跟踪状态数值或模型处理结果。

#### Scenario: 新视频帧和遥测配对显示
- **WHEN** 一个已消费视频帧及其目标框和跟踪状态进入 `cached-overlay` 路径
- **THEN** HDMI 的同一次可见更新显示该视频帧及与其配对的目标框和状态

#### Scenario: 仅遥测变化
- **WHEN** 没有新视频帧但已有画面的遥测显示内容发生变化
- **THEN** 系统保留当前视频画面，只更新覆盖信息，并在一次原子显示更新后呈现结果

#### Scenario: OSD 关闭后冻结
- **WHEN** OSD 从开启状态切换为关闭状态
- **THEN** 系统冻结最后一次实际显示的视频帧，并按既有 OSD 关闭规则处理覆盖信息

#### Scenario: 带填充步长的输入
- **WHEN** `cached-overlay` 路径接收行步长大于可见像素宽度的 `Gray8` 或 `Bgr8` 帧
- **THEN** HDMI 输出的裁剪、颜色和行边界正确，且不读取下一行的填充字节作为可见像素

### Requirement: 优化路径具有独立阶段诊断
当 `osd.timing_enabled=true` 且使用 `cached-overlay` 时，系统 SHALL 周期报告 `source_prepare`、`scale_convert`、`layout`、`overlay_upload`、`commit`、`flip_wait`、`render_total`、`pending_wait`、`submit_to_display` 和 `new_frame_interval`。`source_prepare` 表示从取得源帧读视图到源帧可供图像处理使用的耗时；即使该阶段不发生全帧复制，也 SHALL 产生有效样本。`legacy` 路径 SHALL 继续使用既有 `source_copy` 和既有阶段名称，便于与历史数据对照。

#### Scenario: 收集优化路径阶段数据
- **WHEN** 用户以 `osd.render_path=cached-overlay` 和 `osd.timing_enabled=true` 运行 OSD，且系统持续显示新视频帧
- **THEN** 周期日志包含所有优化路径阶段的样本数、均值和最大值，并能区分源准备与覆盖层上传耗时

#### Scenario: 关闭计时
- **WHEN** 用户以 `osd.timing_enabled=false` 启动优化路径
- **THEN** 系统不采集或输出新增的阶段计时样本

#### Scenario: 旧路径保留历史标签
- **WHEN** 用户以 `osd.render_path=legacy` 和 `osd.timing_enabled=true` 启动 OSD
- **THEN** 周期日志继续报告 `source_copy`，且不把该阶段改名为 `source_prepare`

### Requirement: 优化路径满足生产板流畅度和延迟门槛
系统 MUST 在 HZ-RK3576_WuKong Board、系统 RKNN Runtime 2.3.2、RKNPU 驱动 0.9.8、真实三个模型和固定回放输入的完整跟踪负载下满足本要求。每次测量 SHALL 运行至少 70 秒，并排除前 10 秒预热数据；`cached-overlay` 与 `legacy` SHALL 在同一板卡、同一系统设置和同一输入下各运行至少三次。稳定阶段中，`cached-overlay` 的 `render_total` 均值 SHALL 不超过 40 ms，第 95 百分位 SHALL 不超过 65.536 ms，HDMI 新视频帧显示速率 SHALL 不低于 25 fps，`submit_to_display` 均值 SHALL 不超过 60 ms。

#### Scenario: 同板 A/B 性能验收
- **WHEN** 验证人员按固定输入和相同系统设置完成三组 `legacy` 与三组 `cached-overlay` 测量
- **THEN** 报告分别列出每组稳定阶段的 `render_total` 均值和第 95 百分位、HDMI 新视频帧显示速率以及 `submit_to_display` 均值
- **THEN** 三组 `cached-overlay` 测量均满足规定门槛

#### Scenario: 保护跟踪吞吐量
- **WHEN** 比较同板、同输入的 OSD 关闭基线与 `cached-overlay` 满负载结果
- **THEN** 跟踪消费帧率下降不超过 5%，且不出现新增的持续队列积压

### Requirement: 优化路径通过持续运行和切换验收
系统 SHALL 在生产板上完成至少 20 轮 OSD 开启与关闭切换，并完成至少 10 分钟的完整跟踪满负载运行。持续运行期间 SHALL 保持帧与遥测配对正确，不得出现新增的画面撕裂、错误颜色、缓冲复用污染、死锁或进程异常退出。退出或显示设备故障 SHALL 继续遵守已有故障隔离和资源释放要求。

#### Scenario: 重复开启与关闭
- **WHEN** 验证人员在生产板上连续完成 20 轮 OSD 开启与关闭切换
- **THEN** 每轮关闭均冻结最后实际显示的视频帧，每轮恢复均能继续显示新的严格配对画面

#### Scenario: 十分钟满负载运行
- **WHEN** 生产板使用真实模型和固定输入连续运行完整跟踪与 `cached-overlay` OSD 至少 10 分钟
- **THEN** 进程持续运行，画面保持正确，且诊断中不存在持续增长的待显示积压或异常长时间无新画面

