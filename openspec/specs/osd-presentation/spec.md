# OSD 显示提交规格

## Purpose

让操作者在同一画面中看到互相对应的视频、目标框和跟踪状态。定义进程内提交、慢显示丢帧、共享图像安全及停止后的冻结行为，防止旧框、旧轮结果或过期视频被当作实时跟踪结果。

## Requirements

### Requirement: 图像与跟踪结果一次性配对提交

持续预览模式指 RK3576 正式 ROS 相机输入且 osd.enabled=true。系统 SHALL 使用唯一帧源消费者取得的只读帧作为 OSD 视频输入。OSD MUST NOT 单独打开相机或视频文件。OSD MUST NOT 成为帧源队列的第二个消费者。

系统 SHALL 将同次跟踪处理的帧、显示框、跟踪状态、轮次标识及轮内序号作为不可拆分的结果一次性提交。显示框表示既有跟踪输出中用于图像可视化的框，不是外推到处理时刻的预测框。跟踪结果包 SHALL 仅来自跟踪器报告已消费的帧。持续预览模式在跟踪关闭、启动和收尾阶段 SHALL 接受无框、无算法结果的预览包。正常跟踪阶段 SHALL 继续只提交配对结果。其他模式 SHALL 保持仅已消费帧产生显示提交。系统 MUST NOT 经 ROS 话题获取本地帧、框和本地跟踪状态。

#### Scenario: 相邻两帧并发提交与显示
- **WHEN** 帧 A 与结果 A 已提交，渲染读取期间帧 B 与结果 B 到达
- **THEN** 屏幕显示完整的 A 组或完整的 B 组，不出现 A 图像配 B 框或 B 状态

#### Scenario: 未消费帧
- **WHEN** 正常跟踪阶段的跟踪器因重复时间戳等既有条件返回未消费结果
- **THEN** 该帧不产生新的显示包，既有 ROS 不发布行为保持不变

#### Scenario: 未触发的预览
- **WHEN** 持续预览相机交付合格帧且尚未 ON
- **THEN** 无算法结果和目标框的图像可以提交，显示不依赖 ROS 消息

### Requirement: 无效框清除且状态含义不变

系统 SHALL 在当前包无有效显示框时清除上一包目标框。首版 SHALL 只显示现有单目标跟踪结果。系统 MUST NOT 生成算法未提供的置信度或目标 ID。系统 SHALL 保留 Detecting=0、TargetFound=1、Tracking=2、TargetLost=3 的含义。系统 SHALL 分别以白、青、绿、红表达对应状态；有效 Tracking 框 SHALL 有可辨识的锁定样式。停止、等待和输入故障 MUST NOT 被改写为算法 TargetLost。预览 SHALL 显式表示没有算法结果，MUST NOT 用默认 0 伪装 Detecting。持续预览运行阶段 SHALL 使用 TRK OFF、TRK STARTING 或 TRK ERROR，MUST NOT 新增 ROS 状态数值。MISSION 覆盖状态栏时，运行阶段 SHALL 在独立提示区域可辨认。

#### Scenario: 有框切换为无框
- **WHEN** 上一包有目标框，下一已消费包的显示框无效
- **THEN** 下一包画面不保留旧框，状态文字来自下一包实际跟踪状态

#### Scenario: 数据没有置信度
- **WHEN** 现有跟踪输出仅有框和状态
- **THEN** 显示框不出现伪造的置信度百分比或目标编号

#### Scenario: 停止时保留任务文字
- **WHEN** 持续预览已显示 Tracking 框及 MISSION 覆盖，随后 OFF
- **THEN** 全部目标装饰清除，独立区域说明跟踪关闭，MISSION 仍按下一次实际状态发布解除

### Requirement: 只读帧和有限缓冲保证资源安全

OSD SHALL 保持输入帧像素及采集时间不变。只读像素视图 SHALL 仅在其有效生命周期内被访问。OSD SHALL 在硬件读取其显示缓冲区完成后才复用或释放该缓冲区。

