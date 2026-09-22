# Design

## Context

现在的问题：`src/platform/rk3576/` 目录下唯一一套相机采集实现，文件和类型都叫"camera"，没有任何标识表明它是专门写给 AR0234（当前接入的测试相机型号）的。这带来两个具体后果：

1. 阅读 `camera_device.cpp` 时，工程师看不出里面 `TRIGGER_MODE`/`TRIGGER_SOURCE`/`SOFTWARE_TRIGGER`/`FRAME_RATE` 这几个私有寄存器地址（0x00981901~0x00981904）和固定的 `Y8_1X8` 媒体总线格式协商，其实是 AR0234 所用的 VEYE "mvcam" 厂商私有 subdev 驱动的专属约定，换一款传感器基本不能直接复用。
2. 后续要接入的第二款相机（型号 SC233HGS）已经在参考项目 `rk_hdmi_osd`（`hw/camera.h`/`hw/camera.cpp`）中有实现可以对照：它走内核 in-tree 驱动（非厂商私有），媒体总线格式是 `Y10_1X10`（10 位），采集像素格式是 `V4L2_PIX_FMT_Y10`（10 位位流打包，需要 CPU 解包成 8 位灰度），设备发现方式是扫描 `/dev/media0..7` 按传感器实体名动态定位，而不是像 AR0234 那样固定 `/dev/video0` 和固定实体名字符串。更关键的是，参考实现里完全没有出现 AR0234 那 4 个私有触发寄存器，只用连续采集配合驱动的 SOF（帧起始）硬中断时间戳——软件触发这条路径本身可能就是 AR0234 专属的，不能假设新相机也支持。

综合以上两点，`CameraDevice` 类里的传感器配置逻辑（`configureSensor()`/`setControl()`/`writeAndVerifyControl()`）和 `Rk3576CameraFrameSource` 依赖的触发调度（`CameraTriggerPlan`/`trigger()`）都应视为 AR0234 专属，不应该用暗示"通用相机"的名字。

预期结果：完成本次变更后，任何人查看文件名或类型名就能直接判断"这段代码只服务于 AR0234"，为后续正式接入 SC233HGS 的独立变更留出清晰的命名空间，不需要再纠结"这是不是可以复用的通用相机代码"。

设计范围只覆盖标识符层面的重命名和文件重新组织，技术方案（如何抽象出真正跨相机的公共接口）留给后续接入 SC233HGS 时的变更处理。

## Goals / Non-Goals

**Goals:**
- 让所有专属于 AR0234 采集流程的文件名、C++ 类型名、命名空间、CMake 构建目标名、include guard 都带上明确的 `ar0234`/`Ar0234` 标记。
- 保证重命名前后行为完全一致：同一组 V4L2 ioctl 调用序列、同一套错误信息文本内容（除类型名本身出现在错误信息里的情况）、同一套时间戳映射逻辑、同一套触发计划算法。
- 保证 `colcon build --packages-select drone_tracker` 与 `-DDRONE_TRACKER_BUILD_TESTS=ON` 下的 `ar0234_camera_device_tests`、`ar0234_camera_frame_source_tests`、`camera_timing_tests`（不改名）、`ar0234_camera_driver_tests` 等测试全部通过，且测试断言本身不需要修改（除非断言直接依赖旧类型名的字符串，如 `typeid` 或日志前缀）。

**Non-Goals:**
- 不设计新相机（SC233HGS）的具体接入方案——那是另一次变更的范围，只有本次积累的"AR0234 专属边界"会被它复用。
- 不抽取 `CameraDevice`/`Rk3576CameraFrameSource` 与未来新相机实现之间的公共基类或接口。是否需要这样的抽象、抽象该长什么样，取决于 SC233HGS 实现细节，现在做会是没有依据的过度设计。
- 不改变 `FrameSourceConfig`/`FrameSourceStatus`（`platform_factory.h`）里任何字段的名字、类型或语义，即使这些字段目前只被 AR0234 使用。
- 不触碰 Jetson 分支代码。

## Decisions

### 决策 1：改名范围以"是否包含 AR0234 私有寄存器/固定格式假设"为界，而不是以"文件名含 camera 字样"为界

