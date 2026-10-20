# detection-tracking-arbitration Specification

## Purpose

Keep delayed detections associated with the frame they describe, so image-history eviction does not create an invalid comparison or pretend that recovery was performed. Keep queued detector input current while preserving bounded concurrency and existing watchdog behavior.

## Requirements

### Requirement: Source comparison data survives image eviction

来源元数据仅包含帧身份、采集时间、来源跟踪框、来源卡尔曼预测框及其有效标记。未处置请求包括已接受待执行、正在执行和等待结果处置的请求。系统 SHALL 保留该请求的来源元数据直至处置。元数据 MUST NOT 持有图像资源。

#### Scenario: Original image has left history
- **WHEN** 帧 744 的请求 62 尚未处置，而历史图像已推进到帧 748–777
- **THEN** 帧 744 的元数据仍可用于结果仲裁，且该元数据不会使帧 744 的图像继续存活

### Requirement: Arbitration compares boxes from the same source frame

正检测结果表示检测到了目标。系统 SHALL 仅将正检测结果与同一请求来源帧、同一跟踪回合的有效比较框进行比较。来源框缺失时 MUST NOT 用当前帧或无关前一帧的框替代。

#### Scenario: Current position differs from source position
- **WHEN** 延迟检测框与来源跟踪框的交并比为 0.584，而与当前框的交并比为 0；交并比是两框交集面积除以并集面积
- **THEN** 仲裁使用 0.584 进行重叠测试，并记录来源帧身份

#### Scenario: Neither comparison basis exists
- **WHEN** 正检测结果既没有有效来源跟踪框，也没有有效来源预测框
- **THEN** 仲裁记录比较上下文不可用，不伪造零重叠测量或跳变判断

### Requirement: Missing context is neutral for unconfirmed-result counting

未确认计数记录尚未确认当前跟踪的可用检测证据。缺少必要来源比较上下文的正检测结果 SHALL 保持该计数不变。仅缺少上下文 MUST NOT 确认跟踪或触发目标丢失状态转换。

#### Scenario: Missing metadata at four unconfirmed results
- **WHEN** 计数为 4，且正检测结果因缺少来源上下文而无法比较
- **THEN** 计数保持为 4，仅此缺失既不清除跟踪，也不将计数清零

### Requirement: Partial comparison data has explicit eligibility

Tracking 表示正在跟踪。IoU 是两个框的交集面积除以并集面积，用于判断框是否一致。Tracking 状态中的 IoU 仲裁 SHALL 要求有效的来源跟踪框。仅有来源预测框 MUST NOT 确认跟踪。仅缺少来源预测框时，系统 SHALL 保留原有的仅使用跟踪框的跳变比较。

#### Scenario: Prediction without a tracker observation
- **WHEN** Tracking 状态收到正检测结果，来源预测框有效，但没有来源跟踪观测
- **THEN** 该结果不能建立 IoU 一致性，未确认计数保持不变

#### Scenario: Tracker observation without prediction
- **WHEN** 正检测结果有有效来源跟踪框，但没有来源预测框
- **THEN** 仍可使用来源 IoU 及原有的仅使用跟踪框的跳变检查

### Requirement: Recovery requires source images

恢复是在检测来源图像上初始化跟踪器，再重放后续缓存图像。系统 SHALL 在尝试恢复前要求来源图像存在。缺少像素 MUST NOT 被记录为已尝试初始化或初始化成功。

#### Scenario: Metadata exists but recovery image is gone
- **WHEN** 正检测结果有有效来源元数据，但来源图像已被淘汰
- **THEN** 历史比较仍可用，恢复被记录为不可用，且不会改用另一图像尝试初始化

#### Scenario: Detecting cannot initialize from metadata
- **WHEN** 系统处于 Detecting 检测状态，收到的正检测结果缺少来源图像
- **THEN** 系统保持 Detecting，并继续提供合格的检测输入

### Requirement: Usable evidence retains existing arbitration thresholds

