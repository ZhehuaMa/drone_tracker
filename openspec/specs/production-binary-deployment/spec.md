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

部署包 SHALL 包含本机 ARM64 构建安装目录中的主节点和录像运行程序。部署包 SHALL 携带程序依赖的 ROS 共享库及 vision_msgs 非 Python 消息类型支持库。部署包 SHALL 不包含系统 RKNN Runtime 和基础系统共享库。打包结果 SHALL 记录源版本和文件指纹；文件指纹是用于核对文件内容的 SHA-256 校验值。

#### Scenario: 生成离线安装包
- **WHEN** 从本机 ARM64 构建安装目录打包
- **THEN** 包内可核对主节点、录像工作进程与检查器的内容，且不包含系统 RKNN Runtime

### Requirement: 保持配置一致

部署 SHALL 逐字节安装打包时明确选定的相机配置。部署 SHALL 在覆盖既有配置前备份。CPU 策略 SHALL 作为独立配置文件纳入打包、指纹记录、安装和备份。打包未提供显式策略时 SHALL 使用仓库默认策略，不静默生成新策略。相机配置 SHALL 保持逐字节复制。恢复旧安装 SHALL 同时恢复匹配的策略、配置和启动入口。未随包提供模型时，部署 SHALL 校验目标模型与打包模型的文件指纹。

#### Scenario: 在已有安装的板上升级
- **WHEN** 目标板已有不同配置，维护者安装通过预检的新包
- **THEN** 旧配置获得备份，安装后的 `/opt/drone_tracker/config/camera.yaml` 指纹与包内所选配置一致

#### Scenario: 未指定 CPU 策略
- **WHEN** 维护者在本机打包且未传 `--cpu-policy`
- **THEN** 包内逐字节使用 `deployment/cpu-affinity.yaml`，来源和指纹记录在 `SOURCE.json`

#### Scenario: 从缺少策略的旧安装打包
- **WHEN** 维护者从缺少 CPU 策略的旧板端安装流程迁移到本机打包，且未传 `--cpu-policy`
- **THEN** 打包使用仓库默认策略并记录来源和指纹，不读取或推断旧板端安装内容

### Requirement: 上电启动与核心限制

部署脚本 SHALL 启用系统开机服务。服务和直接启动入口 SHALL 使用同一 CPU 策略文件，将主程序及继承其亲和性的子进程限制在配置允许范围内，并支持程序在该范围内按职责分配。系统 MUST NOT 在服务模板和入口脚本中分别硬编码核心范围。服务 SHALL 在异常退出后尝试重启。

#### Scenario: 启用服务
- **WHEN** 部署脚本成功完成
- **THEN** 开机服务处于 enabled，启动入口使用已安装策略，且默认不立即采集

#### Scenario: 使用生产策略手动启动
- **WHEN** 用户通过直接启动入口使用允许 CPU 4–7 的按职责策略
- **THEN** 总范围和职责映射与服务启动一致

### Requirement: 检查失败保留原安装

部署 SHALL 在修改现有安装前检查包校验值、目标架构、系统 Runtime、模型、动态库与配置设备，以及 CPU 策略的完整性和目标核心可用性。检查失败 SHALL 返回非零退出码。

#### Scenario: 目标板缺少模型
- **WHEN** 未随包提供模型且目标板模型缺失或内容不一致
- **THEN** 安装停止并指出问题，原服务与配置保持不变

#### Scenario: 策略核心不可用
- **WHEN** 策略无效或目标板没有可用的指定核心
- **THEN** 预检返回非零，原安装和服务状态保持不变；仅检查模式不启动或停止服务

### Requirement: 私有 ROS 库离线加载

预检与启动入口 SHALL 优先使用包内私有 ROS 共享库目录。安装 SHALL 不覆盖系统 ROS 目录。打包 SHALL 在本机 ARM64 构建环境中无法解析程序依赖时失败，并记录随包库的来源与文件指纹。

#### Scenario: 新板缺少两个附加 ROS 包
- **WHEN** 同型目标板已具备基础环境，但未安装 vision_msgs 和 cv_bridge
- **THEN** 从包内解析这两个库及其 ROS 依赖，无需 apt 安装，动态库预检通过

