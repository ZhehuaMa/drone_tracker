#include "platform/jetson/tracker_assembly.h"

#include "model/jetson/jetson_storage.h"
#include "model/jetson/tensor_rt_model_executor.h"
#include "preprocess/jetson/jetson_yolo_preprocessor.h"
#include "preprocess/jetson/jetson_nanotrack_preprocessor.h"

namespace platform_jetson {
PlatformResult<TrackerComponents> createProductionComponents(
    const ModelPaths &paths, std::function<std::unique_ptr<FrameCompatibility>()> compatibility) {
    return assembleTracker(paths, {model::loadTensorRtModel, model::adaptJetsonInput,
        [] { return std::make_unique<JetsonYoloPreprocessor>(); },
        [] { return std::make_unique<JetsonNanoTrackPreprocessor>(); }, std::move(compatibility)});
}
} // namespace platform_jetson
