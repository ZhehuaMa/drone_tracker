# production-binary-deployment Specification

## Purpose

为安装好依赖的 RK3576 生产板提供可复制的无人机跟踪程序部署包。部署保持源板配置与二进制内容一致，并提供固定 CPU 核心的上电自启动能力，避免人工复制遗漏运行文件或误覆盖系统依赖。

## Requirements

### Requirement: 启动前关闭桌面

部署提供的启动入口 SHALL 在每次启动节点前同步停止桌面服务。OSD 启用时，启动入口 SHALL 确认目标显示设备的内核客户端列表为空。客户端列表表示当前打开显示设备的进程。停止失败、无法读取占用信息或等待 5 秒后仍有占用时，启动入口 SHALL 返回非零并阻止节点启动。开机服务 SHALL 排在桌面启动任务之后执行关闭动作。节点退出后启动入口 SHALL 不自动恢复桌面。

#### Scenario: 桌面正在运行
- **WHEN** 桌面正在占用显示设备且用户启动部署服务
- **THEN** 先停止桌面，确认显示设备释放后才运行节点

#### Scenario: 显示设备仍被占用
- **WHEN** 桌面停止后 5 秒内显示设备仍被其他进程占用
- **THEN** 报告占用并退出，不启动节点

#### Scenario: 只检查部署包
- **WHEN** 用户执行 `deploy.sh --check`
- **THEN** 不停止桌面、不启动节点

### Requirement: 复制生产运行文件

部署包 SHALL 包含生产安装的主节点和录像运行程序。部署包 SHALL 携带程序依赖的 ROS 共享库及 vision_msgs 非 Python 消息类型支持库。部署包 SHALL 不包含系统 RKNN Runtime 和基础系统共享库。打包结果 SHALL 记录源版本和文件指纹；文件指纹是用于核对文件内容的 SHA-256 校验值。

#### Scenario: 生成离线安装包
- **WHEN** 从生产板已安装目录打包
- **THEN** 包内可核对主节点、录像工作进程与检查器的内容，且不包含系统 RKNN Runtime

### Requirement: 保持配置一致

部署 SHALL 逐字节安装打包时生产板的配置。部署 SHALL 在覆盖既有配置前备份。未随包提供模型时，部署 SHALL 校验目标模型与源模型的文件指纹。

#### Scenario: 在已有安装的板上升级
- **WHEN** 目标板已有不同配置
- **THEN** 旧配置获得备份，安装后的配置指纹与源快照一致

### Requirement: 上电启动与核心限制

部署脚本 SHALL 启用系统开机服务。服务和直接启动入口 SHALL 将主程序及继承其亲和性的子进程限制为 CPU 4–7。服务 SHALL 在异常退出后尝试重启。

#### Scenario: 启用服务
- **WHEN** 部署脚本成功完成
- **THEN** 开机服务处于 enabled，服务配置指定 CPU 4–7，且默认不立即采集

### Requirement: 检查失败保留原安装

部署 SHALL 在修改现有安装前检查包校验值、目标架构、系统 Runtime、模型、动态库与配置设备。检查失败 SHALL 返回非零退出码。

#### Scenario: 目标板缺少模型
- **WHEN** 未随包提供模型且目标板模型缺失或内容不一致
- **THEN** 安装停止并指出问题，原服务与配置保持不变

### Requirement: 私有 ROS 库离线加载

预检与启动入口 SHALL 优先使用包内私有 ROS 共享库目录。安装 SHALL 不覆盖系统 ROS 目录。打包 SHALL 在源板依赖无法解析时失败，并记录随包库的来源与文件指纹。

#### Scenario: 新板缺少两个附加 ROS 包
- **WHEN** 同型目标板已具备基础环境，但未安装 vision_msgs 和 cv_bridge
- **THEN** 从包内解析这两个库及其 ROS 依赖，无需 apt 安装，动态库预检通过
