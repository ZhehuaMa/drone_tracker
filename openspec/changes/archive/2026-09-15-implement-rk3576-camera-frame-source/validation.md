# RK3576 独立相机帧源验证记录

## 范围与结论

变更：`implement-rk3576-camera-frame-source`。本轮实施日期为 2026-09-15。

当前进度 **29/29**：首次实施完成 25 项；用户随后确认执行开发板 2 实机验收，现已完成 6.3–6.6。实机证据见 [开发板 2 验收记录](board2-validation.md)。

已实现独立 AR0234 连续灰度帧源、显式工厂能力开关、设备故障注入、时间映射和独立报告工具。正式 ROS 工厂继续拒绝相机。输出帧使用独立主机内存，消费者持帧不会占用相机驱动缓冲。

首次实施时，用户要求暂不执行开发板 2 运行任务，因此该阶段未连接开发板或执行板端程序。随后用户明确确认执行实机验收，已在开发板 2 复核相机连接，并开展独立采集；当前结果以 [实机续做记录](board2-validation.md) 为准。以下容器构建章节保留首次实施的证据。

本轮使用本地既有 `roc-rk3576-ros2-build:humble-arm64` 镜像中的 Linux ARM64 环境。所有容器均使用 `--rm`，未修改或重建镜像。没有使用真实模型、录像或相机输入；输入是确定的合成灰度像素、生成的 MJPEG 视频和模拟模型接口。MJPEG 表示逐帧 JPEG 压缩的视频。源码流程同时对照本地 `ar0234_osd_hdmi/hw/camera.cpp`，没有修改参考仓库。

## 实现与规格对应

| 规格要求 | 实现与验证 |
|---|---|
| 显式配置、拒绝不支持选项 | 公共配置末尾追加三个字段；`validateCameraConfig()` 在设备访问前检查，保留原聚合初始化顺序 |
| 传感器设置后核对实际值 | 参数数组执行 `media-ctl`，再读取子设备 pad 0 的格式和帧间隔；不同尺寸、帧率、格式被拒绝 |
| 创建阶段不采集 | 工厂创建仅设置/核验并关闭设备；测试检查没有申请缓冲或开流 |
| 缓冲生命周期 | 申请 MMAP 缓冲，即把驱动内存映射到进程；全部预入队后开流；复制后归还；部分失败逆序清理 |
| 停流失败 | 记录错误，先关闭采集设备，再解除映射；测试检查调用顺序 |
| 有效像素及独立寿命 | 1920×1200、stride=2048、非零偏移输入逐像素校验；stride 是相邻两行起点之间的字节数；复用并释放原缓冲后，保留的视图仍有原像素 |
| 元数据防越界 | 覆盖索引、平面数量、偏移、bytesused、映射长度、错误 flags，以及截短帧；访问像素前失败 |
| 时间策略 | 夹取单调时钟样本计算固定偏移；覆盖驱动单调时间、未知/COPY 回退、校时、重复、倒退、策略切换、非法时间和溢出 |
| 队列与停止 | 保留容量参数 30 和不扩容入队；满时丢新帧；测试覆盖单消费者并发停止、旧帧寿命、重启清队列及停止不超过 250 ms |
| 故障后重启 | 覆盖开流、出队、归还、停流和线程启动失败后的再次启动；返回空线程也被拒绝 |
| 录像拒绝 | 启用录像抛出可见异常，禁用安全接受 |
| 工厂边界 | 默认工厂拒绝相机，显式开启加替身设备返回 Ready；完整装配测试直接检查 `CreatePlatformFactory()` 仍拒绝相机；视频和模型测试继续通过 |
| 独立工具 | 公共工厂创建/取帧；两实例各两轮的合成输入生成配置、逐帧/资源/轮次报告和逐像素正确的无损 PGM 快照；已有目录不被覆盖 |
| 输出错误 | 报告流检查写入及关闭状态；故障回调使用 `/dev/full` 模拟磁盘写入失败，状态保留失败原因 |

