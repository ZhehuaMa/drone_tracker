# RK3576 HDMI 显示构建与部署

原 HDMI 管线变更的既定任务已完成（39/39）。RKNN 同步完成问题已修复；生产板在桌面关闭时已完成同进程 20 轮启停及额外 610 秒联合负载，并完成人工画面与恢复流程验收。具体证据见 [原管线最终验收记录](../openspec/changes/integrate-rk3576-hdmi-osd/final-validation.md)。新增参考 HUD 视觉对齐的实施与实机验收进度独立记录在 [HUD 对齐验证记录](../openspec/changes/archive/2026-09-27-align-hud-with-rk-hdmi-osd/validation.md)。原管线的负载数据不能代替新 HUD 的性能验收。

OSD（屏幕信息叠加）在 `drone_tracker_node` 进程内显示跟踪器已消费的图像、同次显示框和外部遥测。不启动第二个采集器。偶数尺寸灰度帧通过 RGA 的中性色度路径处理；奇数尺寸灰度帧先在显示线程复制为等值 BGR，再缩放，不裁切输入。图像与目标层由 DRM/KMS（Linux 显示控制接口）在一次原子提交中切换，即两个层作为同一组交给显示器。

## 构建

既有无 HDMI 部署仍使用默认构建：

```sh
./build.sh --test
```

在具备生产板 SDK 的 ARM64 ROS 2 Humble 环境构建 HDMI 版本：

```sh
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-args \
  -DCMAKE_BUILD_TYPE=Release \
  -DDRONE_TRACKER_PLATFORM=rk3576 \
  -DDRONE_TRACKER_BUILD_OSD=ON \
  -DDRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK=OFF \
  -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DRKNN_INCLUDE_DIR=/path/to/rknn/include \
  -DRKNN_LIBRARY=/usr/lib/librknnrt.so
```

`RKNN_INCLUDE_DIR` 必须包含适配目标 Runtime 的 `rknn_api.h`。链接和部署使用生产板系统 Runtime 2.3.2、驱动 0.9.8，不复制容器 Runtime 到系统。HDMI 构建通过 pkg-config 查找 `libdrm`、`librga` 及其开发头文件。不存在外部 `rk_hdmi_osd` 仓库依赖。

`DRONE_TRACKER_BUILD_OSD=OFF` 不查找显示 SDK。公共显示逻辑和模拟测试可独立构建：

```sh
cmake -S src/osd -B /tmp/drone-osd-tests -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/drone-osd-tests
ctest --test-dir /tmp/drone-osd-tests --output-on-failure
```

可选低层反馈需要 ROS 2 `quadrotor_msgs`，先加载其安装环境，再设置 `DRONE_TRACKER_OSD_LOW_LEVEL_FEEDBACK=ON`。该开关要求 HDMI 构建同时开启。缺少消息包时 CMake 明确失败。已核对的 `LowLevelFeedback` 电池等级为 0 未知、1 正常、2 低电量、3 严重；编译时检查这些常量，避免静默误读其他版本。

程序安装入口仍为 `lib/drone_tracker/drone_tracker_node`。OSD 是静态内部库，不安装单独显示程序或 SDK。参考 ASCII 点阵字库在 `src/osd/reference_font.h`，来源是 DejaVu Sans Mono；`src/osd/DEJAVU_FONT_LICENSE.txt` 保存授权原文。固定中文提示的字形及其独立许可仍在本仓库维护。字体已嵌入源码；构建与运行均不读取本机 `rk_hdmi_osd` 目录。

## 设备和启动参数

启动前检查 HDMI 已连接且指定模式存在。必须停止桌面显示服务，并确认桌面已释放显示设备。仅关闭窗口、屏幕变黑或采集 OFF 都不满足这一条件。程序不会停止或恢复桌面服务、调整 CPU 频率或绑核。

