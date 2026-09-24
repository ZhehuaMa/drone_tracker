# RK3576 生产部署包

部署包来自生产板已安装版本。包含主节点、录像工作进程、录像检查器、ROS 包发现文件、原样配置及配置引用的 RKNN 模型。随包包含程序依赖的 ROS 共享库和 vision_msgs 非 Python 类型支持库，存放在版本目录的 `lib/bundled-ros`。预检和启动优先使用这些库，不覆盖系统 ROS。没有源码、基准测试程序、RKNN Runtime 或基础系统共享库。

## 在其他生产板安装

适用板卡为 `HZ-RK3576_WuKong Board`，Ubuntu 22.04 ARM64、ROS 2 Humble，系统 RKNN Runtime 2.3.2、RKNPU 驱动 0.9.8。基础系统库（如 OpenCV、FFmpeg）和相机驱动须预先安装。无需在目标板通过 apt 补装 vision_msgs 或 cv_bridge；ROS 2 Humble 基础环境仍需存在。

1. 将 `.tar.gz` 和同名 `.sha256` 复制到目标板同一目录。
2. 核对压缩包，解压并进入目录。将下面的 `<包名>` 替换为实际文件名，不含 `.tar.gz`。

   ```bash
   sha256sum -c <包名>.tar.gz.sha256
   tar -xzf <包名>.tar.gz
   cd <包名>
   sudo ./deploy.sh --check
   ```

3. 安装并设置上电启动：

   ```bash
   sudo ./deploy.sh
   ```

   首次安装默认不立即启动，也不关闭当前桌面。若希望立即启动，使用 `sudo ./deploy.sh --start`。升级前同名服务已经运行时，脚本会停止它并在升级后恢复运行。每次启动节点前都会关闭桌面，当前桌面会话也会结束。

4. 检查服务：

   ```bash
   systemctl is-enabled drone-tracker.service
   systemctl status drone-tracker.service
   journalctl -u drone-tracker.service -f
   ```

`enabled` 表示已配置上电启动，不代表本次已启动或相机验证通过。脚本不会重启开发板，也不会发送 `/fpv/tracker_trigger`；跟踪触发方式与原程序相同。

## 配置和 CPU

相机配置安装到 `/opt/drone_tracker/config/camera.yaml`，CPU 策略安装到 `/opt/drone_tracker/config/cpu-affinity.yaml`。两者均与打包时的源文件逐字节一致。`SOURCE.json` 记录源版本和源文件 SHA-256 指纹，`SHA256SUMS` 校验包内文件，`MODELS.sha256` 校验所用模型。

本次生产配置启用 HDMI 与录像，使用 `/dev/video22`、`/dev/media2`、`/dev/dri/card0`，录像目录为 `/var/lib/drone_tracker/recordings`。这些设备编号依赖接线和驱动，不保证其他板相同。采集前执行 `v4l2-ctl --list-devices` 和 `media-ctl -p` 核对实际拓扑。设备存在仅是预检，不证明接入了正确传感器或显示设备。

启动入口会同步停止 `display-manager.service` 和生产板使用的 `lightdm.service`。OSD 启用时，随后读取 `/sys/kernel/debug/dri/<设备编号>/clients`，确认没有进程占用目标显示设备；最多等待 5 秒。停止失败、列表不可读或仍有占用时，启动失败并报告原因，不执行节点。系统必须已挂载 debugfs（内核调试信息文件系统）；脚本不会自动挂载。`deploy.sh --check` 只检查部署条件，不停止桌面。

开机服务排在桌面启动任务之后执行，因此会先等桌面启动任务结束，再停止桌面并运行节点。运行期间不要手动启动桌面。停止节点后不会自动恢复桌面，避免服务异常重启时与桌面争用显示设备。需要恢复时，先执行 `sudo systemctl stop drone-tracker.service`，确认没有手动运行的节点且显示设备已释放，再执行 `sudo systemctl start lightdm.service`。

服务以 root 运行以访问相机、显示和录像目录。服务和手动运行都调用 `/opt/drone_tracker/bin/run-camera`。该入口读取同一份 `cpu-affinity.yaml`，用 `taskset` 设置总允许范围，并把策略路径传给节点。服务主进程就是节点，异常退出后由 systemd 尝试重启；一分钟失败五次后停止重试，可修复问题后执行 `sudo systemctl reset-failed drone-tracker.service` 再启动。

策略中的 `allowed_cpus` 是总范围。`role-pinned` 模式按职责绑定：已验证的生产映射为控制与跟踪 CPU 4、YOLO 检测 CPU 5、HDMI 显示 CPU 6、相机采集与录像 CPU 7。辅助线程继承其创建者的核心。`inherit` 模式只设置总范围，不逐职责分配，也不保证负载均衡。修改文件后必须重启节点；每次 ON 创建的新线程使用启动时读取的同一快照。直接运行程序且未设置 `DRONE_TRACKER_CPU_CONFIG` 时，程序本身不改亲和性。