来源上下文有效时，系统 SHALL 保留已配置的重叠及跳变阈值。真实不一致及已完成的无目标检测 SHALL 保留既有未确认计数处理。仅缺少图像 MUST NOT 增加该计数。

#### Scenario: Metadata-only agreement
- **WHEN** NanoTrack 跟踪正常，来源比较满足原有一致性规则，且仅缺少来源像素
- **THEN** 跟踪按原有一致性规则继续，无需初始化

#### Scenario: Genuine disagreement without recovery pixels
- **WHEN** 来源元数据证明重叠低于重置阈值，没有优先执行的跳变分支，且来源图像不存在
- **THEN** 原有的低重叠未确认计数增量仍发生，并单独记录恢复不可用

#### Scenario: Fifth completed no-target result
- **WHEN** 连续五个已完成的无目标结果达到配置为 5 的丢失上限
- **THEN** 系统按既有丢失规则清除跟踪，并输出 TargetLost 状态

### Requirement: Unavailable recovery cannot become successful jump acceptance

检测跳变是既有来源帧检查识别出的位置或尺寸变化。接受重复跳变需要恢复但来源图像缺失时，系统 MUST NOT 报告恢复成功，也 MUST NOT 像恢复成功一样清除未确认计数。

#### Scenario: Jump tolerance reached without images
- **WHEN** 可进行来源比较的检测跳变反复出现并达到接受阈值，但来源图像不可用
- **THEN** 恢复仍不可用，结果仍未确认，且原有丢失上限继续适用

### Requirement: New eligible input replaces only an unstarted request

合格输入是在既有提交时机通过既有校验的输入。系统 SHALL 用更新的合格输入替换已占用待执行槽的请求。系统 SHALL 最多保留一个待执行请求。替换 MUST NOT 修改正在执行的请求，也 MUST NOT 覆盖尚未读取的已完成结果。

#### Scenario: Latest pending input wins
- **WHEN** A 正在执行，B 待执行，且更新的合格输入 C 提交时没有未读结果阻止接收
- **THEN** B 被替换，C 成为待执行请求，A 继续使用原有输入

#### Scenario: Invalid replacement preserves useful work
- **WHEN** B 待执行，而拟替换它的 C 未通过输入校验
- **THEN** C 被拒绝，B 继续待执行

#### Scenario: Completed result stays protected
- **WHEN** 已完成结果尚未读取，新提交触发既有未读结果接收规则
- **THEN** 已完成结果仍可读取，接收过程保留该保护

### Requirement: Request replacement preserves its input association

每个已接受请求 SHALL 保持图像、采集时间、来源身份、搜索区域与元数据的关联一致。工作线程 SHALL 领取完整请求。被替换请求 SHALL 有明确的终态处置记录。

#### Scenario: Replacement races with worker pickup
- **WHEN** C 替换待执行 B 与工作线程领取 B 并发发生
- **THEN** 工作线程获得完整 B 或完整 C，不混用输入；每个被替换请求恰好记账一次

### Requirement: Submission cadence and inference search stay unchanged

系统 SHALL 在 Detecting 状态保留每个处理帧的提交机会，在 Tracking 状态保留已配置的处理帧间隔。跟踪路径 SHALL 在当前帧真实跟踪观测及来源快照建立后，再读取已完成结果并判定是否提交。未到提交间隔的正常更新 SHALL 仍读取并处置结果。系统 SHALL 保留既有区域、全图及分块搜索级联。替换 MUST NOT 增加推理工作线程，也 MUST NOT 等待输入停止变化才开始执行。

#### Scenario: Continuous pending replacement
- **WHEN** 正在执行的请求结束时，新的合格帧仍持续到达
- **THEN** 工作线程无需等待输入静止，即可领取当前待执行输入

#### Scenario: Tracking reads on an update without submission
- **WHEN** Tracking 路径的当前处理帧未到配置提交间隔，但已有结果 A 完成
- **THEN** 系统读取并处置 A，记录原有间隔跳过，不为清空结果增加一次提交

