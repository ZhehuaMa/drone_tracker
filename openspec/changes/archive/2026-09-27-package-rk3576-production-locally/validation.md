# RK3576 本机生产打包验证记录

## 范围与输入

本记录对应 OpenSpec 变更 `package-rk3576-production-locally`。SHA-256 是文件内容的校验值；两份文件的值相同，表示本次核对时内容逐字节相同。2026-09-27 在 Darwin ARM64 开发机上使用本仓库的 Linux ARM64 镜像打包；首次检查只在生产板运行安装前预检。随后在同一生产板补做升级、备份和回退验证，结果见下文。

| 项目 | 本次值 |
|---|---|
| 目标板 | `HZ-RK3576_WuKong Board`，内核 `6.1.141-rt36`，通过 `ssh firefly-production` 访问 |
| 构建镜像 | `drone-tracker-rk3576:humble-arm64-rknn2.3.2`，镜像 ID `sha256:620524fe768a8c7664a24c40504c86dfea35538d53b2da63b6dbf4cc4849293d` |
| Git | 提交 `3df6e65bdd5bc3916c4f72435b8c6e6379c66593`，打包时工作区有本变更的未提交修改；准确文件内容以包内 `SOURCE.json` 的指纹为准 |
| 程序来源 | 本机 `.docker-artifacts/install/drone_tracker`，由 `./build.sh --clean --test` 在上述镜像内重建 |
| 配置来源 | `parameters/rk3576_sc233hgs_production_params.yaml`；SHA-256 `2ffd84d048012df339d4100bd97a01383e493145f5eead80c9bd4e462bbc4f9c` |
| CPU 策略来源 | `deployment/cpu-affinity.yaml`；SHA-256 `4f46293201355131c1e72c8a72cdfaafa2c3121eaa48fe0b5ad69ac442e4f01b` |
| 本地模型来源 | `.docker-artifacts/production-deployment-20260924-osd/drone-tracker-20260923T180822Z-PzBYXHCj/models`，四个文件均已随包携带 |
| 部署包 | `drone-tracker-20260927T093845Z-nnklynel.tar.gz`；SHA-256 `20c8e90b24e1546f444d05edff5e95cabff21033ab18fce7af56cc5a5241774c` |
| 本机证据 | `.docker-artifacts/production-package-20260927/` 中的归档、同名 `.sha256`、`board-camera.yaml` 和 `deploy-check.log` |
| 板端预检目录 | `/home/hzhy/drone-tracker-local-package-20260927/`，含同一归档、展开内容和 `deploy-check.log` |

## 模板与构建检查

AR0234 模板经 YAML 解析，确认 `sensor_model=ar0234`、`capture_mode=software_trigger`、`recording.enabled=false`。SC233HGS 模板经 YAML 解析，确认 `sensor_model=sc233hgs`、`capture_mode=continuous`、`need_save=true`。新生产模板与预检时板上 `camera.yaml` 的参数值逐项比较，只有 `osd.render_path` 从未显式写入变为 `cached-overlay`；其余参数值一致。这里的 `cached-overlay` 是当前程序默认渲染路径。

`./build.sh --clean --test` 在 ARM64 容器内构建成功，首轮 CTest 为 **43/43 通过**。把打包测试接入 CTest 后，再执行 `./build.sh --test`，最终为 **44/44 通过**。`osd_yaml_tests` 检查 AR0234、SC233HGS 和视频三份模板的有效渲染路径均为 `cached-overlay`。`deployment_package_tests` 的 **5/5 单元测试通过**，覆盖默认值、显式覆盖、缺失输入、合法与越界模型路径、符号链接逃逸、复制期间输入变化和归档失败清理。另生成无模型包，确认包内无模型二进制，但 `MODELS.sha256` 仍有四条记录，`SOURCE.json` 标明 `models_included=false`。

最终包内 `config/camera.yaml` 与所选生产模板逐字节相同，`config/cpu-affinity.yaml` 与默认策略逐字节相同。另用追加一行注释的相机配置执行 `--camera-config` 和 `--without-models` 完整打包；覆盖配置在包内仍逐字节一致，`SOURCE.json` 的路径及 SHA-256 正确，包内无模型二进制。该检查包保存在 `.docker-artifacts/package-override-check/`，不作为本次生产预检包。生产包的归档 `.sha256`、包内 `SHA256SUMS` 及 `MODELS.sha256` 核对通过。主节点、录像工作进程和录像检查器均为 AArch64 ELF 程序；包内有 ROS 包发现文件与私有 ROS 库，且没有 `librknnrt`。容器内对程序和私有库执行了 `readelf` 及 `ldd -r` 检查。

## 首次本地包与生产板比较

