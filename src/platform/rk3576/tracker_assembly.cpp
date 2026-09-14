#include "tracker_assembly.h"

#include <stdexcept>
#include "host_frame.h"
#include "platform/model_paths.h"
#include "yolo_preprocessor.h"

namespace rk3576 {
namespace {
class Compatibility final : public FrameCompatibility {
public:
    PlatformDiagnostic validate(const FrameRef &frame) const override {
        if (!isHostGrayFrame(frame)) {
            return {"rk3576", "FrameCompatibility", {}, "Expected RK3576 host Gray8 storage"};
        }
        return {};
    }
};
} // namespace
std::unique_ptr<FrameCompatibility> createFrameCompatibility() { return std::make_unique<Compatibility>(); }
PlatformResult<TrackerComponents> assembleTracker(const ModelPaths &paths, const AssemblyDependencies &d) {
    using Result = PlatformResult<TrackerComponents>;
    const auto invalid{validateModelPaths(paths, "rk3576")};
    if (!invalid.reason.empty()) {
        return Result::failure(invalid);
    }
    std::string role{"yolo"};
    std::string path{paths.yolo};
    try {
        auto yolo{d.yolo(path)};
        if (!yolo) {
            return Result::failure(yolo.error);
        }
        role = "NanoTrack";
        path = paths.head;
        auto nano{d.nano(paths)};
        if (!nano) {
            return Result::failure(nano.error);
        }
        TrackerComponents components{};
        components.yolo = std::move(yolo.value.backend);
        components.nano = std::move(nano.value.backend);
        components.nano_preprocessor = std::move(nano.value.preprocessor);
        components.yolo_preprocessor = std::make_unique<YoloPreprocessor>();
        components.compatibility = createFrameCompatibility();
        if (!components.complete()) {
            throw std::runtime_error("Incomplete components from loader");
        }
        components.description = "yolo\n" + yolo.value.description + nano.value.description;
        if (d.describe) {
            d.describe(components.description);
        }
        return {std::move(components)};
    } catch (const std::exception &error) {
        return Result::failure({"rk3576", role, path, error.what()});
    }
}
} // namespace rk3576
