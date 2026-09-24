# 实机验收脚本说明

这些脚本用于已授权的独立生产板验收目录，不是正式节点的启动脚本。运行前核对目录、服务名、模型和录像路径，按 validation.md 加载 ROS 与安装环境。

- `acceptance.py comparison`：一次关闭基线和三组旧/新路径，每轮 75 秒。每秒保存资源与线程调度信息。三个模式使用同一录像和模型。
- `acceptance.py endurance`：610 秒持续运行。保存每条消费结果和原始阶段日志。
- `acceptance-small.py rounds`：小尺寸录像的 20 轮 ROS ON/OFF、退出与重新启动。它不替代性能对比，冻结像素另由同一后端实例的 `osd_pipeline_probe` 检查。
- `audit_rga.cpp`：动态链接入口计数与故障注入。编译命令为 `g++ -std=c++17 -O2 -shared -fPIC audit_rga.cpp -I/usr/include/rga -ldl -o audit_rga.so`。设置 `LD_PRELOAD` 后统计 RGA 操作；`OSD_TEST_FAULT=commit` 在第 30 次提交返回 EIO；`timeout` 屏蔽后续翻页事件。该库只用于测试。
- `final-checks.py`：等待性能比较结束后，串行运行提交失败、翻页超时、像素探针和 DRM 打开失败，并恢复正式服务。
- `audit-scheduling.py`：使用 strace 独立短测关闭、旧路径和新路径，记录调度相关系统调用；其结果不加入性能统计。
- `analyze.py <证据目录>`：仅匹配规范运行名称的 JSON，排除预热和边界窗口，合并原始桶。旧日志使用保守上界，明确标记方法。
- `report.py <证据目录> <输出.md>`：从分析 JSON 和原始资源 JSON 生成每轮阶段表。
- `next-comparison.sh`：本次串行调度记录，等待长测完成后安装已经构建好的独立副本再运行对比。重放时先移走上次完成标记，避免把旧标记当作新测试结果。

所有模式会停止测试进程并恢复原本运行的正式服务，同时断言正式配置和 current 链接保持不变。未经修改的脚本硬编码本次独立目录；在另一台板或另一目录运行前必须先调整路径。
