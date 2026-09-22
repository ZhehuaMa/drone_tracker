# Proposal

## Why

RK3576 目标平台即将接入第二款相机（型号 SC233HGS），它使用完全不同的驱动栈、不同的媒体总线格式和不同的采集像素格式（详见 design.md）。但目前 `src/platform/rk3576/` 目录下唯一一套相机采集实现的文件名和类型名都只叫"camera"（例如 `camera_device.cpp`、`CameraDevice`、`Rk3576CameraFrameSource`），完全没有体现它其实是专门为现有 AR0234 相机（一款测试用工业相机型号）写的：里面硬编码了 AR0234 私有的软件触发控制寄存器地址和固定的采集格式假设。如果不先把这批文件和类型改成能明确表明"这是 AR0234 专属实现"的名字，后续新增第二款相机的代码时，工程师和评审者都无法从命名上判断一段相机代码到底服务于哪款硬件，容易在维护和排查问题时张冠李戴，也会让"新相机能否复用这段代码"这个问题被掩盖。

这次变更只做改名和文档同步，不改变任何采集行为、协议或对外接口；目的是先把边界画清楚，为后续正式接入新相机的实现工作打好基础。

## What Changes

- 将当前 AR0234 专属的相机采集实现从"通用相机"命名迁移为"AR0234 专属"命名，覆盖文件名、C++ 类型名、命名空间下的模块、CMake 构建目标名、头文件 include guard，以及提及这些标识符的文档段落。
- 具体范围：
  - `src/platform/rk3576/camera_device.{h,cpp}` → `ar0234_camera_device.{h,cpp}`；类 `CameraDevice` → `Ar0234CameraDevice`。
  - `src/platform/rk3576/camera_frame_source.{h,cpp}` → `ar0234_camera_frame_source.{h,cpp}`；类 `Rk3576CameraFrameSource` → `Ar0234CameraFrameSource`。
  - 配套类型 `CameraDependencies`、`CameraDiagnostic`、`CameraTriggerPlan`、`CameraTriggerEvent` 分别改名为 `Ar0234CameraDependencies`、`Ar0234CameraDiagnostic`、`Ar0234TriggerPlan`、`Ar0234TriggerEvent`。
  - 独立验收工具及其测试文件：`tests/camera_device_test.cpp`、`tests/camera_frame_source_test.cpp`、`tests/camera_driver_support.{h,cpp}`、`tests/camera_driver_test.cpp`、`tests/camera_source_driver.cpp` 全部加上 `ar0234_` 前缀；命名空间 `rk3576::camera_driver` 改为 `rk3576::ar0234_camera_driver`。
  - CMake 构建目标 `rk3576_camera` 改名为 `rk3576_ar0234_camera`；`camera_driver_support`/`camera_source_driver` 目标及对应的 `camera_device_tests`/`camera_frame_source_tests`/`camera_driver_tests` 测试目标同步加上 `ar0234_` 前缀；`camera_standalone.cmake`/`camera_tests.cmake` 两个 CMake 片段文件改名为 `ar0234_camera_standalone.cmake`/`ar0234_camera_tests.cmake`。
  - 同步更新 `src/platform/README.md` 中提到上述类型名的段落，使文档与代码一致。
- **不在本次范围内**（明确保留，不改名）：
  - `CameraSystem`（纯 V4L2 系统调用封装，与传感器型号无关，预期未来可被新相机复用）。
  - `camera_timing.{h,cpp}`、`CameraTiming`、`CameraClockPair`（纯时钟域映射算法，与传感器型号无关）。
  - `CameraSample`、`CameraFormat`（公共采集结果结构体；是否归入 AR0234 专属留待后续接入新相机时再评估，本次保持通用命名）。
  - `tests/camera_timing_test.cpp`、`tests/camera_test_support.h`（跨相机共用的测试辅助文件，本次不拆分）。
  - `FrameSourceConfig` 中的字段名（`camera_media_device`、`camera_sensor_entity`、`camera_buffer_count`、`camera_trigger_rate_hz` 等）：这些字段名本身是通用 V4L2 概念的命名，语义上是否适用于新相机留给后续相机接入变更处理，本次不改。
- 本次不新增任何抽象基类或接口层，不改变任何采集行为、时序、错误处理或对外可观察的 ROS/CLI 行为。**BREAKING**：无外部可观察行为变化，但会破坏任何直接引用上述 C++ 类型名、头文件路径或 CMake 目标名的下游代码（构建脚本、CI 脚本等），需要同步更新。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

无——本次是纯粹的标识符重命名和内部文件重组，不改变任何已发布规格描述的可观察行为（相机配置校验规则、采集格式协商、时间戳语义、ROS 话题/参数等均不受影响）。因此本变更的 `.openspec.yaml` 已设置 `skip_specs: true`，不创建 delta spec 文件。

## Impact

- **受影响代码**：`src/platform/rk3576/camera_device.{h,cpp}`、`src/platform/rk3576/camera_frame_source.{h,cpp}`、`src/platform/rk3576/platform_factory.{h,cpp}`（引用 `Rk3576CameraFrameSource`/`CameraDependencies` 的实例化点）、`src/platform/rk3576/tests/` 目录下的相机相关测试与验收工具、`src/platform/rk3576/camera_standalone.cmake`、`src/platform/rk3576/camera_tests.cmake`、`src/platform/rk3576/CMakeLists.txt`、`src/platform/rk3576/backend.cmake`、`src/platform/README.md`。
- **不受影响**：ROS 节点对外的话题、参数、`TrackerState` 语义；`camera_timing.*`；`FrameSourceConfig`/`FrameSourceStatus` 的字段和语义；Jetson 分支代码（本变更只涉及 `rk3576` 分支）。
- **依赖/构建**：任何直接以旧 CMake 目标名（如 `rk3576_camera`、`camera_device_tests`）作为依赖或在 CI 脚本中引用的地方，需要同步更新为新目标名；本仓库当前未发现可见 CI 配置引用这些目标名，但需要在 tasks 中逐一核实。
