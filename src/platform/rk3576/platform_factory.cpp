#include "platform_factory.h"

#include "host_frame.h"
#include "video_frame_source.h"

namespace rk3576 {
namespace {
class Builder final : public FrameBuilder {
public:
    PlatformResult<FrameRef> copyFrom(const cv::Mat &image, std::uint64_t timestamp_us) override {
        try {
            return {copyHostFrame(image, timestamp_us)};
        } catch (const std::exception &error) {
            return PlatformResult<FrameRef>::failure({"rk3576", "FrameBuilder", {}, error.what()});
        }
    }
};
class Factory final : public PlatformFactory {
public:
    explicit Factory(AssemblyDependencies dependencies) : dependencies_{std::move(dependencies)} {}
    PlatformResult<std::unique_ptr<FrameSource>> createFrameSource(const FrameSourceConfig &config) override {
        if (config.type == FrameSourceType::Video) {
            try {
                return {std::make_unique<Rk3576VideoFrameSource>(config)};
            } catch (const std::exception &error) {
                return PlatformResult<std::unique_ptr<FrameSource>>::failure(
                    {"rk3576", "video", config.path, error.what()});
            }
        }
        return PlatformResult<std::unique_ptr<FrameSource>>::failure(
            {"rk3576", "camera", config.path, "Camera frame source unsupported"});
    }
    PlatformResult<std::unique_ptr<FrameBuilder>> createFrameBuilder() override {
        return {std::make_unique<Builder>()};
    }
    PlatformResult<TrackerComponents> createTrackerComponents(const ModelPaths &paths) override {
        return assembleTracker(paths, dependencies_);
    }

private:
    AssemblyDependencies dependencies_;
};
} // namespace
std::unique_ptr<PlatformFactory> createFactory(AssemblyDependencies dependencies) {
    return std::make_unique<Factory>(std::move(dependencies));
}
} // namespace rk3576