`CameraSystem` 是系统调用替身接口。替身执行真实 `CameraDevice` 的资源流程，只替换打开设备、ioctl、映射、poll、时钟和配置工具行为。不是只测试一个绕过设备层的模拟帧源。初始化失败测试分别触发工具执行、传感器查询、设备能力、格式、事件句柄、缓冲申请、第三个映射、入队和开流失败，并检查已取得资源释放。

额外使用真实 Linux `eventfd` 与 `poll` 验证事件能在 250 ms 界限内唤醒等待。这里的真实系统调用不涉及相机设备。配置工具执行器另外用普通系统命令验证参数边界、标准错误捕获、工具缺失、非零退出和有限超时。

## 构建与测试证据

构建及日志保留在仓库 `.docker-artifacts/`，该目录不纳入版本库。

| 层次 | 本轮结果 | 日志 |
|---|---|---|
| 相机独立构建 | Linux ARM64，Release，成功 | `camera-configure.log`、`camera-build.log` |
| 相机与公共无设备测试 | 14/14 通过 | `camera-tests.log` |
| 完整 RK3576 独立构建 | 成功；包含既有视频和模型组件 | `camera-regression-configure.log`、`camera-regression-build.log` |
| 完整独立回归 | 33/33 通过，包含相机、视频、公共算法、模型契约和回放测试 | `camera-regression-tests.log` |
| 根目录 ROS ARM64 构建 | `colcon build` 成功，1 个包完成 | `camera-ros-build.log` |
| 根目录 ROS 测试 | 最终 12 项均通过；首轮 10/12，两项包发现失败在加载安装工作区后复测通过，复测还自动运行视频素材准备项，共 3/3 | `camera-ros-tests.log`、`camera-ros-retest.log` |
| OpenSpec 严格校验 | `openspec validate implement-rk3576-camera-frame-source --strict` 通过 | CLI 输出 |

完整独立入口使用既有 RKNN 编译 SDK，仅运行替身测试，不提交 NPU 推理。NPU 是开发板的神经网络计算设备。这里的构建不构成 Runtime 2.3.2 或驱动 0.9.8 的实机证据。

### 依赖与静态检查

独立相机配置额外设置 `RKNN_INCLUDE_DIR=/nonexistent`、`RKNN_LIBRARY=/nonexistent`，仍成功配置和构建。该入口未包含 `dependencies.cmake`，不发现 RKNN、ROS、RGA、DRM 或 Jetson SDK。

`ldd` 显示相机工具直接使用 OpenCV、JPEG 和 FFmpeg 等系统库，没有 RKNN、CUDA、TensorRT、VPI 或 Tegra 依赖。系统 FFmpeg 间接引入 `libva-drm` 和 `libdrm` 运行库；这不是相机代码的 DRM 开发依赖，工具不调用显示链或打开 DRM 设备。不将本次结果写成“动态依赖中完全没有 DRM 库”。完整依赖清单见 `camera-tools-and-linkage.log`。

镜像没有 `clang-tidy` 和 `clangd`，仓库未配置相应检查命令，因此没有运行这两项静态分析。相机独立构建使用 `-Wall -Wextra` 编译警告检查；新增翻译单元没有警告。日志中的三个警告来自既有 `src/hybrid_tracker/tests/test_main.cpp`，涉及多行注释及两个未使用变量，本次没有修改该文件。编译警告检查不能替代上述静态分析。

### 可复现命令

从仓库根目录按 `AGENTS.local.md` 创建临时容器。以下为本轮使用的挂载方式，构建结果在容器删除后保留：

