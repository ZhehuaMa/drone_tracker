# Spec Delta

## ADDED Requirements

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

## MODIFIED Requirements

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

### Requirement: 私有 ROS 库离线加载

预检与启动入口 SHALL 优先使用包内私有 ROS 共享库目录。安装 SHALL 不覆盖系统 ROS 目录。打包 SHALL 在本机 ARM64 构建环境中无法解析程序依赖时失败，并记录随包库的来源与文件指纹。

#### Scenario: 新板缺少两个附加 ROS 包
- **WHEN** 同型目标板已具备基础环境，但未安装 vision_msgs 和 cv_bridge
- **THEN** 从包内解析这两个库及其 ROS 依赖，无需 apt 安装，动态库预检通过

#### Scenario: 构建环境缺少 ROS 依赖
- **WHEN** 本机 ARM64 构建环境无法解析所需的 ROS 共享库
- **THEN** 打包失败，不生成声称依赖齐全的部署包
