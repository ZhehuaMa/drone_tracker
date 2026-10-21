# rk3576-continuous-hdmi-stream Specification

## Purpose

使 RK3576 正式相机服务在健康运行期间持续向 HDMI 输出视频与 OSD，即叠加的状态和遥测信息。该能力明确视频流、跟踪和记录的独立控制边界，避免停止算法时失去预览，也避免旧轮结果或仍被设备访问的资源进入新的运行会话。

## Requirements

### Requirement: 持续预览模式具有明确适用范围

系统 SHALL 仅在 RK3576 正式 ROS 相机输入且 `osd.enabled=true` 时启用持续预览模式。系统 SHALL 保留未启用 OSD 的相机、Jetson、有限视频文件及独立采集工具的原启停行为。`osd.enabled` SHALL 仍为启动参数。系统 MUST NOT 为实现持续预览要求新增配置开关。

#### Scenario: 生产相机使用现有配置
- **WHEN** 合法 SC233HGS 相机配置启用 `osd.enabled=true`，且没有发送触发消息
- **THEN** 服务启动采集和 HDMI 预览，检测跟踪及记录会话保持关闭

#### Scenario: 文件输入保持原行为
- **WHEN** 正式 ROS 输入为合法有限 MJPEG 文件且启用 OSD
- **THEN** 文件仍等待 ON 才播放，OFF 结束本轮，EOF 不自动循环

#### Scenario: 相机关闭 OSD
- **WHEN** 相机输入使用 `osd.enabled=false`
- **THEN** 相机仍由原 ON/OFF 轮次控制，且不打开 HDMI

### Requirement: 服务生命周期控制视频流

持续预览模式 SHALL 在节点参数、模型、消息接口和显示初始化成功后自动启动相机。首张合格帧之前 SHALL 显示黑底、等待输入及可用 OSD。健康视频流 MUST NOT 因 ON、OFF 或 MISSION 开启、关闭或重新预热。服务最终退出 SHALL 停止采集并释放显示资源。

#### Scenario: 等待首次 ON
- **WHEN** 服务已得到合格相机帧，但操作者十秒内未发送 ON
- **THEN** HDMI 持续显示不同图像及新鲜遥测，没有目标框、检测结果消息或新录像文件

#### Scenario: 预热中收到 OFF
- **WHEN** SC233HGS 尚在合法启动预热期间收到 OFF
- **THEN** 跟踪和记录请求关闭，相机继续原预热，合格帧到达后显示预览

### Requirement: OFF 后预览不等待算法或记录收尾

持续预览模式收到 OFF SHALL 关闭当前跟踪结果接收并清除目标装饰。健康显示链路 SHALL 在接收 OFF 后 500 ms 内完成清框和跟踪关闭提示。健康相机的预览取帧 MUST NOT 等待在途推理结束、录像准备或记录收尾。系统 MUST NOT 因 OFF 显示输入已停止。已经交给显示硬件的更新 SHALL 安全完成。

#### Scenario: 推理尚未返回
- **WHEN** 一张图像正在受控阻塞的推理中，收到 OFF 后相机继续出帧
- **THEN** 推理尚未返回时 HDMI 已清框并继续显示递增预览帧，旧推理结果之后被拒绝

#### Scenario: 录像收尾较慢
- **WHEN** OFF 后录像工作进程仍在其既有收尾预算内等待存储
- **THEN** 相机和无框 HDMI 预览继续更新，记录结果分别报告

### Requirement: 唯一取帧者和有界分发

相机原队列 SHALL 保持单一生产者和单一消费者。显示与跟踪 SHALL 共享只读帧引用，MUST NOT 各自抢取原队列或重新打开相机。新增待跟踪任务 SHALL 至多一份，忙时 SHALL 由最新任务替换尚未执行的任务并计数。待显示包 SHALL 至多一份。上述替换 MUST NOT 改写像素或采集时间。

#### Scenario: 跟踪落后于相机
- **WHEN** 跟踪正在处理 A，B 和 C 先后进入待跟踪位置
- **THEN** 待跟踪位置仅保留 C，B 释放且替换计数增加，A 的有效引用保持至处理结束

### Requirement: 视频与算法结果采用明确的配对模式

跟踪关闭、启动和收尾阶段 SHALL 输出无目标框的预览。正常运行阶段 SHALL 只显示当前轮次实际消费的图像及其同帧显示框和状态。系统 MUST NOT 把旧框覆盖到更新的预览帧。正常运行阶段的新画面速率 SHALL 按实际配对帧测量，MUST NOT 宣称必然等于采集帧率。本能力不要求推理长时间阻塞时自动回退预览。

#### Scenario: 采集六十帧而处理二十五帧
- **WHEN** 相机实际每秒采集 60 帧，当前轮次实际处理并提交 25 帧
- **THEN** HDMI 显示配对画面，统计分别报告采集、跟踪和实际显示新帧率，不把名义刷新率当作 60 个新画面

#### Scenario: 运行中暂无处理结果
- **WHEN** 跟踪仍开启但超过 `osd.frame_stale_ms` 没有新的完整结果
- **THEN** HDMI 清框并显示等待提示，不借用旧框给新图像，也不伪造关闭或 TargetLost 状态

### Requirement: 新轮任务和输出与旧轮隔离

系统 SHALL 分别标识视频流会话和跟踪轮次。新跟踪轮次 SHALL 从新的分发边界接收图像，MUST NOT 使用关闭期间的旧任务。OFF 后的旧轮帧结果 MUST NOT 发起新的 ROS 发布或 HDMI 目标显示。系统 SHALL 允许按 target-observations 为该轮发送一次显式无效 bbox/LOS 控制通知；该通知不是旧推理结果，也不产生目标装饰。重复 OFF SHALL 去重。通知 SHALL 在 bag 封存前尝试提交；控制路径 MUST NOT 等待推理或磁盘收尾。旧设备访问结束前 MUST NOT 重置或释放其资源。已经交给 ROS 中间件的消息 SHALL 保留既有延迟交付语义。

