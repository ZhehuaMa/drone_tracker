# ROS 库随包交付验证（2026-09-24）

修复新板缺少 vision_msgs 和 cv_bridge 导致的部署预检失败。打包与检查使用 SSH 别名 firefly-production 对应的 HZ-RK3576_WuKong 生产源板。未连接用户报错的新目标板。

- 源版本：`/opt/drone_tracker/releases/bundle-20260923T181257Z-2945`。
- 安装包：`drone-tracker-20260924T030338Z-xuvlRjNS.tar.gz`，包含原配置、二进制、模型及 56 个 ROS 共享库。
- Bash 语法、OpenSpec 校验、压缩包和包内全部文件 SHA-256 校验通过。
- 源板 `deploy.sh --check` 通过。`ldd -r` 确认主程序从包内解析 cv_bridge 和 vision_msgs；全部 56 个随包库无缺失依赖或未解析符号。
- 包内不含 RKNN Runtime；没有修改源板正式安装或服务，没有启动相机。
- systemd 检查同时报告源系统 bootanim 的 KillMode=none 和 rkaiq_3A 服务文件全员可写警告；未修改这些系统服务。
- 未执行新目标板部署、相机、模型推理、重启或持续负载验证。源板检查不替代新板验收，任务 4.4 保持未完成。

安装包和动态库验证日志位于仓库忽略目录 `.docker-artifacts/production-bundle-with-ros-20260924/`。
