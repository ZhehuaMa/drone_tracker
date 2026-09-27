# Design

## Context

当前部署包只能在生产源板生成，且会复制板上已安装的相机配置。目标是在本机已有的 ARM64 构建产物旁完成打包，使仓库中的生产模板明确决定下一份包的参数。变更的动机见 `proposal.md`；行为要求见 `specs/production-binary-deployment/spec.md`。

现有 `build.sh` 已在 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 镜像内编译，并把安装目录留在 `.docker-artifacts/install/drone_tracker`。现有 `package.sh` 仍硬编码源板架构、`/opt/drone_tracker/current`、板上模型和 ROS 库。`deploy.sh` 消费固定的包布局，安装配置到目标板 `/opt/drone_tracker/config/camera.yaml`。这些 `/opt/drone_tracker` 路径是目标板路径，不是本机或容器中的源路径。

## Goals / Non-Goals

**Goals:**

- 本机选择相机配置、CPU 策略、构建安装目录和本地模型目录，离线生成与现有目标板安装器兼容的包。
- 保留输入文件原字节，记录内容指纹，使配置来源和模型身份可核对。
- 把容器能够完成的结构及动态依赖检查，与目标板才能完成的设备和运行检查分开。

**Non-Goals:**

- 不在打包时访问、安装或启动目标板，也不在容器中模拟相机或 NPU 实机验收。
- 不把 RKNN 模型二进制、生产包或系统 RKNN Runtime 提交到仓库。
- 不改变跟踪算法、ROS 参数含义、服务启动入口或目标板安装目录。

## Decisions

### 1. 保留 `deployment/package.sh` 作为本机入口，在 ARM64 容器内执行打包核心

选择：入口在宿主机解析参数和绝对路径，确认镜像及输入后，以 `--platform linux/arm64 --network none` 启动现有构建镜像。配置、策略、安装目录和模型目录只读挂载；输出目录可写。容器内使用 ROS 2 Humble、`ldd`、`readelf` 和 `sha256sum` 检查 Linux ARM64 文件并组包。打包核心可拆到 `deployment/` 下的独立脚本，避免在宿主机执行 Linux 专用命令。

原因：本机是非 ARM64 Linux 环境，已有镜像却能提供与正式编译一致的 ARM64 工具和 ROS 库。拒绝继续要求源板打包，因为那会再次让板上安装状态决定发布内容。实际结果是打包无需 SSH；缺少镜像或 Docker 时会在本机明确失败。

### 2. 用两份独立完整模板表达相机用途

选择：现有 `rk3576_camera_params.yaml` 改名为 `rk3576_ar0234_camera_params.yaml`，保留 AR0234 软件触发参数，修正 `recording.enabled=false`。从当前生产板配置建立 `rk3576_sc233hgs_production_params.yaml`，保留 SC233HGS 连续模式、模型、`need_save=true`、HDMI、显示计时和录像参数；显式写出当前默认的 `osd.render_path: cached-overlay`。两份文件都说明模拟内参及 `/dev` 路径的复核边界。`rk3576_camera.launch.py` 默认引用 AR0234 文件，保持其通用入口语义。

原因：两个传感器的采集模式和录像能力不同。拒绝用一份配置配合注释切换，因为有效参数与示例参数容易混淆。实际结果是生产打包默认文件与通用相机 launch 默认文件不同；两者都在文件名和文档中明确标识。

### 3. 模型路径按目标板路径映射，不重写运行配置

选择：生产模板中的 `/opt/drone_tracker/models/<相对路径>` 保持原样。打包器要求 `--model-root` 指向本地模型根目录，并读取 `<model-root>/<相对路径>`。YOLO、模板 Backbone、搜索 Backbone 和 Head 四个角色必须有非空路径；共享 `nano_backbone` 保持空值。打包拒绝 `..` 穿越、符号链接逃逸、重复目标名冲突和缺失文件。`--without-models` 仍计算四个模型的 SHA-256，只省略二进制复制。

原因：节点运行和目标板安装器已经使用 `/opt/drone_tracker/models/`。拒绝在 YAML 中写容器挂载路径或在打包时改写 YAML，因为那会破坏配置逐字节可追溯性。实际结果是本地模型目录可放在任意位置，包内配置始终使用目标板路径。

