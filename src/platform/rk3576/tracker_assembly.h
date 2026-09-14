#ifndef RK3576_TRACKER_ASSEMBLY_H
#define RK3576_TRACKER_ASSEMBLY_H

#include <functional>
#include "platform/platform_factory.h"
#include "rknn_nanotrack_factory.h"

namespace rk3576 {
/** @brief 查询描述与模型同次加载取得，不再次加载模型。 */
struct YoloComponents {
    std::unique_ptr<model::YoloBackend> backend{};
    std::string description{};
};
/** @brief 仅创建期间调用，返回组件不借用这些回调的生命周期。 */
struct AssemblyDependencies {
    std::function<PlatformResult<YoloComponents>(const std::string &)> yolo{};
    std::function<PlatformResult<NanoTrackComponents>(const ModelPaths &)> nano{};
    std::function<void(const std::string &)> describe{};
};
PlatformResult<TrackerComponents> assembleTracker(const ModelPaths &paths, const AssemblyDependencies &dependencies);
std::unique_ptr<FrameCompatibility> createFrameCompatibility();
} // namespace rk3576
#endif // RK3576_TRACKER_ASSEMBLY_H
