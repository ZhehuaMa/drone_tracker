# Tasks

## 1. 重命名相机设备层（AR0234 私有触发与格式协商）

- [x] 1.1 将 `src/platform/rk3576/camera_device.h` 重命名为 `ar0234_camera_device.h`：include guard 从 `RK3576_CAMERA_DEVICE_H` 改为 `RK3576_AR0234_CAMERA_DEVICE_H`，类 `CameraDevice` 改名为 `Ar0234CameraDevice`，`CameraDependencies`/`CameraDiagnostic`/`CameraTriggerPlan`/`CameraTriggerEvent` 分别改名为 `Ar0234CameraDependencies`/`Ar0234CameraDiagnostic`/`Ar0234TriggerPlan`/`Ar0234TriggerEvent`；`CameraSystem`、`CameraSample`、`CameraFormat` 保持原名不变。在文件顶部新增一行注释说明"`CameraSample`/`CameraFormat` 为跨相机通用结构体，其余以 `Ar0234` 开头的类型为本文件专属"。验证：文件存在于新路径，`grep -n "class CameraDevice\b"` 在源码树中零命中。
- [x] 1.2 将 `src/platform/rk3576/camera_device.cpp` 重命名为 `ar0234_camera_device.cpp`，同步更新其 `#include "camera_device.h"` 为 `#include "ar0234_camera_device.h"`，以及文件内所有对 1.1 中已改名类型的引用。验证：与旧文件逐行 diff（`diff <(git show HEAD:src/platform/rk3576/camera_device.cpp) src/platform/rk3576/ar0234_camera_device.cpp`）确认差异只涉及标识符替换，无逻辑变化。

## 2. 重命名相机帧源层（生产者/消费者调度与触发计划）

- [x] 2.1 将 `src/platform/rk3576/camera_frame_source.h` 重命名为 `ar0234_camera_frame_source.h`：include guard 从 `RK3576_CAMERA_FRAME_SOURCE_H` 改为 `RK3576_AR0234_CAMERA_FRAME_SOURCE_H`，类 `Rk3576CameraFrameSource` 改名为 `Ar0234CameraFrameSource`，`#include "camera_device.h"` 改为 `#include "ar0234_camera_device.h"`，`CameraTriggerPlan`/`CameraTriggerEvent`/`CameraDependencies`/`CameraDiagnostic` 的使用点同步改为 1.1 中的新名字。验证：文件存在于新路径，`grep -n "Rk3576CameraFrameSource"` 在源码树中零命中。
- [x] 2.2 将 `src/platform/rk3576/camera_frame_source.cpp` 重命名为 `ar0234_camera_frame_source.cpp`，同步更新其 `#include` 路径和内部类型引用。验证：与旧文件逐行 diff 确认差异只涉及标识符替换。

## 3. 更新平台工厂对相机类型的引用

- [x] 3.1 更新 `src/platform/rk3576/platform_factory.h`：`#include "camera_frame_source.h"` 改为 `#include "ar0234_camera_frame_source.h"`，`CameraDependencies camera{}` 字段改为 `Ar0234CameraDependencies camera{}`。验证：该文件编译不再依赖旧头文件路径。
- [x] 3.2 更新 `src/platform/rk3576/platform_factory.cpp`：`std::make_unique<Rk3576CameraFrameSource>(...)` 改为 `std::make_unique<Ar0234CameraFrameSource>(...)`。验证：`grep -rn "Rk3576CameraFrameSource\|CameraDependencies\b" src/platform/rk3576/platform_factory.*` 零命中（`Ar0234CameraDependencies` 除外）。

## 4. 更新 CMake 构建脚本

- [x] 4.1 将 `src/platform/rk3576/camera_standalone.cmake` 重命名为 `ar0234_camera_standalone.cmake`，更新其中 `add_library(rk3576_camera STATIC camera_device.cpp camera_timing.cpp camera_frame_source.cpp)` 为 `add_library(rk3576_ar0234_camera STATIC ar0234_camera_device.cpp camera_timing.cpp ar0234_camera_frame_source.cpp)`（`camera_timing.cpp` 保持原名，见 design.md 决策 1），并更新引用该目标名的 `target_link_libraries` 语句。验证：文件存在于新路径，`grep -rn "rk3576_camera\b" src/platform/rk3576/*.cmake src/platform/rk3576/CMakeLists.txt` 零命中（`rk3576_ar0234_camera` 除外）。
- [x] 4.2 将 `src/platform/rk3576/camera_tests.cmake` 重命名为 `ar0234_camera_tests.cmake`：`foreach(camera_test camera_device camera_timing camera_frame_source)` 拆分为对 `ar0234_camera_device`、`camera_timing`（保持原名）、`ar0234_camera_frame_source` 分别处理测试目标命名（`ar0234_camera_device_tests`、`camera_timing_tests`、`ar0234_camera_frame_source_tests`）；`camera_driver_support`/`camera_source_driver`/`camera_driver_tests` 库、可执行文件及 ctest 用例名（`camera_driver_missing_args`/`camera_driver_invalid_args`）同步加 `ar0234_` 前缀。验证：文件存在于新路径；`ctest -N`（在配置好的构建目录下）列出的用例名均已带 `ar0234_` 前缀（除 `camera_timing_tests`）。
- [x] 4.3 更新 `src/platform/rk3576/CMakeLists.txt` 与 `src/platform/rk3576/backend.cmake` 中对 `camera_standalone.cmake`/`camera_tests.cmake` 的 `include()` 调用，以及 `add_library(rk3576_camera ...)`/`target_link_libraries(... rk3576_camera)` 等语句，改为新文件名和新目标名。验证：`grep -rn "camera_standalone\.cmake\|camera_tests\.cmake\|rk3576_camera\b" src/platform/rk3576/CMakeLists.txt src/platform/rk3576/backend.cmake` 零命中（新名字除外）。