### 4. 保持包布局，改变来源记录

选择：继续生成 `config/camera.yaml`、`config/cpu-affinity.yaml`、`payload/`、`MODELS.sha256`、`DEVICES`、`SHA256SUMS` 和归档校验文件。相机配置及策略通过字节复制入包。`SOURCE.json` 记录所选宿主机路径、各输入 SHA-256、Git 提交及工作区是否有修改、镜像身份、模型是否随包提供。旧的 `board` 和源板 `release` 字段不用于本机包；使用清楚的构建安装目录字段替代。包内不包含系统 RKNN Runtime，目标板继续使用 `/usr/lib/librknnrt.so`。

原因：`deploy.sh` 和 `run-camera` 已依赖这些包路径，但不依赖旧 `SOURCE.json` 的板卡字段。拒绝新建另一套部署包格式，因为会增加目标板安装和回退风险。实际结果是现有安装器可继续读取包，来源信息不会误称包出自生产源板。

### 5. 本机只验证可验证的条件，目标板继续预检

选择：容器检查 YAML 结构、传感器模式、模型映射、程序架构、ROS 共享库闭包及所有输入复制前后指纹。`DEVICES` 仅记录 `/dev/...` 语法；打包时不要求本机存在这些设备。目标板 `deploy.sh --check` 继续检查字符设备、系统 RKNN Runtime 指纹、模型和库链接。首次迁移时，发布记录还应比较新包 `camera.yaml` 与目标板当前文件并记录差异。

原因：容器没有生产板设备拓扑、内核和 RKNPU 驱动。拒绝把容器检查称为实机验收。实际结果是生产板不参与打包，但部署前仍可阻止明显不适用的包。

## 打包流程示例

1. 执行 `./build.sh --clean --test`，生成 `.docker-artifacts/install/drone_tracker`。清洁构建可避免旧安装文件混入发布包。
2. 将四个已批准的 RKNN 模型放入本地忽略目录。例如 `<model-root>/fp16/head.rknn` 对应生产配置中的 `/opt/drone_tracker/models/fp16/head.rknn`。
3. 运行 `./deployment/package.sh .docker-artifacts/packages --model-root <model-root>`。入口选仓库生产模板、默认安装目录和默认 CPU 策略；需要其他配置时传 `--camera-config <路径>`。
4. 容器检查并复制输入，生成归档和校验文件。本机核对包内配置与选定模板的 SHA-256 相同。
5. 将包送至 `HZ-RK3576_WuKong Board`，运行 `sudo ./deploy.sh --check`。核对当前配置差异后再安装，并单独做相机及模型实机验证。

## Risks / Trade-offs

- [容器 ROS 共享库与目标板基础系统库不兼容] → 首次迁移在生产板运行 `deploy.sh --check` 和 `ldd -r`，失败时先定位 ABI 差异；不覆盖板上 ROS 或 RKNN 系统库。
- [仓库生产模板的设备编号随接线变化] → 模板注明设备路径属于目标板；部署前使用 `v4l2-ctl --list-devices` 和 `media-ctl -p` 核对，预检继续要求设备存在。
- [默认模板启用 ROS bag 与显示计时，可能增加负载] → 保留当前生产配置的行为，并在发布记录中列出开关及实际配置差异；性能结论仍以目标板短时对照为准。
- [本机模型与目标板旧模型同名但内容不同] → `MODELS.sha256` 以本地输入生成；无模型包在目标板安装前必须通过指纹核对。
- [构建或输入在打包过程中变化] → 打包前后复核指纹，失败时删除未完成的临时包；正式发布使用清洁构建产物。

## Migration Plan

1. 建立两份模板并更新 launch、测试和文档引用；旧文件名停止作为默认入口。
2. 将 `package.sh` 切换为本机容器入口，保持生成包的目标板安装布局。旧的板端 `--release` 用法由本机 `--install-root` 替代，并在文档中注明。
3. 用当前生产配置与模型生成首个本地包，核对配置及模型 SHA-256。先在生产板执行 `deploy.sh --check`，再比较现有配置与包内配置。
4. 安装时沿用 `deploy.sh` 的配置备份和旧 release 保留机制。若运行异常，按 `deployment/README.md` 的备份回退步骤恢复旧配置、CPU 策略和入口。
