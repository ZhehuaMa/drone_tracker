#ifndef RK3576_AR0234_CAMERA_DRIVER_SUPPORT_H
#define RK3576_AR0234_CAMERA_DRIVER_SUPPORT_H

#include <filesystem>
#include "../platform_factory.h"

namespace rk3576::ar0234_camera_driver {
/** @brief 独立验收参数；frames/seconds 任一达到即结束当前轮次。 */
struct Options {
    FrameSourceConfig config{};
    std::filesystem::path output{};
    std::uint64_t frames{};
    int seconds{};
    int rounds{1};
    int instances{1};
    int slow_ms{};
    int snapshots{1};
    bool compare{};
    bool finite_commands{};
    std::uint64_t commands{};
    int tail_ms{500};
};
Options parse(int argc, char **argv);
/** @brief 通过公共工厂采集并保存报告；依赖注入仅供无设备测试，不提供模拟设备 CLI。 */
void run(const Options &options, Ar0234CameraDependencies injected = {});
} // namespace rk3576::ar0234_camera_driver
#endif // RK3576_AR0234_CAMERA_DRIVER_SUPPORT_H