- **选择**：只重命名 `camera_device.*`、`camera_frame_source.*` 及其配套类型（`CameraDependencies`/`CameraDiagnostic`/`CameraTriggerPlan`/`CameraTriggerEvent`），以及围绕它们的测试和验收工具文件；`camera_timing.*`（`CameraTiming`/`CameraClockPair`）和 `CameraSystem` 保持原名不变。
- **为什么**：`camera_timing.*` 做的是"把驱动打的单调时钟时间戳映射到系统时间"这类纯算法，和具体是哪颗传感器无关——参考项目 `rk_hdmi_osd` 里 SC233HGS 走的 `hw::Sof2Epoch` 做的正是同一件事（SOF 时间戳 -> Epoch 时间的线性模型映射）。`CameraSystem` 是 `open`/`ioctl`/`mmap`/`poll`/`execute` 这层最底层的 Linux 系统调用包装，本身不包含任何传感器私有逻辑。给这两者强行加上 `Ar0234` 前缀，只会让未来接入 SC233HGS 时被迫做一次"改回通用名"的返工，或者被迫复制一份几乎相同的代码。
- **被否决的替代方案**：把 `src/platform/rk3576/` 目录下所有含 "camera" 字样的文件统一加前缀（包括 `camera_timing.*`）。这样命名一致性更强，但会把明知与传感器无关的代码也打上专属标签，误导后来者以为这些逻辑不能复用，且未来去掉前缀又是一次无谓的改动。
- **实际影响**：`camera_test_support.h` 因为同时被 `camera_timing_test.cpp`（保留原名）和即将改名的 `ar0234_camera_device_test.cpp`/`ar0234_camera_frame_source_test.cpp` 共用，本次保持文件名不变，只更新其内部对 `CameraDevice`/`CameraDependencies` 等已重命名类型的引用。这是本次唯一一个"文件名不变但内容要改"的例外，已在 tasks 中单列。

### 决策 2：`CameraSample`/`CameraFormat` 暂不归入 AR0234 专属命名

- **选择**：这两个结构体（分别表示"一次出队的采集结果"和"协商后的采集格式"）保留通用名字，不改成 `Ar0234CameraSample`/`Ar0234CameraFormat`。
- **为什么**：用户已经明确这是本次的既定范围（"暂不归入 AR0234 专属"）。这两个结构体的字段（宽高、行跨度、时间戳、序号等）描述的是采集结果的通用形状，不是 AR0234 的私有细节；是否需要在未来因为 SC233HGS 的 10 位打包格式而拆分出不同的结构体，留给那次变更依据实际约束决定，现在改名没有依据。
- **被否决的替代方案**：一并改名以保持"这个文件里的所有类型都是 AR0234 专属"的强一致性。否决原因同决策 1：过早贴标签，且实际语义（"一帧采集结果"）本来就是通用概念。
- **实际影响**：`ar0234_camera_device.h`（重命名后的文件）里会同时存在通用命名的 `CameraSample`/`CameraFormat` 和专属命名的 `Ar0234CameraDevice`，这是本次有意保留的不一致，已在 tasks 中注明原因避免被误"顺手统一"。

### 决策 3：测试与验收工具文件同步改名，命名空间 `rk3576::camera_driver` 改为 `rk3576::ar0234_camera_driver`

- **选择**：`tests/camera_device_test.cpp`、`tests/camera_frame_source_test.cpp`、`tests/camera_driver_support.{h,cpp}`、`tests/camera_driver_test.cpp`、`tests/camera_source_driver.cpp` 全部加 `ar0234_` 前缀；`camera_driver_support.h` 里的命名空间 `rk3576::camera_driver` 同步改为 `rk3576::ar0234_camera_driver`。
- **为什么**：这些测试和独立验收工具（`camera_source_driver` 可执行文件）直接使用 `CameraDependencies`（即将改名为 `Ar0234CameraDependencies`）并且只服务于 AR0234 采集路径的现场验收（连接真实 AR0234 设备采集报告），保留通用名字会造成和被测对象名字不一致，看起来像是"通用相机验收工具"。
- **被否决的替代方案**：只改类型引用，不改文件名/命名空间。会导致 `grep ar0234` 无法定位到这些测试文件，削弱本次改名想要达到的"按名字就能分清楚哪些代码服务于哪款相机"的效果。
- **实际影响**：`tests/camera_timing_test.cpp` 和 `tests/camera_test_support.h` 不改名（原因见决策 1），需要在 tasks 中明确写出"这两个文件不改"，避免执行时被误认为遗漏。

### 决策 4：CMake 构建目标名同步改名，且逐一核对 `.docker-artifacts/` 下的历史构建产物不需要处理

