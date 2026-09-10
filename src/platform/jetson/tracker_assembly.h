#ifndef PLATFORM_JETSON_TRACKER_ASSEMBLY_H
#define PLATFORM_JETSON_TRACKER_ASSEMBLY_H

#include <functional>
#include "platform/platform_factory.h"
#include "hybrid_tracker/model/jetson/jetson_model_backend.h"

namespace platform_jetson {
/** @brief 生产装配的可替换资源创建能力；回调仅在创建期间使用，不被组件借用。 */
struct AssemblyDependencies {
    std::function<model::LoadResult(const std::string &)> load{};
    model::InputAdapter adapt{};
    std::function<std::unique_ptr<YoloPreprocessor>()> yolo_preprocessor{};
    std::function<std::unique_ptr<NanoTrackPreprocessor>()> nano_preprocessor{};
    std::function<std::unique_ptr<FrameCompatibility>()> compatibility{};
};
/** @brief 顺序加载并验证三个模型；任何普通失败均不返回部分组件，也不启动线程。 */
PlatformResult<TrackerComponents> assembleTracker(const ModelPaths &paths, const AssemblyDependencies &dependencies);
/** @brief 接入现有 TensorRT 加载器及 Jetson 预处理器，帧检查由平台提供。 */
PlatformResult<TrackerComponents> createProductionComponents(
    const ModelPaths &paths, std::function<std::unique_ptr<FrameCompatibility>()> compatibility);
} // namespace platform_jetson

#endif // PLATFORM_JETSON_TRACKER_ASSEMBLY_H