## 5. 重命名测试与独立验收工具文件

- [x] 5.1 将 `tests/camera_device_test.cpp` 重命名为 `tests/ar0234_camera_device_test.cpp`，更新其中对 `CameraDevice`/`CameraDependencies` 等已改名类型的引用。验证：与旧文件逐行 diff 确认差异只涉及标识符替换。
- [x] 5.2 将 `tests/camera_frame_source_test.cpp` 重命名为 `tests/ar0234_camera_frame_source_test.cpp`，更新其中对 `Rk3576CameraFrameSource` 等已改名类型的引用。验证：与旧文件逐行 diff 确认差异只涉及标识符替换。
- [x] 5.3 将 `tests/camera_driver_support.h` 重命名为 `tests/ar0234_camera_driver_support.h`：include guard 从 `RK3576_CAMERA_DRIVER_SUPPORT_H` 改为 `RK3576_AR0234_CAMERA_DRIVER_SUPPORT_H`，命名空间 `rk3576::camera_driver` 改为 `rk3576::ar0234_camera_driver`，`Options`/`parse`/`run` 中对 `CameraDependencies` 的引用改为 `Ar0234CameraDependencies`。验证：文件存在于新路径。
- [x] 5.4 将 `tests/camera_driver_support.cpp` 重命名为 `tests/ar0234_camera_driver_support.cpp`，同步更新 `#include`、命名空间和类型引用。验证：与旧文件逐行 diff 确认差异只涉及标识符替换。
- [x] 5.5 将 `tests/camera_driver_test.cpp` 重命名为 `tests/ar0234_camera_driver_test.cpp`，更新 `#include "camera_driver_support.h"` 为 `#include "ar0234_camera_driver_support.h"` 及命名空间引用。验证：与旧文件逐行 diff 确认差异只涉及标识符替换。
- [x] 5.6 将 `tests/camera_source_driver.cpp` 重命名为 `tests/ar0234_camera_source_driver.cpp`，更新 `#include` 路径、命名空间引用，以及帮助文本中的可执行文件名（`camera_source_driver` → `ar0234_camera_source_driver`）。验证：与旧文件逐行 diff 确认差异只涉及标识符和帮助文本替换。

## 6. 更新共享测试辅助文件（文件名不变）

- [x] 6.1 更新 `tests/camera_test_support.h`：将 `#include "../camera_frame_source.h"` 改为 `#include "../ar0234_camera_frame_source.h"`，将文件内对 `CameraDevice`/`CameraDependencies` 的引用（含第 40 行注释、`dependencies()` 辅助函数）改为 `Ar0234CameraDevice`/`Ar0234CameraDependencies`。本文件名保持 `camera_test_support.h` 不变，因为它同时被不改名的 `camera_timing_test.cpp` 共用（见 design.md 决策 1）。验证：`grep -n "rk3576::CameraDevice\|rk3576::CameraDependencies"` 在该文件中零命中。

## 7. 同步文档

- [x] 7.1 更新 `src/platform/README.md` 中提到 `CameraDependencies::observe`、`CameraDevice`、`Rk3576CameraFrameSource` 等标识符的段落，改为对应的 `Ar0234` 前缀新名字。验证：`grep -n "CameraDependencies\|Rk3576CameraFrameSource" src/platform/README.md` 零命中（`Ar0234CameraDependencies` 除外）。

## 8. 全仓库核实与构建验证

- [x] 8.1 在源码树范围（排除 `.docker-artifacts/` 和 `openspec/changes/archive/` 等历史归档目录）搜索确认零残留：`grep -rn "\bCameraDevice\b\|Rk3576CameraFrameSource\|\bCameraDependencies\b\|\bCameraDiagnostic\b\|\bCameraTriggerPlan\b\|\bCameraTriggerEvent\b" src/ launch/ parameters/` 均无命中（除已确认应保留的 `CameraSample`/`CameraFormat`/`CameraSystem`/`CameraTiming`/`CameraClockPair`)。验证：命令输出为空。
- [x] 8.2 检查 `launch/`、`parameters/`、`Dockerfile`（若存在）等是否有脚本直接引用旧 CMake 目标名（`rk3576_camera`、`camera_device_tests`、`camera_frame_source_tests`、`camera_driver_support`、`camera_source_driver`、`camera_driver_tests`）。如有引用，同步更新为新目标名；如无引用，在本任务勾选说明"未发现引用"。验证：搜索命令输出记录在提交说明或 PR 描述中。
- [x] 8.3 在匹配的 RK3576 ARM64 构建环境下执行 `colcon build --packages-select drone_tracker --cmake-args -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON` 并运行 `ctest`（或按 `AGENTS.local.md` 指定的本地构建方式），确认 `ar0234_camera_device_tests`、`ar0234_camera_frame_source_tests`、`camera_timing_tests`、`ar0234_camera_driver_tests` 等测试全部通过，且断言内容未被迫修改（除非断言字符串直接包含旧类型名）。验证：`ctest` 输出全部为 `Passed`；记录本次验证所用的构建环境、编译器版本，以及因缺少 RK3576 硬件而未运行的检查项（例如 `camera_source_driver` 现场采集验收）。