后端调用 `drmSetMaster()` 请求显示控制权，再读取目标设备对应的 `/sys/kernel/debug/dri/<设备次设备号>/clients`。只有内核记录中恰好存在本进程的一个客户端，且 `master=y`（持有显示控制权），才继续分配显示缓冲。生产板 root 下 `drmIsMaster()` 和 `drmSetMaster()` 都可能误报成功，不能仅凭返回值认定独占。

该部署需要可读取上述 debugfs 内核调试记录。记录不可读、格式无法识别、有其他客户端或本进程不是 master 时，初始化拒绝并关闭已打开的 DRM 文件。此时应检查权限、桌面服务与设备占用，不绕过核验。节点不自动挂载 debugfs 或提升权限。

访问 `/dev/dri/card0`、`/dev/dma_heap/system` 以及系统 RGA 所需设备必须具备相应权限。权限分配由部署环境负责。默认不会提升程序权限。

`parameters/rk3576_ar0234_camera_params.yaml`、`parameters/rk3576_sc233hgs_production_params.yaml` 和 `parameters/rk3576_video_params.yaml` 包含如下启动参数：

| 参数 | 默认值 | 含义 |
|---|---|---|
| `osd.enabled` | false | 是否启动 HDMI 显示 |
| `osd.drm_device` | `/dev/dri/card0` | 显示设备 |
| `osd.connector` | `HDMI-A-1` | 必须精确匹配的接口名称 |
| `osd.width` / `osd.height` | 720 / 480 | 输出模式，偶数且在 2–4096 范围 |
| `osd.refresh_hz` | 60.0 | 名义刷新率，允许对应约 59.94 Hz 模式 |
| `osd.frame_stale_ms` | 1000 | 无新结果包超过此时长后清框并提示等待 |
| `osd.telemetry_timeout_ms` | 2000 | 遥测有效期限，使用不受校时影响的单调时钟 |
| `osd.craft_name` | FPV | 最多 24 个可打印 ASCII 字符 |
| `osd.battery_cells` | 0 | 电池节数；0 表示未知，不按整包电压猜测 |

两个超时的合法范围均为 1–3600000 毫秒，电池节数为 0–32，刷新率为 1–240 Hz。尺寸范围只保证配置合法；实际设备必须提供该模式，文字可读性应在实际尺寸验收。所有 `osd.*` 修改均需重启。

`visualized_result` 只控制既有 ROS 调试图像，与 HDMI 开关独立。未编译 OSD 时启用 `osd.enabled` 会明确失败。显示初始化失败会回收资源并使节点初始化失败；运行中显示失败只停用显示，原跟踪和 ROS 发布继续，恢复 HDMI 需要重启节点。

## 遥测与显示行为

标准遥测订阅 IMU、飞行状态、电池、GPS、气压、温度、失效保护和独立雷达状态。完整话题与消息类型见本变更 `specs/osd-ros-telemetry/spec.md`。IMU、GPS、气压、温度采用 best-effort（允许丢弃旧消息以优先新数据）；其他输入采用 reliable（可靠传递），深度均为 10。可用 ROS remap 替换话题名。

例如，把温度输入改为设备实际话题：

```sh
ros2 run drone_tracker drone_tracker_node --ros-args \
  --params-file /path/to/rk3576_video_params.yaml \
  -p osd.enabled:=true -r /fpv/temperature_baro:=/my_baro/temperature
```

必须先填写参数文件中的既有模型和输入配置。此示例不新增模型默认值，也不代表相机已标定。

返航点仅在连接与定位都有效的解锁上升沿记录。解锁时没有定位，本轮不补记后来的位置。气压地面参考只在连接、未解锁和落地状态均新鲜时更新。电池 `charge` 是剩余 Ah：容量 13 Ah、剩余 10 Ah 对应已耗 3000 mAh。未知、非法或过期值不会伪装为有效零值。

