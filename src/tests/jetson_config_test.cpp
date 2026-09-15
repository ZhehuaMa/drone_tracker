#include "drone_tracker_node.h"
#include "ros_test_support.h"

#include <iostream>

std::unique_ptr<PlatformFactory> CreatePlatformFactory() { throw std::logic_error("Inject test factory"); }
namespace {
using model_test::require;
class Source final : public FrameSource {
public:
    bool start() override { throw std::logic_error("Configuration test must not start capture"); }
    bool stop() override { return true; }
    bool isRunning() const override { return false; }
    bool getImageSize(int &w, int &h) const override { w = 640; h = 640; return true; }
    bool waitFrame(FrameRef &, std::chrono::microseconds) override { return false; }
    bool tryFrame(FrameRef &) override { return false; }
    void configureRecording(bool, const std::string &, const std::string &) override {}
};
class Factory final : public PlatformFactory {
public:
    explicit Factory(std::shared_ptr<ModelPaths> paths) : paths_{std::move(paths)} {}
    PlatformResult<std::unique_ptr<FrameSource>> createFrameSource(const FrameSourceConfig &) override {
        return {std::make_unique<Source>()};
    }
    PlatformResult<std::unique_ptr<FrameBuilder>> createFrameBuilder() override { return {}; }
    PlatformResult<TrackerComponents> createTrackerComponents(const ModelPaths &paths) override {
        *paths_ = paths;
        return {ros_test::components(std::make_shared<ros_test::Trace>())};
    }
private:
    std::shared_ptr<ModelPaths> paths_;
};
} // namespace
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    try {
        auto paths{std::make_shared<ModelPaths>()};
        {
            DroneTrackerNode node{rclcpp::NodeOptions{}, std::make_unique<Factory>(paths)};
            require(paths->yolo.find("/models/drone_int8.engine") != std::string::npos, "Jetson YOLO default changed");
            require(paths->backbone.find("/models/nanotrackv3_backbone_int8.engine") != std::string::npos,
                "Jetson backbone default changed");
            require(paths->head.find("/models/nanotrack_head_int8.engine") != std::string::npos, "Jetson head default changed");
            require(paths->template_backbone.empty() && paths->search_backbone.empty(), "Jetson split default changed");
        }
        rclcpp::NodeOptions absolute{};
        absolute.parameter_overrides({{"tracker.yolo_engine", "/outside/yolo.engine"}});
        { DroneTrackerNode node{absolute, std::make_unique<Factory>(paths)}; }
        require(paths->yolo == "/outside/yolo.engine", "Jetson absolute path prefixed");
        rclcpp::NodeOptions mixed{};
        mixed.parameter_overrides({{"tracker.nano_template_backbone", "invalid"}});
        bool rejected{false};
        try { DroneTrackerNode node{mixed, std::make_unique<Factory>(paths)}; }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "Jetson split paths accepted");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
}