- **选择**：`rk3576_camera` 静态库目标改名为 `rk3576_ar0234_camera`；`camera_driver_support`、`camera_source_driver`、`camera_device_tests`、`camera_frame_source_tests`、`camera_driver_tests` 及对应的 ctest 用例名（`camera_driver_missing_args`/`camera_driver_invalid_args`/`camera_driver_tests`）同步加 `ar0234_` 前缀；`camera_standalone.cmake`/`camera_tests.cmake` 两个 CMake 片段文件改名为 `ar0234_camera_standalone.cmake`/`ar0234_camera_tests.cmake`。
- **为什么**：构建目标名和源文件名一起改，避免出现"文件叫 `ar0234_camera_device.cpp` 但 target 还叫 `rk3576_camera`"这种命名割裂。已确认 `.docker-artifacts/` 目录下存在大量历史构建产物（`CMakeFiles/rk3576_camera.dir/` 等），这些是构建缓存输出，不是源码，重新构建时会自动以新目标名重新生成，不需要手动修改或删除。
- **被否决的替代方案**：保留旧的 CMake 目标名，只改源文件名。会让 `cmake --build . --target rk3576_ar0234_camera` 这种按目标名操作的命令与实际源码脱节，增加维护者查找目标对应源文件的成本。
- **实际影响**：`src/platform/rk3576/CMakeLists.txt`、`src/platform/rk3576/backend.cmake` 中 `include(camera_standalone.cmake)`/`include(camera_tests.cmake)` 及所有 `add_library`/`add_executable`/`add_test` 语句需要同步更新文件名和目标名引用。

## Risks / Trade-offs

- **[风险] 改名范围出现遗漏，导致 `camera_test_support.h` 或某个测试文件残留对旧类型名（如 `CameraDevice`）的引用，编译失败或静默使用了错误符号** → **缓解**：tasks 中把"全仓库搜索确认零残留旧标识符"列为独立、可验证的收尾步骤（`grep -rn` 特定标识符列表，期望只在历史归档目录 `.docker-artifacts/`、`openspec/changes/archive/` 中出现，源码树下为零命中）。
- **[风险] `CameraSample`/`CameraFormat` 保留通用名，而它们所在文件已改名为 `ar0234_camera_device.h`，未来协作者可能誤以为这两个结构体也是 AR0234 专属，或反过来誤以为整份文件都是通用的** → **缓解**：在 `ar0234_camera_device.h` 顶部的文件注释中明确写出"本文件内 `CameraSample`/`CameraFormat` 为通用结构体，其余类型为 AR0234 专属"，避免歧义（已列入 tasks）。
- **[风险] 外部脚本、CI 配置或其他仓库如果直接引用了旧的 CMake 目标名或类型名，本次改名会造成编译或链接失败** → **缓解**：本仓库当前未发现可见的 CI 配置（README 记录的验证方式均为手动命令），但 tasks 中仍需搜索仓库内是否有脚本（如 `parameters/`、`launch/`、`Dockerfile` 等）引用了这些标识符；风险主要落在仓库外部消费者身上，proposal.md 的 Impact 一节已标注为 **BREAKING** 提醒下游。
- **[风险] 重命名过程中意外改动了行为（例如误删了某个错误处理分支）** → **缓解**：tasks 要求改名前后对 `camera_device.cpp`/`camera_frame_source.cpp` 与新文件做逐行 diff 确认只有标识符变化；且要求本地/CI 可行范围内跑一遍 `ar0234_camera_device_tests`、`ar0234_camera_frame_source_tests`、`camera_timing_tests` 等既有测试，确认全部通过且断言内容未被迫修改（除非断言字符串直接包含旧类型名）。

## Migration Plan

按依赖顺序进行，每一步都可以独立编译验证：

1. 重命名不依赖其他改名产物的叶子文件：`camera_device.{h,cpp}` → `ar0234_camera_device.{h,cpp}`，同步改类型名和 include guard。
2. 重命名依赖步骤 1 的 `camera_frame_source.{h,cpp}` → `ar0234_camera_frame_source.{h,cpp}`，更新其 `#include "camera_device.h"` 为 `#include "ar0234_camera_device.h"`，同步改类型名和 include guard。
3. 更新 `platform_factory.h`/`platform_factory.cpp` 中对 `CameraDependencies`/`Rk3576CameraFrameSource` 的引用和 `#include` 路径。
4. 更新 CMake：`camera_standalone.cmake`/`camera_tests.cmake` 改名并更新内部源文件名、目标名；`CMakeLists.txt`/`backend.cmake` 里的 `include()`/`target_link_libraries()` 同步更新。
5. 重命名测试与验收工具文件（`tests/camera_device_test.cpp` 等 5 个文件），更新其中的类型引用、命名空间、`#include` 路径。
6. 更新 `tests/camera_test_support.h`（文件名不变）内部对已改名类型的引用。
7. 更新 `src/platform/README.md` 中提及旧类型名/文件名的段落。
8. 全仓库搜索确认零残留（源码树范围，不含 `.docker-artifacts/` 和 `openspec/changes/archive/`）。
9. 构建并运行受影响的测试目标，确认全部通过。

**回滚策略**：本次变更是纯重命名且不改变行为，如需回滚，直接 `git revert` 对应提交即可；不涉及数据迁移、模型文件或运行时状态，无需额外的回滚步骤。
