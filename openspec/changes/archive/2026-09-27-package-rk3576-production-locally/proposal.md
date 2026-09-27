# Proposal

## Why

生产部署包目前必须在生产源板上生成，而且打包配置取自板上已安装文件。维护者难以从仓库判断下一次打包会使用哪些相机参数，也无法仅凭本机已完成的 ARM64 构建产物生成发布包。

例如，现在修改仓库相机模板后，仍须把程序传到源板，再从 `/opt/drone_tracker/config/camera.yaml` 复制实际配置。变更后，维护者在仓库选择生产模板和本地模型目录，由本机 ARM64 容器生成包；目标生产板只负责安装前检查和实机验证。

## What Changes

- 将现有 RK3576 相机模板改名为 AR0234 专用模板，并关闭其不受支持的独立录像默认开关。相机 launch 的默认参数文件指向新名称，仍要求显式指定 `video_source`。
- 以当前生产配置建立 SC233HGS 生产模板。模板中的 `/opt/drone_tracker/models/...`、`/opt/drone_tracker/config/camera.yaml` 和 `/dev/...` 均表示目标生产板运行路径，不表示打包容器路径。
- **BREAKING**：`deployment/package.sh` 的默认打包来源从生产源板已安装目录改为本机 `.docker-artifacts/install/drone_tracker`、仓库生产模板、仓库 CPU 策略及显式本地模型目录。打包在现有 Linux ARM64 构建容器中执行，不再要求登录生产源板。
- 支持显式选择相机配置、安装目录和 CPU 策略。即使不随包携带模型，也从本地模型目录计算模型指纹；缺少输入时明确失败，不回退到源板旧文件。
- 保留目标板安装布局、包内校验、旧配置备份和 `deploy.sh --check`。记录所选配置、构建产物、模型及构建环境的来源与 SHA-256 指纹。
- 更新部署文档、模板引用、配置检查和打包测试。首次切换时比较包内配置与目标板当前配置，并在生产板执行预检和实机验收。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `production-binary-deployment`：规定本地 ARM64 容器打包、双相机模板及默认生产模板选择、输入校验与来源记录，同时保留目标板逐字节安装和预检要求。

## Impact

- 主要涉及 `parameters/`、`deployment/package.sh`、`deployment/README.md`、`launch/rk3576_camera.launch.py`、配置和部署测试，以及相关项目说明。
- 本机打包依赖现有 `drone-tracker-rk3576:humble-arm64-rknn2.3.2` 镜像、本地构建安装目录和不纳入 Git 的 RKNN 模型文件。容器内收集 ROS 共享库；目标板仍使用自己的系统 RKNN Runtime。
- 不改变跟踪算法、ROS 话题、模型内容、目标板服务入口或安装路径。容器打包成功不等于相机、模型和长期负载实机验收通过。