系统 SHALL 至多保留一份待显示结果。渲染落后时 SHALL 用最新完整结果替换尚未处理的结果。提交方 MUST NOT 等待屏幕刷新或执行显示像素复制。正在渲染的源帧 SHALL 至多为一份；冻结底图 SHALL 使用显示侧自有存储，不长期占用采集帧。

#### Scenario: 显示慢于跟踪
- **WHEN** 渲染正在处理 A，B 和 C 依次提交且均未被取出
- **THEN** 待显示槽只保留 C，B 的引用被释放，跟踪不等待 A 完成后才提交 C

#### Scenario: 同帧被异步检测持有
- **WHEN** 异步检测与 OSD 同时持有一帧，OSD 绘制框和文字
- **THEN** 检测读取到的原始像素和时间戳不变，最后一个源帧引用释放前资源仍有效

### Requirement: HDMI 中视频与框使用同一几何和显示版本

系统 SHALL 保持视频宽高比并留黑边。目标框 SHALL 使用与视频相同的最终缩放与偏移，裁剪到有效视频范围。视频、框和本地跟踪状态 SHALL 在同一个显示更新中切换。屏幕刷新频率 SHALL 与新视频画面更新频率分别报告；重复显示一帧 MUST NOT 计为新的输入画面。

#### Scenario: 默认画布显示原尺寸相机图
- **WHEN** 1920×1200 视频在 720×480 画布中使用矩形 (0,14,720,450) 显示
- **THEN** 原图中心 (960,600) 映射到 (360,239)，图像和框采用同一映射

#### Scenario: 跟踪只有每秒二十五帧
- **WHEN** 每秒有 25 个不同结果包而显示器约每秒刷新 60 次
- **THEN** 允许重复显示完整结果，诊断不将新视频更新率报告为 60 fps

### Requirement: OFF 冻结最后画面并继续显示遥测

持续预览模式 OFF SHALL 关闭旧跟踪结果接收并清掉旧轮待显示包。该模式 SHALL 保持视频流开放并提交无框预览。健康显示链路 SHALL 在 OFF 后 500 ms 内完成清框及跟踪关闭提示。预览 MUST NOT 等待推理或记录收尾，MUST NOT 因 OFF 显示输入停止。已交硬件的提交 SHALL 安全完成，未交接旧结果 MUST NOT 恢复目标显示。

非持续预览模式收到 OFF 后系统 SHALL 关闭当前轮的显示提交并清除待显示结果。显示 SHALL 保留最后实际显示的、不含旧框与旧遥测的底图。显示 SHALL 清除目标框，并显著标注“画面已冻结：输入已停止”。健康显示链路 SHALL 在收到停止事件后 500 ms 内完成冻结提示。最新有效遥测 SHALL 在冻结画面上继续更新。

非持续预览模式已交给硬件的提交 SHALL 安全完成后再冻结；未交给硬件的旧轮结果 MUST NOT 在停止后重新开启实时显示。没有任何已显示帧时 SHALL 使用黑底和停止提示。冻结图像的帧时间 MUST NOT 被系统当前时间覆盖。

#### Scenario: 正常跟踪后停止
- **WHEN** 非持续预览模式图像 A 已显示且带有目标框，随后收到 OFF，并继续收到新电池数据
- **THEN** 保留最后实际显示底图，清框并出现中文冻结提示，电池数值继续更新且帧时间不变

#### Scenario: 首帧之前停止
- **WHEN** 非持续预览模式还没有任何已显示帧便收到 OFF
- **THEN** 黑色背景上显示输入已停止，不显示伪造图像或目标框

#### Scenario: 相机 OFF 后有新画面
- **WHEN** 持续预览 OFF 后相机交付 B、C，电池继续更新
- **THEN** HDMI 显示无框 B、C 和新电池，没有输入停止提示

#### Scenario: 预热中 OFF
- **WHEN** 持续预览尚无合格图像时 OFF
- **THEN** 黑底等待与遥测继续，相机不停止预热，合格帧到达后开始预览