ON 开始新轮；重复 ON 不新增轮次。ON/OFF 短提示只在当前轮保留约两秒，使用不受校时影响的单调时钟计时。OFF、EOF 和输入故障保留最后实际显示的纯视频底图，清框并显示对应中文提示，遥测继续更新。新轮首包之前旧底图保持冻结标记。左下 `F` 后面的数字是本轮已消费帧的真实序号；它不是采集时间。左下时钟使用运行设备的本地系统日历时间，显示到十分之一秒；设备时区改变会影响该读数。

### 参考 HUD 的位置与数据

视觉基准固定为 `rk_hdmi_osd` 提交 `3e605a26c7c135b468ef2a3638ffd47144594603` 的 720×480 默认布局。主字为 16×32，小字为 8×16。画布缩放时，机名、四角角标、左右信息列和图形一起缩放；目标框仍用视频几何结果定位。元素坐标、颜色和参考快照的完整对照见 [HUD 对齐验证记录](../openspec/changes/archive/2026-09-27-align-hud-with-rk-hdmi-osd/validation.md)。

| 区域 | 显示内容 | 缺失或过期时 |
|---|---|---|
| 左上与左列 | 机名、副标题、高度、地速、航向、卫星定位等级、温度 | 未知数值显示 `--`；地速从 m/s 换算为 km/h，定位等级取 `GPSRAW.fix_type`。 |
| 上中与中心 | 琥珀色返航箭头和距离、红色圆环准星、白色俯仰梯 | 返航所需信息或姿态过期时隐藏相应图形。红色准星不表示已锁定目标。 |
| 右列 | 飞行模式、跟踪状态、解锁计时、电池整包电压、单节电压、电流、已耗 mAh | 单节电压与已耗电量没有可信输入时显示 `--`；不按电压猜电芯节数。 |
| 左下与底部 | 可信剩余比例电量条、本地时钟、帧序号、实时消息、ON/OFF 短提示和独立告警 | 剩余比例未知时隐藏电量条；冻结与故障时中文提示占用消息区，告警保留在右侧。 |
| 视频中的单目标 | 状态色角框；Tracking 另有外角框、中心菱形与标签底板 | 无有效框、等待或冻结时全部清除。算法未提供置信度或编号，画面不会造出百分比、`T01` 或第二目标。 |

`/fpv/tracker_trigger` 的 `frame_id="MISSION"` 表示开始执行拦截任务。收到该值后，右列状态栏由 `TRK TRACKING` 改为 `ON MISSION`，直到本节点向 `/fpv/tracker_state` 发布下一条跟踪状态。普通 `ON`、`OFF` 仍只影响启停和约两秒的短提示。任务文字不改变目标框配色或跟踪状态含义；`TrackerState=3` 仍表示 `TargetLost`（目标丢失），下一条状态为 `3` 时状态栏显示 `TARGET LOST`。

调试状态切换时，使用 `journalctl -u drone-tracker.service -f | grep -E 'Tracker trigger received|Tracker state published|OSD control|OSD display'`。`OSD control` 记录任务置位、状态发布解除任务及停轮请求；`OSD display` 只在成功提交且状态栏文字、跟踪框标签或显示状态变化时输出。`hud` 是右上角文字，`target_label` 是当前单目标框标签，空字符串表示没有框标签。收到 `OFF` 后，预期看到 `event=end_round status=Stopped`，随后看到 `OSD display status=Stopped has_video=1 new_video=0 boxes=0 target_label=""`；`has_video=1` 表示保留最后的视频底图，并非仍在采集。当前实现仍可能保留上一帧的 `hud="TRK TRACKING"`，应以停止事件和显示状态判断是否停止。

电流取自 `/mavros/battery`。HUD 对有效电流显示绝对值，例如输入 `-3.0 A` 显示 `CUR 03.0A`；未知或非有限值显示 `CUR --A`。该显示转换不修改原始 ROS 消息。

