# 部署包验证记录

## 启动前关闭桌面更新

用户追加要求后，交付包更新为 `.docker-artifacts/production-deployment-20260924-osd/drone-tracker-20260923T180822Z-PzBYXHCj.tar.gz`，同目录附有校验文件和验证日志。此包替代下文首次交付包。

新增 `prepare-display.py`，由服务和手动启动入口共同调用。它同步停止桌面，然后在 OSD 启用时检查内核显示设备客户端列表。无法停止、证据不可读或 5 秒后仍有占用时拒绝启动节点。服务增加在桌面启动任务之后执行的顺序约束。

在正式生产板 Python 3.10 上执行 7 项隔离测试，全部通过。测试替代了系统服务调用，覆盖停止顺序、停止失败、停止后仍 active、设备延迟释放、占用超时、证据不可读或无效，以及关闭桌面先于检查释放。未实际停止桌面或运行节点。包的只读预检通过，检查结束时真实 `lightdm.service` 仍为 active。未执行开机或 HDMI 画面验证。

本轮读取的源版本路径已变为 `/opt/drone_tracker/releases/bundle-20260923T180016Z-38249`。通过逐文件比较确认，与首次包的运行文件、模型和配置内容完全相同；变化仅为源安装路径和本次部署工具。新版压缩包取回后的 SHA-256 校验通过。

## 交付内容

2026-09-24 从正式生产板 `HZ-RK3576_WuKong Board` 的 `/opt/drone_tracker/releases/20260924-recording-2f4686c` 提取运行文件。未重新编译，未混入当前工作区其他未提交修改。

用户已确认包含当前模型。包内包含气球 YOLO 模型，以及 FP16 NanoTrack 的模板 Backbone、搜索 Backbone 和 Head。未包含系统共享库、ROS 2 运行时、源码、测试程序及录像。

部署包存放在本地忽略目录 `.docker-artifacts/production-deployment-20260924/`：

- `drone-tracker-20260923T175528Z-Ik2LMVRP.tar.gz`，约 5.7 MiB。
- 同名 `.sha256` 文件。
- `preflight.log`、`corruption.log`、`affinity.log`。

文件名中的 `20260923T175528Z` 是 UTC 时间，对应北京时间 9 月 24 日。

## 已执行检查

- 本机 Bash 语法检查通过，OpenSpec 严格验证通过。
- 生产板 `deploy.sh --check` 通过：ARM64、板型、CPU 可用性、模型指纹、配置设备、三个程序的动态链接和系统 Runtime 指纹均符合要求。
- systemd 服务文件静态验证通过。系统原有 `bootanim.service` 和 `rkaiq_3A.service` 产生独立警告，部署工具未修改这些服务。
- 对包内配置附加一行后，预检返回非零并报告校验失败。恢复快照后，包内全部 SHA-256 校验通过。
- 使用 `taskset -c 4-7` 启动的检查进程读取 `/proc/self/status`，结果为 `Cpus_allowed_list: 4-7`。服务文件另有 `CPUAffinity=4-7`。
- 生产板原配置与包内配置通过 `cmp` 逐字节比较。配置 SHA-256 为 `bef2b7892d3a9cafeef979a8ec9b6c90d0e5b397cdada9efee79e07818103183`。
- 主节点 SHA-256 为 `b86b076fe0c890d167c6a47b9a68c9c80759520cbe36db519a2a5063afc6de6b`。其余源文件和模型指纹见包内 `SOURCE.json`。
- 取回本机后的压缩包校验通过；检查归档文件清单，确认运行程序、四个模型文件与配置齐全。
- 生产板系统 RKNN Runtime 指纹未变，仍为 `d31fc19c85b85f6091b2bd0f6af9d962d5264a4e410bfb536402ec92bac738e8`。

## 验证边界

本次只创建并验证部署包，未在正式目录执行安装，未修改生产板现有服务状态。`drone-tracker.service` 尚未安装到源板。未执行重启、相机采集、模型推理、HDMI 显示、录像或长时间负载测试，因此不声称这些运行验证通过。CPU 检查验证了绑定机制，尚未测量实际节点与录像工作进程运行时的全部线程。

目标板安装后需检查开机启动和实际设备映射。当前配置保留源板的设备编号和模拟相机内参。其他生产板的设备存在检查不能替代传感器拓扑与图像质量验收。
