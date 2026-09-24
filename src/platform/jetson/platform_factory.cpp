#include "platform/platform_factory.h"

#include <exception>
#include <stdexcept>
#include "platform/jetson/tracker_assembly.h"
#include "frame/vpi_frame.h"
#include "mv_camera/mv_camera.h"
#include "mv_camera/mv_video.h"

namespace {
class JetsonFrameSource final : public FrameSource {
public:
    explicit JetsonFrameSource(const FrameSourceConfig &c) {
        if (c.type == FrameSourceType::Camera) {
            auto camera{std::make_unique<MVCamera>(c.path, c.width, c.height, c.fps, c.trigger_mode,
                                                  c.output_width, c.output_height, c.flip, c.mirror)};
            if (!camera->is_initialized()) {
                throw std::runtime_error("Camera initialization failed");
            }
            camera_ = camera.get();
            source_ = std::move(camera);
        } else {
            source_ = std::make_unique<MVVideo>(c.path, c.use_history_timestamp);
        }
    }
    bool start() override {
        try {
            return source_->run_async();
        } catch (const std::exception &) {
            source_->stop();
            return false;
        }
    }
    bool stop() override { return source_->stop(); }
    bool isRunning() const override { return source_->is_running(); }
    bool getImageSize(int &width, int &height) const override { return source_->get_image_size(width, height); }
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override {
        return source_->getImageQueue().wait_dequeue_timed(frame, timeout);
    }
    bool tryFrame(FrameRef &frame) override { return source_->getImageQueue().try_dequeue(frame); }
    void configureRecording(bool save, const std::string &directory, const std::string &prefix) override {
        if (camera_) { camera_->setup(save, directory, prefix); }
    }
private:
    std::unique_ptr<MVCameraBase> source_{};
    MVCamera *camera_{}; // 借用 source_ 持有的相机；视频输入为空。
};
class JetsonFrameBuilder final : public FrameBuilder {
public:
    PlatformResult<FrameRef> copyFrom(const cv::Mat &image, std::uint64_t timestamp_us) override {
        auto frame{VpiFrame::copyFrom(image, timestamp_us)};
        if (!frame) {
            return PlatformResult<FrameRef>::failure({"jetson", "FrameBuilder", {},
                                                      "VPI copy failed; expected nonempty Gray8 or BGR8 image"});
        }
        return {std::move(frame)};
    }
};
class JetsonFrameCompatibility final : public FrameCompatibility {
public:
    PlatformDiagnostic validate(const FrameRef &frame) const override {
        if (!std::dynamic_pointer_cast<const VpiFrame>(frame)) {
            return {"jetson", "FrameCompatibility", {}, "PlatformTypeMismatch: expected VpiFrame"};
        }
        return {};
    }
};
class JetsonPlatformFactory final : public PlatformFactory {
public:
    PlatformResult<std::unique_ptr<FrameSource>> createFrameSource(const FrameSourceConfig &config) override {
        try {
            if (config.camera_recording.enabled) {
                throw std::invalid_argument("Independent camera recording requires RK3576 SC233HGS");
            }
            auto source{std::make_unique<JetsonFrameSource>(config)};
            int width{}, height{};
            if (!source->getImageSize(width, height) || width <= 0 || height <= 0) {
                return PlatformResult<std::unique_ptr<FrameSource>>::failure(
                    {"jetson", "FrameSource", config.path, "Input dimensions unavailable"});
            }
            return {std::move(source)};
        } catch (const std::exception &error) {
            return PlatformResult<std::unique_ptr<FrameSource>>::failure(
                {"jetson", "FrameSource", config.path, error.what()});
        }
    }
    PlatformResult<std::unique_ptr<FrameBuilder>> createFrameBuilder() override {
        try {
            return {std::make_unique<JetsonFrameBuilder>()};
        } catch (const std::exception &error) {
            return PlatformResult<std::unique_ptr<FrameBuilder>>::failure(
                {"jetson", "FrameBuilder", {}, error.what()});
        }
    }
    PlatformResult<TrackerComponents> createTrackerComponents(const ModelPaths &paths) override {
        return platform_jetson::createProductionComponents(paths,
            [] { return std::make_unique<JetsonFrameCompatibility>(); });
    }
};
} // namespace

std::unique_ptr<PlatformFactory> CreatePlatformFactory() { return std::make_unique<JetsonPlatformFactory>(); }