日志中的 `CPU policy loaded` 只表示文件读取成功。每条 `CPU affinity pid=... tid=... role=... requested=... actual=... round=...` 才表示该职责在工作前绑定并读回成功。可用 `journalctl -u drone-tracker.service` 核对六个职责，再用下面的 `/proc` 掩码确认。

服务停止时先向主节点发送 SIGINT，让它完成录像收尾；30 秒后仍未退出的进程组由 systemd 清理。启动后可核对所有节点线程的实际核心限制：

```bash
pid=$(systemctl show -p MainPID --value drone-tracker.service)
grep Cpus_allowed_list /proc/"$pid"/task/*/status
```

部署不修改 `drone-interceptor.service`，不覆盖 `/usr/lib/librknnrt.so`，不写 ROS 安装目录。

## 备份与回退

每次安装在 `/opt/drone_tracker/backups/<时间戳>` 留存旧入口、模型、配置和 `current` 链接。旧 release 目录保持不变。脚本输出本次备份路径；安装失败也应使用该路径。

以下步骤恢复已有安装。将 `backup` 改为实际备份路径：

```bash
backup=/opt/drone_tracker/backups/<时间戳>
sudo systemctl disable --now drone-tracker.service
# 先移走本次新增的配置和入口，再恢复备份，避免旧安装遗留新策略。
sudo rm -f /opt/drone_tracker/config/cpu-affinity.yaml /opt/drone_tracker/bin/cpu-policy.py
sudo tar -xpf "$backup/installation.tar" -C /opt/drone_tracker
if [ -f "$backup/drone-tracker.service" ]; then
    sudo cp -a "$backup/drone-tracker.service" /etc/systemd/system/drone-tracker.service
else
    sudo rm -f /etc/systemd/system/drone-tracker.service
fi
sudo systemctl daemon-reload
```

查看备份中的 `service-enabled.txt` 和 `service-active.txt`。原来已启用时再执行 `sudo systemctl enable drone-tracker.service`；原来正在运行（值为 `1`）时再执行 `sudo systemctl start drone-tracker.service`。原来不存在该服务时无需再启动。备份解压会覆盖原有文件；本次新增且旧包没有的模型或程序版本可以保留，不影响旧配置。

首次安装没有旧内容时可能不存在 `installation.tar`。此时禁用并删除服务文件，再移走本次安装的 `config/camera.yaml`、`config/cpu-affinity.yaml`、`bin/run-camera`、`bin/cpu-policy.py`、`bin/prepare-display.py` 和 `current` 链接；最后执行 `sudo systemctl daemon-reload`。保留模型、录像和 release 目录供检查，确认后再清理。

## 重新打包

在生产源板放置整个 `deployment/` 目录，执行：

```bash
source /opt/ros/humble/setup.bash
bash deployment/package.sh /tmp/drone-tracker-packages --cpu-policy /绝对路径/cpu-affinity.yaml
```

默认包含当前配置引用的模型。目标板已具有相同模型时可追加 `--without-models`；安装仍会检查模型内容与源板一致。旧源安装没有 CPU 策略时，必须显式传入 `--cpu-policy`，例如使用已核对的 `deployment/cpu-affinity.yaml`。脚本不会自行生成策略。`SOURCE.json` 会记录策略来源和 SHA-256 指纹。已有策略且未传该选项时，脚本复制当前安装的策略。打包不重新编译、不启动相机、不更改服务。打包期间若源配置、模型、二进制或当前版本链接变化，会终止生成。

包内有源程序附带的许可证文件。模型和生成的部署包应存放于忽略的本地产物目录，不提交到源码仓库。

### 显示计时诊断版本

需要排查 HDMI 延迟时，在保留当前 release 路径和 `camera.yaml` 副本后，
使用包含 `add-osd-stage-timing` 实现的版本。将配置 `osd.timing_enabled` 设为
`true` 并重启服务；默认值为 `false`，不可运行时修改。
指标边界及日志示例见 `src/platform/README.md` 的“HDMI 显示分段计时”。

生产对照须在明确测试窗口执行。保持模型、输入、录像、显示模式与其他配置不变，
交替关闭和开启计时，各至少三轮且每轮至少 60 秒。记录板卡、源码/配置/模型指纹、
系统 Runtime、显示模式、采集/消费/显示速率及 CPU/内存变化。
吞吐中位数下降超过 5% 时记录为开销目标未通过并调查。
测试结束恢复原配置和 release 后重启服务。只回退诊断时可设
`osd.timing_enabled=false` 后重启；不得覆盖系统 RKNN Runtime。