### Requirement: 新轮与等待状态不误用旧结果

非持续预览模式接受 ON 并开始新轮时 SHALL 建立新的显示轮次。运行中的重复 ON SHALL 保持现有轮次和显示状态。旧轮包 MUST NOT 在新轮被接受。非持续预览模式新轮第一份完整包成功显示前，旧底图 SHALL 保持冻结标记且无目标框。首份新包成功显示后 SHALL 移除冻结提示。

持续预览 SHALL 分别使用视频流身份与跟踪轮次。ON/OFF MUST NOT 重置流序号或关闭视频接收。启动阶段 SHALL 继续预览；正常运行 SHALL 只接收新分发边界之后的配对结果。旧轮结果 MUST NOT 被接受。跟踪异常 SHALL 回到预览；相机故障 SHALL 按服务失败退出，恢复 MUST NOT 依赖 ON。

没有新包超过 `osd.frame_stale_ms` 时，系统 SHALL 清除框并显示等待新帧，默认期限为 1000 ms。普通等待 MUST NOT 被认定为 EOF。非持续预览模式的 EOF 和输入/处理失败 SHALL 使用冻结底图并分别说明播放结束或故障；再次 ON SHALL 遵守旧轮隔离规则。持续预览普通处理等待 SHALL 清框并提示等待，不用旧框覆盖最新原图或伪造输入失败。

#### Scenario: 停止时推理尚未结束
- **WHEN** 非持续预览模式 OFF 后旧轮处理完成并尝试提交，随后新轮开始
- **THEN** 旧包被拒绝，新轮首包显示前画面保持冻结且无旧框

#### Scenario: 合法长帧间隔
- **WHEN** 帧源仍运行，但超过配置期限没有新结果包
- **THEN** 屏幕显示等待新帧并清框，帧源继续按原逻辑等待，后续当前轮完整包可恢复显示

#### Scenario: 运行中重复 ON
- **WHEN** 当前轮正在显示配对结果且再次收到 ON
- **THEN** 不建立第二个显示轮次，不清除当前框或增加工作线程

#### Scenario: 相机新轮独立
- **WHEN** 持续预览 OFF 后旧处理尝试提交，随后再次 ON
- **THEN** 旧结果被拒绝，相机不重开，启动阶段预览继续，新轮从新分发边界接收任务

### Requirement: OSD 不改变既有业务输出

启用 OSD SHALL 保持现有 ROS 话题名称、消息类型、QoS、框与图像时间语义及状态发布条件。`visualized_result` SHALL 继续只控制既有 ROS 调试图像。OSD SHALL 不修改跟踪算法、模型契约、目标类别或传感器采集模式。持续预览只改变相机服务启停边界，MUST NOT 将预览帧发布到 /fpv/tracker_image。该话题 SHALL 仍仅在 visualized_result=true 且当前轮次已消费图像时按原规则发布。OFF 后 MUST NOT 为旧轮启动新的结果发布；已交 ROS 中间件的消息允许延迟交付。

#### Scenario: 关闭 ROS 调试图像
- **WHEN** `visualized_result=false` 且 `osd.enabled=true`
- **THEN** 不发布 ROS 调试图像，HDMI 仍可显示完整配对画面，原框和状态话题保持原规则

#### Scenario: 调试图像开启但仅预览
- **WHEN** 持续预览 visualized_result=true，尚未 ON 或已经 OFF
- **THEN** HDMI 更新，不产生新跟踪图像、检测框或算法状态消息

### Requirement: 任务触发覆盖 HUD 跟踪状态文字

HUD 状态栏 SHALL 在收到 `/fpv/tracker_trigger` 的 `frame_id="MISSION"` 后显示 `ON MISSION`。此处的任务触发表示开始执行拦截任务，不是 `TrackerState` 的新数值。系统 SHALL 持续显示 `ON MISSION`，直到本节点下一次向 `/fpv/tracker_state` 发布新的跟踪状态值；随后 SHALL 显示该值对应的跟踪状态文字。状态值 `3` SHALL 继续表示 `TargetLost`，MUST NOT 解释为任务阶段。