严重告警为红色，约每 300 毫秒切换可见性；连接异常为琥珀色。`cached-overlay` 只重画变化区域，`legacy` 每次完整绘制，两者使用同一元素定义。可重复生成的纯 CPU 快照、差异图及生成命令保存在 HUD 对齐变更目录。参考快照里的两个人造目标与虚构置信度仅用于识别视觉差异。

内部计数 `submitted/replaced/displayed/refreshes` 分别表示接受的新包、槽内替换包、实际显示的新包和成功画面提交。节点日志将最后一项标为 `commits`。`refreshes` 是提交次数，不是显示器物理扫描次数；屏幕刷新率另由模式信息报告。相同帧重复提交不能计为新视频帧率。

节点另报告累计 `submit_calls`（提交调用次数）、`submit_mean_us`（平均微秒）和 `submit_max_us`（最大微秒）。统计覆盖完整提交调用及被替换包的释放，不包括包构造或硬件完成等待。采样期间仍可能发生新提交，累计值用于运行诊断；它们不代表相机到屏幕的端到端延迟。

## 部署与回退

1. 为当前 OSD 关闭版本保留独立安装目录和配置，记录二进制及系统 Runtime 的 SHA-256。
2. 把 OSD 开启版本安装到另一个目录，检查 `ldd -r` 无缺库或未解析符号。
3. 停止桌面服务并确认设备释放，再启动 OSD。先用受控输入验证，再用已批准模型和明确相机/视频运行。正式验收必须记录具体生产板、模型指纹和输入身份。
4. 同一节点进程至少执行 20 轮 ON/OFF，再额外持续运行至少 10 分钟，并比较相同输入下 OSD 开关的吞吐和资源占用。

生产板已验证的桌面服务为 `lightdm`。在板端执行以下顺序；`sudo` 表示由有权管理服务的部署人员操作：

```sh
sudo systemctl stop lightdm
systemctl is-active lightdm  # 预期 inactive；该状态的退出码为非零
sudo cat /sys/kernel/debug/dri/0/clients
# /dev/dri/card0 对应上面的 0；预期只剩表头，没有客户端行。
# 若仍有客户端，查明占用者并正常结束，确认释放后再启动节点。
source /opt/ros/humble/setup.bash
source /path/to/install/setup.bash
ros2 run drone_tracker drone_tracker_node --ros-args \
  --params-file /path/to/rk3576_video_params.yaml -p osd.enabled:=true
# 节点退出后执行；采集 OFF 时不能执行恢复操作。
sudo cat /sys/kernel/debug/dri/0/clients
# 确认 OSD 已退出且列表只有表头后，按需恢复桌面。
sudo systemctl start lightdm
systemctl is-active lightdm  # 预期 active
```

OSD 整个运行期间保持桌面关闭。OFF 只停止采集，HDMI 仍显示冻结底图并更新遥测，因此继续占有显示设备。正确顺序是“停止桌面 → 确认设备释放 → 启动 OSD → 退出并释放显示资源 → 按需恢复桌面”。若退出后仍有显示客户端，先查明并正常释放，不立即恢复桌面。

回退方法一：在同一安装中设置 `osd.enabled=false` 并重启。回退方法二：若缺少显示动态库导致进程无法启动，切回已保留的 OSD 关闭安装和配置。两种方法均不更换系统 RKNN 库。恢复桌面之前，均须确认原 OSD 进程退出并释放显示设备。

新 HUD 部署时先保留当前 `/opt/drone_tracker/current` 所指发布目录、配置和模型指纹，再把新包部署到独立发布目录。若屏幕不可读、配对异常或负载不达标，切回原 `current` 目标和原配置，并确认 OSD 进程已退出后再恢复桌面。当前实施与实机验收记录见 [HUD 对齐验证记录](../openspec/changes/archive/2026-09-27-align-hud-with-rk-hdmi-osd/validation.md)。模拟测试、RGA 调用成功或参考程序曾经运行，均不能替代本项目的屏幕配对和生产负载验收。