#### Scenario: 构建环境缺少 ROS 依赖
- **WHEN** 本机 ARM64 构建环境无法解析所需的 ROS 共享库
- **THEN** 打包失败，不生成声称依赖齐全的部署包

### Requirement: 两种 RK3576 相机配置模板
仓库 SHALL 分别提供 AR0234 相机模板 `parameters/rk3576_ar0234_camera_params.yaml` 和 SC233HGS 生产模板 `parameters/rk3576_sc233hgs_production_params.yaml`。前者 SHALL 保持软件触发模式，且默认关闭仅 SC233HGS 支持的独立原画面录像。后者 SHALL 以当前生产配置为参数基线，显式使用 SC233HGS 连续采集，并保留当前模型、ROS bag、HDMI 和独立录像设置。两个模板 SHALL 标明设备路径须在目标板复核，以及相机内参是否为实测标定。相机 launch SHALL 默认引用改名后的 AR0234 模板，并继续要求用户显式提供 `video_source`。

#### Scenario: 通用相机入口使用默认模板
- **WHEN** 用户启动 `rk3576_camera.launch.py`，仅提供 `video_source` 而未指定 `params_file`
- **THEN** launch 选择 AR0234 模板，节点仍按软件触发配置读取参数，独立原画面录像默认关闭

#### Scenario: 查看生产模板
- **WHEN** 维护者查看 SC233HGS 生产模板
- **THEN** 能直接看到连续采集、四个 RKNN 模型角色、目标板设备路径及 ROS bag、HDMI、独立录像开关的当前生产参数

### Requirement: 本地选择打包输入
`deployment/package.sh` SHALL 在本机调用 ARM64 Linux 构建环境生成生产部署包，不要求读取源板已安装目录。默认相机配置 SHALL 为仓库 SC233HGS 生产模板；`--camera-config` SHALL 覆盖该选择。默认程序安装目录 SHALL 为 `.docker-artifacts/install/drone_tracker`；`--install-root` SHALL 覆盖该选择。默认 CPU 策略 SHALL 为 `deployment/cpu-affinity.yaml`；`--cpu-policy` SHALL 覆盖该选择。打包 SHALL 要求显式提供本地 `--model-root`。打包 SHALL 在任何必需输入缺失或不合法时失败，不回退读取生产板已安装配置。

#### Scenario: 默认生产模板打包
- **WHEN** 维护者在本机完成 ARM64 构建，提供有效本地模型目录并执行 `deployment/package.sh <输出目录> --model-root <目录>`
- **THEN** 部署包采用仓库 SC233HGS 生产模板、默认安装目录和默认 CPU 策略，无须连接源板

#### Scenario: 选择其他配置
- **WHEN** 维护者传入有效的 `--camera-config /绝对路径/相机.yaml`
- **THEN** 部署包逐字节采用该文件，并记录其路径及 SHA-256 指纹

#### Scenario: 缺少本地模型
- **WHEN** 配置引用一个模型，但 `--model-root` 中找不到对应文件
- **THEN** 打包返回非零并指出缺失的模型，不生成可部署的成功产物

### Requirement: 目标板模型路径映射与输入指纹
配置中的 `/opt/drone_tracker/models/` SHALL 表示目标生产板运行时的模型根目录。打包 SHALL 将该目录下的相对路径映射到本地 `--model-root`，不得把容器路径写入运行配置。打包 SHALL 拒绝根目录外、空缺或无法安全映射的必需模型路径。`--without-models` SHALL 只省略包内模型二进制，仍须从本地模型计算 `MODELS.sha256`。打包 SHALL 校验输入复制前后的内容一致，并在 `SOURCE.json` 中记录配置、CPU 策略、程序、模型及构建环境的来源和指纹；不得将本机容器冒充为生产源板。

#### Scenario: 映射模型文件
- **WHEN** 配置引用 `/opt/drone_tracker/models/fp16/head.rknn`，且本地模型根目录含 `fp16/head.rknn`
- **THEN** 打包使用该本地文件，包内配置仍引用目标板路径，模型清单记录该文件的 SHA-256

#### Scenario: 不携带模型
- **WHEN** 维护者使用 `--without-models` 且提供完整本地模型目录
- **THEN** 包内没有模型二进制，但 `MODELS.sha256` 仍覆盖配置引用的全部模型，目标板预检继续核对这些指纹
