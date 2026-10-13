# Spec Delta

## MODIFIED Requirements

### Requirement: 上电启动与核心限制

部署脚本 SHALL 启用系统开机服务。服务和直接启动入口 SHALL 使用同一 CPU 策略文件，将主程序及继承其亲和性的子进程限制在配置允许范围内，并支持程序在该范围内按职责分配。系统 MUST NOT 在服务模板和入口脚本中分别硬编码核心范围。服务 SHALL 在异常退出后尝试重启。启用开机服务 SHALL 与实际启动节点区分。RK3576 正式相机配置 osd.enabled=true 时，节点实际启动 SHALL 自动采集并显示预览，检测跟踪及显式启用的记录会话仍等待 ON。OFF MUST NOT 被描述为释放相机或 HDMI；服务退出后才释放。

#### Scenario: 启用服务
- **WHEN** 部署脚本成功完成
- **THEN** 开机服务处于 enabled，启动入口使用已安装策略；部署未请求立即启动时不采集，实际启动持续预览节点后自动采集但不自动跟踪或录制

#### Scenario: 使用生产策略手动启动
- **WHEN** 用户通过直接启动入口使用允许 CPU 4–7 的按职责策略
- **THEN** 总范围和职责映射与服务启动一致

#### Scenario: 服务已启动但跟踪关闭
- **WHEN** 持续预览服务已启动且随后接收 OFF
- **THEN** 相机与 HDMI 仍由节点使用，显示实时预览，跟踪和记录会话关闭，桌面保持停止

## ADDED Requirements

### Requirement: 持续预览升级和回退说明明确

部署说明 SHALL 明确持续预览节点启动即采集，以及 ON/OFF 仍控制跟踪和记录。配置 SHALL 保留明确选择的模型、算法、显示及记录值。恢复旧生命周期 SHALL 先停止新版服务，再恢复匹配的旧 release、配置及启动入口。仅选择 legacy 渲染路径 MUST NOT 被称为生命周期回退。仅检查部署包 MUST NOT 开流或启动服务。

#### Scenario: 回退旧版启停行为
- **WHEN** 维护者需要恢复 ON 开流和 OFF 冻结
- **THEN** 文档指向完整旧版本恢复流程，先确认新版退出释放设备，不将 osd.render_path=legacy 作为替代

#### Scenario: 升级前仅预检
- **WHEN** 执行 deploy.sh --check
- **THEN** 不停止桌面、不启动节点、不采集，也不创建跟踪或录像会话