#### Scenario: Current source observation precedes delayed arbitration
- **WHEN** Tracking 路径获得当前帧 B 的实际跟踪框，且旧结果 A 可能触发恢复
- **THEN** B 的请求仍绑定恢复前的 B 来源观测及原有搜索区域，不把 A 的旧来源观测或恢复后的状态写成 B 的来源

#### Scenario: Existing early return does not consume output
- **WHEN** 输入无效、时间重复，或持续原生跟踪跳变使更新在正常检测交互点之前按既有规则提前返回
- **THEN** 该更新不新增结果读取或提交，不改变原有提前返回及清理行为

### Requirement: Round cleanup releases metadata and images safely

跟踪回合是一次 ON 到 OFF 的处理周期。回合清理 SHALL 取消待执行请求，并等待执行中的图像访问结束。清理 SHALL 在复用前清除旧结果及保留的元数据。帧号重复时，结果 MUST NOT 跨回合生效。

#### Scenario: OFF during execution and replacement
- **WHEN** OFF 时 A 正在执行、C 待执行，随后新回合从帧 1 开始
- **THEN** C 被取消，A 的资源访问在清理返回前结束，旧元数据被释放，旧结果不能影响新回合的帧 1

### Requirement: Watchdog counting and reset behavior remain unchanged

看门狗计数表示跟踪器已初始化但未读到新检测结果的更新次数。系统 SHALL 仅在既有无结果分支增加该计数。跟踪分支读取任何结果后 SHALL 在仲裁前将其清零，包括来源上下文不可用的结果。

#### Scenario: Unusable reply still resets the current watchdog
- **WHEN** 无结果计数为 60，且读取到来源元数据不可用的正检测结果
- **THEN** 尽管该结果不能确认跟踪，看门狗计数仍变为 0

### Requirement: Watchdog threshold and timeout action remain unchanged

看门狗 SHALL 保留严格条件 `counter > lost_max_count * yolo_interval * 3`。超时 SHALL 清除跟踪初始化状态及框，将计数清零，并在下一个处理帧恢复 Detecting。该路径 MUST NOT 增加按实际时间计量的上限、工作线程重启或额外的 TargetLost 转换。

#### Scenario: Existing sixty-update boundary
- **WHEN** `lost_max_count` 为 5、`yolo_interval` 为 4，且连续 61 次已初始化跟踪更新都未读取到结果
- **THEN** 第 60 次更新不超时，第 61 次执行既有超时动作，下一个处理帧进入 Detecting

### Requirement: Result age remains observational

结果年龄是在一致时间基准下，从来源采集到结果消费的经过时间。系统 SHALL 保留采集时间，并记录可获得的年龄证据。本变更 MUST NOT 增加最大结果年龄拒绝规则。

#### Scenario: Old result with usable context
- **WHEN** 结果已经较旧，但属于当前回合且具有有效来源比较上下文
- **THEN** 不会仅因经过时间较长而拒绝该结果

### Requirement: External state meanings and recovery acceptance remain compatible

系统 SHALL 保留 Detecting=0、TargetFound=1、Tracking=2、TargetLost=3。除明确规定的缺失上下文处理外，系统 SHALL 保留 ROS 接口、常规丢失状态转换及同步恢复接受规则。

#### Scenario: Initialization and normal tracking
- **WHEN** 可用正检测结果具有来源图像，且初始化按既有恢复接受规则成功
- **THEN** 输出进入 TargetFound，下一个正常的已初始化更新进入 Tracking

#### Scenario: Partial replay retains its existing rule
- **WHEN** 初始化成功，部分重放步骤失败，但既有恢复规则仍接受该操作
- **THEN** 本变更不会悄悄将该规则替换为所有步骤必须成功

#### Scenario: Native tracker jump retains its existing reset path
- **WHEN** 连续 NanoTrack 跳变达到既有容忍上限
- **THEN** 保留原有提前返回的重置路径，不额外发布 TargetLost

### Requirement: Completed result is taken before current submission

