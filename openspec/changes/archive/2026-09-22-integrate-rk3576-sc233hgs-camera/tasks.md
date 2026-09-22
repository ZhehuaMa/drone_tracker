# Tasks

## 1. 抽取共享 V4L2 采集基类（不改变 AR0234 行为的纯重构）

- [x] 1.1 在 `src/platform/rk3576/` 新增共享基类（例如 `rk3576_camera_capture_base.h/.cpp`），把 `ar0234_camera_device.cpp` 中与传感器型号无关的内存映射缓冲区申请/出队入队轮询/停止唤醒逻辑迁移进去，通过一个策略接口（格式协商、控制项读写、Gray8 提取）向具体传感器开放扩展点；完成后 `ar0234_camera_device.cpp` 改为基于该基类实现 AR0234 专属策略
- [x] 1.2 重新运行现有 `ar0234_camera_device`、`ar0234_camera_frame_source`、`camera_timing`、`ar0234_camera_driver_tests`（`ctest -R ar0234`）等全部既有 AR0234 测试，确认其全部通过且没有修改任何既有断言，作为本次重构"行为不变"的回归证据

## 2. 实现 SC233HGS 采集设备与帧源

- [x] 2.1 新增 `sc233hgs_camera_device.h/.cpp`，基于任务 1.1 的共享基类实现 SC233HGS 专属策略：仅协商 NV12 采集格式、不读写任何触发相关控制项、从驱动缓冲区提取 Y 平面并转换为 Gray8 输出，并在打开时校验 `capture_mode` 为 continuous（非 continuous 时初始化失败并给出说明）
- [x] 2.2 新增 `sc233hgs_camera_frame_source.h/.cpp`，复用 `CameraSystem`、`camera_timing.h` 等既有传感器无关模块，向单一消费者交付带时间戳的帧，行为对齐 `rk3576-sc233hgs-camera-frame-source` 规格中的队列、停止重启及独立验收要求
- [x] 2.3 新增无硬件单元测试（使用可注入的系统调用替身模拟 V4L2 ioctl 与缓冲区），覆盖：格式协商失败即报错并保持未采集状态、NV12 缓冲区提取出的 Y 平面尺寸与逐行跨度正确、队列满时旧帧被丢弃且不阻塞采集线程、停止后可重新启动且不遗留旧帧，运行 `ctest -R sc233hgs` 全部通过

## 3. 平台工厂新增按传感器型号派发

- [x] 3.1 在 `src/platform/platform_factory.h` 的 `FrameSourceConfig` 中新增 `camera_sensor_model` 枚举字段（`Ar0234`/`Sc233hgs`），默认值为 `Ar0234`
- [x] 3.2 修改 `src/platform/rk3576/platform_factory.cpp` 的 `Factory::createFrameSource`，按 `camera_sensor_model` 分支构造 `Ar0234CameraFrameSource` 或新增的 SC233HGS 帧源；未知取值时创建失败并在诊断信息中列出请求值与受支持列表
- [x] 3.3 新增派发逻辑单元测试：验证默认值路径等价于改动前行为、显式请求 `sc233hgs` 得到对应实现且不初始化任何 AR0234 触发控制项、未知取值被拒绝且不回退到任一已知型号

## 4. ROS 节点参数与校验

- [x] 4.1 在 `src/drone_tracker_node.h/.cpp` 中新增 `mv_camera.sensor_model` 参数声明（字符串，默认 `ar0234`），解析并映射为 `camera_sensor_model`；未知取值时节点初始化失败并说明受支持列表
- [x] 4.2 在相机配置校验逻辑中新增约束：`sensor_model=sc233hgs` 且 `capture_mode=software_trigger` 时初始化失败，错误信息说明该型号不支持软件触发,不静默改为 continuous
- [x] 4.3 在参数运行期变更处理路径中新增：拒绝运行期修改 `mv_camera.sensor_model`，返回失败并提示需重启节点
- [x] 4.4 新增/扩展节点级测试覆盖任务 4.1-4.3 的三个分支（合法 SC233HGS 连续配置、SC233HGS 配置软件触发被拒绝、运行期改型号被拒绝），对应 `rk3576-ros-camera` 规格新增的三个 Scenario

## 5. 构建系统集成

- [x] 5.1 更新 `src/platform/rk3576/backend.cmake` 与 `ar0234_camera_standalone.cmake`：新增共享基类源文件目标，新增 `rk3576_sc233hgs_camera` 静态库目标（含 `sc233hgs_camera_device.cpp`、`sc233hgs_camera_frame_source.cpp`），并将其链接进 `rk3576_tracker_assembly`
- [x] 5.2 新增 `sc233hgs_camera_tests.cmake`（参考 `ar0234_camera_tests.cmake` 的结构）注册任务 2.3 的新测试可执行文件
- [x] 5.3 在匹配的 RK3576 交叉编译环境执行 `colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON`，确认新增目标编译通过且 `ctest` 全部通过

## 6. 参数示例与文档

- [x] 6.1 更新 `parameters/rk3576_camera_params.yaml`，新增 `mv_camera.sensor_model` 说明注释及一份 SC233HGS 配置示例（含 `media_device`/`sensor_entity` 仅作诊断标识的说明、`capture_mode` 固定为 continuous 的提示）
- [x] 6.2 检查 `launch/` 下 RK3576 相关 launch 文件，确认无需改动即可透传新参数；如需要显式声明 `sensor_model` 覆盖项，补充对应的 launch 参数声明

## 7. 生产板实机验收（需要真实 SC233HGS 硬件，本次变更范围内不可离线完成）

- [x] 7.1 在生产开发板上执行 `media-ctl -p`，核实实际的 `/dev/videoM`、`/dev/mediaN` 拓扑值并写入正式部署参数文件
- [x] 7.2 用真实驱动确认 rkisp_mainpath 节点协商的 NV12 缓冲区实际 plane 数量（单平面或 `NV12M` 双平面），如与任务 2.1 的假设不符，调整缓冲区管理代码后重新运行任务 2.3 的测试
- [x] 7.3 在生产板上完成开机到出现首帧的实测,确认现有采集超时阈值在 ISP 3A 收敛期间是否仍然适用；如证实过紧，记录结果并另行发起 OpenSpec 变更调整阈值

实机证据（生产板 `firefly-production`，2026-09-22）：

- `media-ctl -d /dev/media0 -p` 显示 SC233HGS 实体为 `m00_b_sc233hgs 0-0030`，输入为 1920×1200 的 `Y10_1X10`；对应 ISP 拓扑为 `/dev/media2` 的 `rkisp_mainpath=/dev/video22`。
- `v4l2-ctl -d /dev/video22 --get-fmt-video` 显示 1920×1200、`NV12`、单平面、Y 行跨度 1920 字节；未发现 `NV12M` 双平面协商。
- 连续 5 次从停止状态启动 `/dev/video22` 并采集首帧，均成功，耗时 192–195 ms，单帧大小 3456000 字节；首帧时间均早于 2 秒采集超时阈值。按当前证据无需调整超时，也无需另行发起 OpenSpec 变更。
- `/dev/video-camera0` 当前指向 `/dev/video31`，其实际格式为 800×600，属于另一条 ISP 链路；正式 SC233HGS 参数使用 `/dev/video22`，不能使用该稳定别名。
