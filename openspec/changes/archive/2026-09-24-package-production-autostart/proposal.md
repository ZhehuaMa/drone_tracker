# Proposal

## Why

生产板已有可运行的安装版本，但复制到其他同型板和设置上电启动仍需手工操作。需要一个独立部署包，保持当前生产配置并将程序限制在 CPU 4–7 上运行。

## What Changes

- 新增部署脚本和打包脚本，直接使用生产板已安装文件，不重新编译工作区。
- 新增 systemd 服务。systemd 是系统启动和管理后台程序的机制；服务负责上电启动、退出清理与异常重启。
- 原样复制生产配置，保留旧安装和配置备份。包内携带主程序依赖的 ROS 共享库，以及 vision_msgs 的消息类型支持库；目标板无需通过 apt 补装 vision_msgs 和 cv_bridge。系统 RKNN Runtime 与基础系统库仍由目标板提供。
- 启动入口先停止桌面服务，确认 OSD 使用的显示设备已释放后再运行节点；停止失败时拒绝启动。
- 例如：原来需要复制程序并手工启动；现在解压包、执行部署脚本，下次上电自动启动并绑定 CPU 4–7。

## Capabilities

### New Capabilities

- `production-binary-deployment`: 生产二进制打包、配置一致性和开机服务管理。

### Modified Capabilities

无。

## Impact

新增 `deployment/` 脚本和说明。安装目标为 `/opt/drone_tracker` 和 `drone-tracker.service`。不改变算法、ROS 话题、模型契约及现有 `drone-interceptor.service`。