```bash
mkdir -p .docker-artifacts/{build,install,log}
docker run --rm \
  --mount type=bind,src="${PWD}",dst=/work/ros2_ws/src/drone_tracker \
  --mount type=bind,src="${PWD}/.docker-artifacts/build",dst=/work/ros2_ws/build \
  --mount type=bind,src="${PWD}/.docker-artifacts/install",dst=/work/ros2_ws/install \
  --mount type=bind,src="${PWD}/.docker-artifacts/log",dst=/work/ros2_ws/log \
  --workdir /work/ros2_ws roc-rk3576-ros2-build:humble-arm64 bash -lc '
    cmake -S src/drone_tracker/src/platform/rk3576 -B build/camera-only \
      -DDRONE_TRACKER_CAMERA_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
      -DRKNN_INCLUDE_DIR=/nonexistent -DRKNN_LIBRARY=/nonexistent \
      -DCMAKE_CXX_FLAGS="-Wall -Wextra" &&
    cmake --build build/camera-only -j4 &&
    ctest --test-dir build/camera-only --output-on-failure
  '
```

完整独立回归在相同挂载的临时容器中执行：

```bash
cmake -S src/drone_tracker/src/platform/rk3576 -B build/camera-regression \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/camera-regression -j4
ctest --test-dir build/camera-regression --output-on-failure
```

根目录 ROS 构建在新的临时容器中补装约定依赖，容器退出后补装内容删除：

```bash
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  ros-humble-vision-msgs ros-humble-mavros-msgs ros-humble-cv-bridge
source /opt/ros/humble/setup.bash
colcon build --packages-select drone_tracker --cmake-clean-cache --cmake-args \
  -DDRONE_TRACKER_PLATFORM=rk3576 -DDRONE_TRACKER_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
LD_LIBRARY_PATH=/opt/rknn-runtime/lib:$LD_LIBRARY_PATH \
  ctest --test-dir build/drone_tracker --output-on-failure
```

首轮测试遗漏了 `source install/setup.bash`，因此包发现失败。复测只重跑失败项，命令为上述加载环境后执行 `ctest --test-dir build/drone_tracker --rerun-failed --output-on-failure`。此修正不涉及源码或开发板环境。

随后补充工具慢消费等待期间的每秒资源采样与故障响应。该小范围修改重新构建工具，并通过三项工具测试，记录为 `camera-driver-final-build.log`、`camera-driver-final-tests.log`。这不改变此前设备层、视频或 ROS 的测试范围。

### 产物位置

- `.docker-artifacts/build/camera-only/camera_source_driver`：无需 RKNN SDK 的 Linux ARM64 工具。
- `.docker-artifacts/build/camera-only/camera-driver-fixtures/normal/`：无设备工具测试生成的四轮报告及 PGM 快照，不是相机实拍。
- `.docker-artifacts/build/camera-regression/`：完整独立构建及视频素材。
- `.docker-artifacts/install/drone_tracker/`：根目录 ROS 构建产物。
- `.docker-artifacts/log/camera-*.log`：上述命令的构建、测试及依赖检查日志。

## 首次实施时未执行的实机检查（历史）

以下为首次实施时的缺口。后续完成情况见 [实机续做记录](board2-validation.md)，不能用容器或既有板端历史记录替代：

1. **6.3**：开发板 2 连接相机后的拓扑、格式、驱动和设备占用复核。
2. **6.4**：真实短采集的同帧有效像素对照、快照、时间来源及偏移采样跨度记录。曝光同步尚未验证。
3. **6.5**：至少 20 轮启停、100 次新建销毁，逐轮停止不超过 1 秒，无线程或描述符累积。
4. **6.6**：至少 10 分钟真实采集及慢消费负载，解释资源变化，分别记录驱动序号缺口和队列满丢帧。

设备恢复和运行命令见 [平台使用说明](../../../src/platform/README.md#rk3576-独立原生灰度相机源)。相机配置会改变传感器实际格式，停止后不声称恢复其他应用原有设置。

本次不修改前序完整回放的 7.2、7.3 模型质量任务，不宣称 ROS 相机接入、Jetson 实机等价性或部署性能通过。上述首次实施阶段的实机缺口已由续做补齐；当前变更具备归档条件，尚未执行归档。