结果快照表示从已完成结果槽取走并保存在当前更新中的一份结果。系统 SHALL 在检测路径及跟踪路径的正常检测交互点先尝试取走一次结果，再进行当前帧的提交判定。已经取走的旧结果 MUST NOT 继续以未读状态阻止本次提交。其他既有接收限制 SHALL 继续适用。

#### Scenario: Ready negative result does not block current input
- **WHEN** 检测路径进入正常交互点时已有无目标结果 A，当前帧 B 有效，实例可接收请求，且读取至提交之间没有另一结果完成
- **THEN** 系统先取走 A，再接受 B，不因 A 仍未读而拒收 B，随后按既有规则处置 A

#### Scenario: Ready positive result is saved before submission
- **WHEN** 检测路径已有带来源身份的正检测结果 A，当前帧 B 具有提交机会
- **THEN** 系统先保存 A，再对 B 执行原接收规则，处理 A 时仍使用 A 的来源身份和检测框

#### Scenario: No ready result does not wait for inference
- **WHEN** 正常交互点没有已完成结果，工作线程仍在推理
- **THEN** 读取立即返回无结果，当前帧按原节奏参与提交，不等待工作线程完成

### Requirement: Current submission precedes saved-result arbitration

结果仲裁是根据已取走结果进行跟踪确认、丢失判断或恢复的过程。系统 SHALL 在处理结果快照所需的历史查找、初始化和补跟踪之前，完成当前帧的既有提交尝试或间隔跳过判定。提交被拒绝 MUST NOT 丢弃已经取走的结果快照。

#### Scenario: Expensive catch-up follows current request
- **WHEN** 结果 A 需要在来源帧初始化并补跟踪，且当前帧 B 到达既有提交时机
- **THEN** B 的请求判定先于 A 的历史查找及初始化开始；处理 A 不会把 B 的提交推迟到补跟踪结束

#### Scenario: Rejected current request preserves saved output
- **WHEN** 已保存结果 A，而当前帧 B 按既有接收规则被拒绝
- **THEN** 系统仍对 A 执行一次既有处置，不用 B 的拒绝覆盖 A 的身份或结果

### Requirement: Saved result has one disposition per update

系统 SHALL 保持结果快照的请求身份、来源帧、采集时间、来源元数据及回合身份一致。每个正常交互点 SHALL 只读取一次结果，并只处置该次读取所得快照。读取成功与否 SHALL 驱动原有的结果分支或无结果分支；系统 MUST NOT 为补取本次提交后才完成的结果而进行第二次读取。

#### Scenario: New result cannot replace local snapshot
- **WHEN** A 已被取走，随后工作线程完成结果 B
- **THEN** 当前交互点只处置 A，B 不替换局部快照；B 的未读保护及后续结果处置仍遵循原规则，A 不被处理两次

#### Scenario: Original provenance and cleanup rules remain applicable
- **WHEN** 保存的正结果属于旧回合，或当前回合结果缺少恢复所需的来源图像
- **THEN** 系统按既有回合校验及恢复资格处理该快照，不改用当前帧、不跨回合初始化，也不伪造恢复成功

### Requirement: Completion between read and submission stays protected

读取快照与提交仍是两个独立操作。系统 SHALL 对读取后才发布的已完成结果保留未读保护。系统 MUST NOT 为保证当前帧接收而清空、覆盖或忽略该新结果。未读结果造成的合法拒收 SHALL 保留原原因及关联身份。

#### Scenario: Result arrives after empty snapshot
- **WHEN** 读取返回无结果，工作线程随后完成 B，且 B 在当前帧 C 提交前成为未读结果
- **THEN** C 可以按既有规则被拒绝并记录 `UnreadResult`，该次提交不改写 B 的内容；后续结果处置保留原规则，当前更新不进行第二次读取

#### Scenario: Pending work can still replace a briefly visible frame
- **WHEN** 帧 389 成功进入待执行槽，但工作线程尚在处理帧 388，随后更新的合格帧 390 到达且没有未读结果阻止接收
- **THEN** 390 仍按既有最新待执行规则替换 389，并记录替换身份；调用顺序变化不保证 389 进入推理
