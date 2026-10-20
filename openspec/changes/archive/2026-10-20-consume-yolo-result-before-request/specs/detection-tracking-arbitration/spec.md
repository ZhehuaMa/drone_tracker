# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

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