#### Scenario: 旧请求完成于新轮之后
- **WHEN** OFF 使轮次 1 失效，系统等待其访问结束后启动轮次 2
- **THEN** 轮次 2 不消费轮次 1 的请求或结果，算法模型跨轮复用

#### Scenario: 从预览进入新轮
- **WHEN** 预览已显示流内序号 100，随后新轮在分发边界就绪
- **THEN** 新轮从边界之后取得新任务，不用旧启动结果使 HDMI 回到序号 99

#### Scenario: 失效通知与迟到推理竞争
- **WHEN** OFF 关闭一轮的正常发布接收并发出无效 bbox/LOS，旧推理随后返回
- **THEN** 没有后续旧轮有效观测或目标装饰；无效通知不会停止 HDMI 视频，也不会触发新的 ROS 图像

### Requirement: 重复及快速启停不累积资源

重复 ON SHALL 保持当前运行轮次。重复 OFF SHALL 安全。停止中的控制 SHALL 仅保留最后的期望启停状态。最后期望为 ON 时 SHALL 等旧模型访问结束后创建一个新轮。ROS 控制接收 MUST NOT 执行推理等待或记录准备与收尾。预览、遥测及记录错误报告 SHALL 保持响应。

#### Scenario: 停止中多次改变期望状态
- **WHEN** 轮次正在收尾，依次接收 ON、OFF、ON
- **THEN** 不累积三个轮次，旧访问结束后只启动一个新轮，收尾期间预览继续

### Requirement: 记录与 ROS 调试图像保持业务边界

原画面录像 SHALL 仅由 `recording.enabled` 和 ON/OFF 会话控制。ROS bag SHALL 保持 `need_save` 的现有轮次规则，并按 rk3576-ros-video 记录姿态、里程计、浮点框、LOS 和 MAVROS IMU。所有 visualized_result 设置下，bag MUST NOT 包含 `/fpv/tracker_image` 消息或话题元数据。服务自动预览 MUST NOT 创建上述记录。`/fpv/tracker_image` SHALL 继续只在 `visualized_result=true` 且当前轮次实际消费图像时发布。预览 MUST NOT 产生该话题消息或其 bag 数据。

#### Scenario: 开启调试图像但未触发
- **WHEN** 持续预览配置 `visualized_result=true`、`need_save=true`、`recording.enabled=true`，服务启动后尚未收到 ON
- **THEN** HDMI 有视频，但没有跟踪图像消息、bag 会话或原画面录像会话

#### Scenario: 结束一轮后继续预览
- **WHEN** ON 后记录和调试图像正常，随后接收 OFF
- **THEN** 当前记录收尾且停止新的跟踪图像发布，HDMI 预览继续

#### Scenario: 调试图像不等于消息录像
- **WHEN** ON 有效轮次同时设置 visualized_result=true、need_save=true、recording.enabled=true
- **THEN** 可实时发布调试图像并独立录制原画面，bag 记录规定的非图像话题；OFF 后三者按各自会话边界收尾，HDMI 继续预览

### Requirement: 故障按资源边界隔离

相机首次开流失败或运行失败 SHALL 安全清理并使节点非零退出。相机恢复 MUST NOT 依赖 ON；部署服务 SHALL 沿用异常重启策略。跟踪处理异常 SHALL 结束该跟踪轮次，健康相机继续预览。录像、bag 和显示故障 SHALL 遵守各自既有隔离规则。最终退出 SHALL 等待设备访问安全结束，重复清理 SHALL 安全。

#### Scenario: 未触发时相机失败
- **WHEN** 正在预览且从未发送 ON，相机报告采集故障
- **THEN** 保留故障原因、清理采集与显示并非零退出，不等待 ON 才处理故障

#### Scenario: 跟踪处理失败
- **WHEN** 模拟跟踪处理抛出异常而相机保持健康
- **THEN** 本轮失效并显示跟踪错误，持续预览继续，旧访问清理后允许下一次 ON

### Requirement: 定向验收具有统一时长和证据边界

持续预览模式 SHALL 使用本要求替代关联规格的重复长测时长要求；其他模式原验收不变。生产验收 SHALL 在指定 HZ-RK3576_WuKong Board 完成至少 20 轮 ON/OFF，以及至少 60 秒连续跟踪、录像与 HDMI 联合有效负载。每次性能或负载运行 MUST NOT 超过 180 秒。报告 SHALL 区分模拟、容器和生产证据，MUST NOT 将短测拼接成历史长测通过。

#### Scenario: 完成定向联合检查
- **WHEN** cached-overlay 的同进程短测覆盖未 ON 预览至少十秒、20 轮启停、联合有效负载至少六十秒和最终 OFF 预览至少十秒，且全程不超过 180 秒
- **THEN** 记录流/轮次身份、首帧时间、清框时间、帧率、队列替换、录像完成和内存/线程/文件描述符变化；相机没有因轮次切换重开

#### Scenario: 路径与基线对照
- **WHEN** 同板同相机同模型再执行 legacy 行为检查及 OSD 关闭的 ON 有效负载基线，每次不超过 180 秒
- **THEN** 两路径的启停与配对行为一致；cached-overlay 的 ON 有效窗口核对既有渲染和吞吐门槛，报告可比窗口及差值，缺少可比负载时保留未完成

#### Scenario: 生产证据尚缺
- **WHEN** 自动测试和 ARM64 构建通过，但生产板或 HDMI 不可用
- **THEN** 生产生命周期、资源和性能检查保持未完成，不声称完整验收通过