| 文件或参数 | 新包 | 生产板当前文件 | 结论 |
|---|---|---|---|
| `camera.yaml` SHA-256 | `2ffd84d048012df339d4100bd97a01383e493145f5eead80c9bd4e462bbc4f9c` | `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c` | 字节不同；参数差异仅为显式加入 `osd.render_path: cached-overlay`，其余为说明文字 |
| `need_save` | `true` | `true` | 一致；本机较旧快照曾为 `false`，本次模板以生产板实际值建立 |
| `recording.enabled` | `true` | `true` | 一致 |
| `mv_camera.sensor_model` / `capture_mode` | `sc233hgs` / `continuous` | `sc233hgs` / `continuous` | 一致 |
| 系统 RKNN Runtime | 包内未携带 | `/usr/lib/librknnrt.so` SHA-256 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8` | 保持目标板系统库 |

四个模型的 SHA-256 与板上 `/opt/drone_tracker/models` 对应文件一致：

| 模型相对路径 | SHA-256 |
|---|---|
| `balloon_best_zoo6_int8_fp16_cls.rknn` | `002b1b0a5e7476a5cc62f495997f05d2550e44697fb89c8d48913fb1ee5e06f7` |
| `fp16/template_backbone.rknn` | `622f36f606a51d4fa6514856fefb1afea79218d45d2af4f3c3ced84ce3c52ca2` |
| `fp16/search_backbone.rknn` | `9ca8cc19dd0de798a3075c0f797ad4508362d9c297f9a1f04e784c34bfdc9f44` |
| `fp16/head.rknn` | `2ffad600a875ab04332140a59248285bfa5c23b1cc3b32c6dbb9486165494889` |

已在 `HZ-RK3576_WuKong Board` 核对归档校验并执行 `sudo ./deploy.sh --check`，结果为“预检通过：包、模型、配置设备、CPU、ARM64 程序和系统动态库均可用”。板上原服务仍为 active，`/opt/drone_tracker/current` 仍指向 `bundle-20260927T070059Z-2281`。预检期间没有安装、切换服务或启动相机。

## 已有安装升级与回退补验

2026-09-27 在 `HZ-RK3576_WuKong Board` 使用上述同一归档补验 delta spec 的“在已有安装的板上升级”场景。升级前 `/opt/drone_tracker/current` 指向 `bundle-20260927T070059Z-2281`；服务为 `active/enabled`。旧相机配置 SHA-256 为 `f909ec82c2c3c45a82b1c85fb616e2f1f7a97b2da4159c7755af195f2e05298c`，与包内新配置不同。重新核对归档、包内文件及 `sudo ./deploy.sh --check` 后，执行 `sudo ./deploy.sh`。

安装器生成 `/opt/drone_tracker/backups/20260927T095351Z-3091`，并将 `current` 指向新版本 `bundle-20260927T095351Z-3091`。备份的 `installation.tar` 含旧 `config/camera.yaml`、`config/cpu-affinity.yaml`、`bin/run-camera` 和 `current`；`previous-release.txt` 记录旧版本，`service-active.txt` 为 `1`，`service-enabled.txt` 为 `enabled`。备份中旧相机配置的 SHA-256 与升级前一致。安装后相机配置 SHA-256 为 `2ffd84d048012df339d4100bd97a01383e493145f5eead80c9bd4e462bbc4f9c`，与包内所选模板一致；CPU 策略 SHA-256 为 `4f46293201355131c1e72c8a72cdfaafa2c3121eaa48fe0b5ad69ac442e4f01b`。服务在新版本下为 `active/enabled`。

随后按 `deployment/README.md` 的回退步骤停用服务、从该备份恢复安装文件与服务文件，再重新启用并启动服务。复核结果：`current` 回到 `bundle-20260927T070059Z-2281`；相机配置、CPU 策略、`run-camera`、`cpu-policy.py` 和服务文件的 SHA-256 均与升级前相同。服务为 `active/running/enabled`，`NRestarts=0`。新 release 目录和该备份保留，便于追查。本次补验没有发送跟踪 ON 指令，也不证明新版本的相机、NPU 或持续负载验收通过。

## 验证边界

生产板预检验证文件、设备节点和动态依赖等安装条件；升级补验验证配置安装、旧文件备份及回退。它们不证明设备节点连接的是预期传感器，也不证明相机采集、NPU 模型结果、完整跟踪质量或长期负载通过。本次未执行这些实机运行检查。镜像内未提供 `clang-tidy` 和 `clangd`，没有执行对应静态检查。Jetson 构建与运行也未执行。

## OpenSpec 需求对照

已对照本变更的 `proposal.md`、`design.md` 和 `specs/production-binary-deployment/spec.md`：两份相机模板与 launch 默认值、宿主机输入选择、模型路径映射及无模型包指纹、程序和 ROS 私有库的离线检查、配置与 CPU 策略逐字节复制、目标板备份和预检均有对应实现或上述验证。既有 `deploy.sh` 继续备份旧配置、策略、入口和当前版本链接；既有 `run-camera` 继续优先设置包内私有 ROS 库路径。执行 `openspec validate package-rk3576-production-locally --strict` 通过。此次没有改动算法、ROS 话题、模型内容或目标板安装路径。