任务触发 SHALL 只覆盖状态栏文字，不得修改与视频帧配对的跟踪状态、目标框颜色和锁定样式。收到 `ON`、`OFF` 或其他非 `MISSION` 触发值 SHALL 不启动任务文字覆盖。两条 HDMI 渲染路径 SHALL 显示相同的任务文字。

#### Scenario: 跟踪期间进入任务
- **WHEN** 状态栏显示 `TRK TRACKING`，随后收到 `frame_id="MISSION"`，且没有新的 `/fpv/tracker_state` 发布
- **THEN** 状态栏持续显示 `ON MISSION`，即使两秒触发提示结束或后续视频帧到达

#### Scenario: 新跟踪状态结束任务覆盖
- **WHEN** 状态栏显示 `ON MISSION`，随后本节点向 `/fpv/tracker_state` 发布 `3`
- **THEN** 状态栏显示 `TARGET LOST`，目标丢失仍按状态 `3` 的既有红色规则绘制

#### Scenario: 非任务触发不覆盖状态栏
- **WHEN** 已显示 `TRK TRACKING`，随后收到 `frame_id="ON"`、`frame_id="OFF"` 或其他字符串
- **THEN** 状态栏不因这些触发值显示 `ON MISSION`

### Requirement: HDMI 状态切换诊断日志

系统 SHALL 在接收触发消息、发布变化后的跟踪状态、置位或解除任务覆盖，以及请求结束一轮跟踪或视频流时记录事件日志。任务覆盖指任务触发后，右上角暂以 `ON MISSION` 代替跟踪状态文字的既有行为。系统 SHALL 在 HDMI 成功提交的画面中，状态栏文字或显示状态发生变化时记录实际提交结果。

目标框旁的标签已从可见画面移除。系统 SHALL 保留显示提交日志中的 `target_label` 字段，并 SHALL 始终将该字段输出为 `target_label=""`，表示没有可见目标标签。系统 MUST NOT 将输入的目标名称或回退跟踪状态记录为已显示的目标标签。系统 MUST NOT 仅因输入标签变化而新增显示切换日志。

日志 SHALL 包含轮次及足以关联画面的帧序号。持续预览 SHALL 另记流身份、原图序号及包类型。停止日志 SHALL 区分跟踪关闭仍有视频与输入停止冻结，并报告清框结果。稳定视频帧 SHALL 不逐帧输出相同的诊断日志。

#### Scenario: 跟踪中收到任务触发
- **WHEN** 跟踪中收到 `frame_id="MISSION"`
- **THEN** 日志能区分触发到达、任务覆盖置位、状态发布解除覆盖和最终提交的右上角文字
- **THEN** 显示提交日志的 `target_label` 字段保持为空

#### Scenario: 跟踪中收到停止触发
- **WHEN** 非持续预览模式跟踪中收到 `frame_id="OFF"`
- **THEN** 日志能关联停止请求、显示状态变为 `Stopped`，以及最后视频底图冻结和目标框清除后的提交结果
- **THEN** 停止后的显示提交日志继续输出 `target_label=""`

#### Scenario: 输入含标签但画面没有标签
- **WHEN** 当前成功提交画面携带有效 Tracking 框，输入标签为非空目标名称或空字符串
- **THEN** 右上角仍按当前状态或任务覆盖显示，日志均输出 `target_label=""`

#### Scenario: 仅输入标签改变
- **WHEN** 视频继续提交，状态栏文字、显示状态、框几何及其他诊断触发条件均未改变，仅输入标签从 `TRACKING` 改为另一个名称
- **THEN** 不因标签变化新增显示切换日志，也不改变可见目标装饰

#### Scenario: 持续预览停止日志
- **WHEN** 持续预览 OFF 后新图像显示
- **THEN** 日志区分请求接受、清框提交及清理完成，记录同一流与新序号，target_label 为空
