# Tasks

## 1. 相机模板与入口

- [x] 1.1 将 `parameters/rk3576_camera_params.yaml` 改名为 `parameters/rk3576_ar0234_camera_params.yaml`，设置 `recording.enabled=false` 并标注模拟内参和设备复核要求；解析文件后确认 AR0234、软件触发与录像默认值正确。
- [x] 1.2 以当前生产配置建立 `parameters/rk3576_sc233hgs_production_params.yaml`，写明目标板路径、内参性质及 `osd.render_path`；用 YAML 对照确认除显式渲染路径和说明外，当前生产参数值一致。
- [x] 1.3 更新 `launch/rk3576_camera.launch.py` 的默认参数文件名；检查 launch 在未指定 `params_file` 时解析到 AR0234 模板，且仍要求显式 `video_source`。

## 2. 本机打包入口和输入处理

- [x] 2.1 将 `deployment/package.sh` 改为本机容器入口，解析输出目录、`--model-root`、`--camera-config`、`--install-root`、`--cpu-policy` 与 `--without-models`；用帮助输出和缺失输入调用确认默认值及错误退出码。
- [x] 2.2 在 `deployment/` 实现 ARM64 容器内的打包核心，读取本地安装目录并保持现有部署包布局；对包内三个程序、ROS 包发现文件和私有 ROS 库执行架构及动态依赖检查。
- [x] 2.3 在打包核心中实现配置解析及模型路径映射，拒绝缺失、越界和符号链接逃逸；用合法 `fp16/head.rknn` 映射与非法路径输入确认结果。
- [x] 2.4 在打包核心中保留 `--without-models` 的模型指纹检查，并在 `SOURCE.json` 记录所选输入、SHA-256、Git 状态和镜像身份；核对有模型包与无模型包的清单及来源记录。
- [x] 2.5 保证复制输入期间变化或组包失败不会留下成功命名的归档；模拟源文件变化后确认失败，并确认正常包的 `SHA256SUMS` 与归档校验通过。

## 3. 自动化验证

- [x] 3.1 更新 `src/osd/tests/config_yaml_test.py` 与相关模板引用测试；运行对应 CTest，确认两份相机模板和视频模板的有效 `osd.render_path` 为 `cached-overlay`。
- [x] 3.2 为本机打包参数、模型映射和失败路径增加部署测试；运行测试确认默认生产模板、显式配置覆盖、缺失模型、越界路径和 `--without-models` 的行为。
- [x] 3.3 使用本机真实 ARM64 安装目录和已批准模型执行一次完整打包；核对包内相机配置与所选模板逐字节一致，程序为 AArch64，且包内没有系统 RKNN Runtime。

## 4. 文档与发布边界

- [x] 4.1 更新 `deployment/README.md`，写明本地打包命令、模型目录布局、各 `/opt/drone_tracker/...` 路径属于目标板、配置来源及旧 `--release` 用法迁移；按文档命令检查参数能够解析。
- [x] 4.2 更新 `src/platform/README.md` 中的旧模板名称、两种相机入口和模型填写步骤；检查仓库文档不再把改名文件作为有效默认路径。
- [x] 4.3 对首次本地包记录与生产板当前 `camera.yaml` 的参数及 SHA-256 差异，并在 `HZ-RK3576_WuKong Board` 运行 `sudo ./deploy.sh --check`；保存板卡、包、模型及检查结果，不以此宣称相机或 NPU 实机验收通过。
- [x] 4.4 对照 `proposal.md`、`design.md` 和本变更的 delta spec 核验实现，运行 `openspec validate package-rk3576-production-locally --strict`；确认所有规划要求有对应实现与验证记录。
