#include "drone_tracker_node.h"
#include "ros_test_support.h"
#include "platform/rk3576/video_frame_source.h"
#include "platform/rk3576/host_frame.h"

#include <filesystem>
#include <future>
#include <iostream>
#include <rosbag2_cpp/reader.hpp>

// 测试不链接生产平台，因此没有真实 RKNN 推理或运行期假模型参数。
std::unique_ptr<PlatformFactory> CreatePlatformFactory() { throw std::logic_error("Test must inject factory"); }
struct DroneTrackerNodeTestAccess {
    static bool idle(const DroneTrackerNode &node) { return node.run_state_ == DroneTrackerNode::RunState::Idle; }
    static CameraIntrinsics intrinsics(const DroneTrackerNode &node) { return node.cam_intrinsics_; }
    static cv::Matx33d rotation(DroneTrackerNode &node) {
        std::lock_guard<std::mutex> lock{node.gimbal_mtx_};
        cv::Matx33d result{cv::Matx33d::eye()};
        node.gimbal_history_.latest(result);
        return result;
    }
};
namespace {
using namespace ros_test;
using model_test::require;
using namespace std::chrono_literals;
struct SourceTrace {
    std::string video{};
    FrameSourceConfig config{};
    std::atomic<int> starts{}, stops{}, delivered{};
    std::atomic_bool active{}, eof{}, fail_start{}, fail_input{}, tail_race{}, long_gap{};
    std::atomic<int> limit{10000};
    std::atomic_bool warming_up{};
    std::atomic_bool recording_failed{};
    std::mutex mutex{};
    std::vector<std::uint64_t> timestamps{};
    std::vector<FrameRef> frames{};
};
class Source final : public FrameSource {
public:
    explicit Source(std::shared_ptr<SourceTrace> trace) : trace_{std::move(trace)} {}
    bool start() override {
        ++trace_->starts;
        trace_->delivered = 0;
        trace_->eof = false;
        trace_->active = !trace_->fail_start;
        return trace_->active;
    }
    bool stop() override { ++trace_->stops; trace_->active = false; return true; }
    bool isRunning() const override { return trace_->active; }
    bool getImageSize(int &w, int &h) const override { w = 100; h = 80; return true; }
    FrameSourceStatus status() const override {
        FrameSourceStatus result{trace_->fail_input || trace_->fail_start ? FrameSourceState::Failed
            : trace_->eof ? FrameSourceState::Eof : FrameSourceState::Running, "controlled", "test source"};
        result.recording.enabled = trace_->config.camera_recording.enabled;
        if (result.recording.enabled) {
            result.recording.phase = trace_->recording_failed ? "failed" : "waiting_for_qualified_input";
            if (trace_->recording_failed) { result.recording.reason = "injected recording storage failure"; }
        }
        return result;
    }
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override {
        frame.reset();
        std::this_thread::sleep_for(std::min(timeout, std::chrono::microseconds{10000}));
        if (!trace_->active || trace_->fail_input || trace_->warming_up) { return false; }
        if (trace_->long_gap && trace_->delivered > 0) { return false; }
        if (trace_->delivered >= trace_->limit) { trace_->eof = true; return false; }
        if (trace_->tail_race) { trace_->eof = true; return false; }
        return next(frame);
    }
    bool tryFrame(FrameRef &frame) override {
        frame.reset();
        if (trace_->tail_race.exchange(false)) { return next(frame); }
        return false;
    }
    void configureRecording(bool, const std::string &, const std::string &) override {}
private:
    bool next(FrameRef &frame) {
        const auto timestamp{std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - 20000};
        frame = std::make_shared<TestFrame<1>>(cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, timestamp);
        ++trace_->delivered;
        std::lock_guard<std::mutex> lock{trace_->mutex};
        trace_->timestamps.push_back(timestamp);
        trace_->frames.push_back(frame);
        return true;
    }
    std::shared_ptr<SourceTrace> trace_;
};
class VideoCompatibility final : public FrameCompatibility {
public:
    PlatformDiagnostic validate(const FrameRef &frame) const override {
        return rk3576::isHostGrayFrame(frame) ? PlatformDiagnostic{}
            : PlatformDiagnostic{"test", "frame", {}, "not RK3576 gray frame"};
    }
};
class Factory final : public PlatformFactory {
public:
    Factory(std::shared_ptr<Trace> trace, std::shared_ptr<SourceTrace> source, std::shared_ptr<ModelPaths> paths)
        : trace_{std::move(trace)}, source_{std::move(source)}, paths_{std::move(paths)} {}
    PlatformResult<std::unique_ptr<FrameSource>> createFrameSource(const FrameSourceConfig &config) override {
        require(!config.use_history_timestamp, "historical time leaked");
        source_->config = config;
        if (!source_->video.empty()) {
            auto video_config{config};
            video_config.path = source_->video;
            return {std::make_unique<rk3576::Rk3576VideoFrameSource>(video_config)};
        }
        return {std::make_unique<Source>(source_)};
    }
    PlatformResult<std::unique_ptr<FrameBuilder>> createFrameBuilder() override { return {std::make_unique<Builder>()}; }
    PlatformResult<TrackerComponents> createTrackerComponents(const ModelPaths &paths) override {
        *paths_ = paths;
        ++trace_->assemblies;
        auto value{components(trace_)};
        if (!source_->video.empty()) { value.compatibility = std::make_unique<VideoCompatibility>(); }
        return {std::move(value)};
    }
private:
    std::shared_ptr<Trace> trace_;
    std::shared_ptr<SourceTrace> source_;
    std::shared_ptr<ModelPaths> paths_;
};
rclcpp::NodeOptions options(std::vector<rclcpp::Parameter> extra = {}) {
    std::vector<rclcpp::Parameter> values{
        {"video_source", "test.mov"}, {"tracker.yolo_engine", "/models/yolo.rknn"},
        {"tracker.nano_template_backbone", "template.rknn"}, {"tracker.nano_search_backbone", "search.rknn"},
        {"tracker.nano_head", "head.rknn"}, {"camera.fx", 100.0}, {"camera.fy", 110.0},
        {"camera.cx", 50.0}, {"camera.cy", 40.0}, {"tracker.enable_dcf", false},
        {"tracker.yolo_interval", 1}, {"tracker.lost_max_count", 2}, {"visualized_result", true}};
    for (const auto &parameter : extra) {
        values.erase(std::remove_if(values.begin(), values.end(), [&](const auto &v) {
            return v.get_name() == parameter.get_name();
        }), values.end());
        values.push_back(parameter);
    }
    rclcpp::NodeOptions result{};
    result.parameter_overrides(values);
    return result;
}
struct Harness {
    std::shared_ptr<Trace> trace{std::make_shared<Trace>()};
    std::shared_ptr<SourceTrace> source{std::make_shared<SourceTrace>()};
    std::shared_ptr<ModelPaths> paths{std::make_shared<ModelPaths>()};
    std::shared_ptr<DroneTrackerNode> node{};
    std::shared_ptr<rclcpp::Node> observer{std::make_shared<rclcpp::Node>("observer")};
    rclcpp::executors::SingleThreadedExecutor executor{};
    std::vector<vision_msgs::msg::Detection2D> detections{};
    std::vector<sensor_msgs::msg::Image> images{};
    std::vector<int> states{};
    rclcpp::Subscription<vision_msgs::msg::Detection2D>::SharedPtr detection{};
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image{};
    rclcpp::Subscription<std_msgs::msg::Int8>::SharedPtr state{};
    rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr trigger_pub{};
    Harness(const rclcpp::NodeOptions &config = options(),
            std::function<std::thread(std::function<void()>)> launch = {},
            std::function<void(const char *)> hook = {}, const std::string &video = {},
            std::unique_ptr<osd::DisplayBackend> display = {}) {
        source->video = video;
        node = std::make_shared<DroneTrackerNode>(config, std::make_unique<Factory>(trace, source, paths),
                                                launch, hook, std::move(display));
        detection = observer->create_subscription<vision_msgs::msg::Detection2D>("/fpv/target_bbox", 100,
            [this](vision_msgs::msg::Detection2D::SharedPtr msg) { detections.push_back(*msg); });
        image = observer->create_subscription<sensor_msgs::msg::Image>("/fpv/tracker_image", 100,
            [this](sensor_msgs::msg::Image::SharedPtr msg) { images.push_back(*msg); });
        state = observer->create_subscription<std_msgs::msg::Int8>("/fpv/tracker_state", 100,
            [this](std_msgs::msg::Int8::SharedPtr msg) { states.push_back(msg->data); });
        trigger_pub = observer->create_publisher<std_msgs::msg::Header>("/fpv/tracker_trigger", rclcpp::QoS{1}.best_effort());
        executor.add_node(node);
        executor.add_node(observer);
        pump(150ms);
    }
    ~Harness() { node->stop(); }
    void trigger(const std::string &command) {
        std_msgs::msg::Header message{};
        message.stamp = observer->now();
        message.frame_id = command;
        trigger_pub->publish(message);
        pump(20ms);
    }
    void pump(std::chrono::milliseconds duration) {
        const auto until{std::chrono::steady_clock::now() + duration};
        while (std::chrono::steady_clock::now() < until) {
            executor.spin_some();
            std::this_thread::sleep_for(2ms);
        }
    }
    template<typename Predicate> void until(Predicate predicate, const char *message) {
        const auto deadline{std::chrono::steady_clock::now() + 4s};
        while (!predicate() && std::chrono::steady_clock::now() < deadline) { pump(5ms); }
        require(predicate(), message);
    }
};
struct DisplayTrace {
    std::mutex mutex{};
    std::vector<osd::PresentationState> states{};
    std::atomic<int> initializes{}, shutdowns{};
    std::atomic<bool> fail_init{}, fail_present{};
    std::uint64_t prepared_time{};
};
class TestDisplay final : public osd::DisplayBackend {
public:
    explicit TestDisplay(std::shared_ptr<DisplayTrace> trace) : trace_{std::move(trace)} {}
    void initialize() override {
        ++trace_->initializes;
        if (trace_->fail_init) { throw std::runtime_error("injected OSD init failure"); }
    }
    void prepareVideo(const Frame &frame) override {
        auto view{frame.mapRead()};
        require(view && cv::countNonZero(view->mat() != 42) == 0, "OSD source changed");
        trace_->prepared_time = frame.timestampUs();
    }
    bool present(const osd::PresentationState &state, const std::function<bool()> &authorize) override {
        if (!authorize()) { return false; }
        if (trace_->fail_present) { throw std::runtime_error("injected OSD display failure"); }
        if (state.new_video) { require(state.frame_time_us == trace_->prepared_time, "OSD frame pairing"); }
        std::lock_guard<std::mutex> lock{trace_->mutex};
        trace_->states.push_back(state);
        return true;
    }
    void discardPrepared() noexcept override {}
    void interrupt() noexcept override {}
    void shutdown() noexcept override { ++trace_->shutdowns; }
private:
    std::shared_ptr<DisplayTrace> trace_;
};
void osdCompatibility() {
    for (bool enabled : {false, true}) {
        for (bool images : {false, true}) {
            auto display{std::make_shared<DisplayTrace>()};
            Harness h{options({{"osd.enabled", enabled}, {"osd.timing_enabled", true}, {"visualized_result", images}}), {}, {}, {},
                      std::make_unique<TestDisplay>(display)};
            h.node->run();
            h.until([&] { return h.detections.size() >= 5; }, "OSD blocked detections");
            h.node->stop(); h.pump(120ms);
            require(h.images.empty() == !images, "OSD changed image switch");
            require(h.states.size() == 1 && h.states[0] == 0, "OSD changed state-only publication");
            require(display->initializes == (enabled ? 1 : 0), "disabled OSD opened device");
            if (enabled) {
                std::lock_guard<std::mutex> lock{display->mutex};
                require(!display->states.empty() && display->states.back().status == osd::DisplayStatus::Stopped,
                        "node OFF did not freeze");
                require(display->states.back().boxes.empty(), "node OFF retained boxes");
                for (const auto &state : display->states) {
                    if (!state.new_video) { continue; }
                    require(std::find(h.source->timestamps.begin(), h.source->timestamps.end(), state.frame_time_us)
                        != h.source->timestamps.end(), "OSD replaced capture time with prediction time");
                }
            }
        }
    }
    auto display{std::make_shared<DisplayTrace>()};
    Harness h{options({{"osd.enabled", true}}), {}, {}, {}, std::make_unique<TestDisplay>(display)};
    h.node->run();
    h.until([&] { return h.detections.size() > 3; }, "no detections before OSD failure");
    const auto before{h.detections.size()};
    display->fail_present = true;
    h.until([&] { return display->shutdowns > 0 && h.detections.size() > before + 5; },
            "display failure stopped business");
    h.node->stop();
    auto failed{std::make_shared<DisplayTrace>()}; failed->fail_init = true;
    auto source{std::make_shared<SourceTrace>()};
    bool rejected{false};
    try {
        auto node{std::make_shared<DroneTrackerNode>(options({{"osd.enabled", true}}),
            std::make_unique<Factory>(std::make_shared<Trace>(), source, std::make_shared<ModelPaths>()),
            std::function<std::thread(std::function<void()>)>{}, std::function<void(const char *)>{},
            std::make_unique<TestDisplay>(failed))};
    } catch (const std::exception &) { rejected = true; }
    require(rejected && source->starts == 0 && failed->shutdowns == 1, "OSD init did not roll back");
}
void configuration() {
    for (const auto &parameter : std::vector<rclcpp::Parameter>{
        {"tracker.yolo_engine", ""}, {"tracker.nano_template_backbone", ""},
        {"tracker.nano_search_backbone", ""}, {"tracker.nano_head", ""}, {"tracker.nano_backbone", "mixed"},
        {"tracker.yolo_target_class_id", 1}, {"camera.fx", 0.0}, {"camera.fy", -1.0},
        {"camera.cy", std::numeric_limits<double>::quiet_NaN()}, {"use_sim_time", true},
        {"use_history_timestamp", true}, {"osd.width", 0}, {"osd.frame_stale_ms", 0},
        {"osd.battery_cells", -1}, {"osd.refresh_hz", std::numeric_limits<double>::infinity()},
        {"recording.enabled", true}}) {
        auto source{std::make_shared<SourceTrace>()};
        bool rejected{false};
        try {
            auto node{std::make_shared<DroneTrackerNode>(options({parameter}),
                std::make_unique<Factory>(std::make_shared<Trace>(), source, std::make_shared<ModelPaths>()))};
        } catch (const std::exception &) { rejected = true; }
        require(rejected && source->starts == 0 && !source->active, "invalid configuration started source");
    }
    Harness h{};
    const auto intrinsics{DroneTrackerNodeTestAccess::intrinsics(*h.node)};
    require(intrinsics.fx == 100.0 && intrinsics.fy == 110.0 && intrinsics.px == 50.0
        && intrinsics.py == 40.0, "video intrinsics scaled");
    require(h.paths->yolo == "/models/yolo.rknn", "absolute path prefixed");
    require(h.paths->head.find("/models/head.rknn") != std::string::npos, "relative path not resolved");
    for (const auto &parameter : std::vector<rclcpp::Parameter>{{"use_sim_time", true}, {"camera.fx", 200.0},
        {"tracker.enable_dcf", true}, {"video_source", "other.mov"}, {"need_save", true},
        {"osd.enabled", true}, {"osd.timing_enabled", true}, {"osd.width", 1280}, {"recording.enabled", true},
        {"recording.directory", "/var/lib/drone_tracker/other"}, {"recording.bitrate_bps", 8000000},
        {"recording.segment_seconds", 30}, {"recording.sync_interval_ms", 500},
        {"recording.max_total_bytes", 1000000}, {"recording.min_free_bytes", 1000000}}) {
        require(!h.node->set_parameter(parameter).successful, "startup parameter changed");
    }
}
void messagesAndRestart() {
    Harness h{};
    h.trigger("ON"); h.trigger("ON");
    h.until([&] { return h.detections.size() > 3; }, "no empty detections");
    require(h.source->starts == 1 && h.states.front() == 0, "duplicate ON or initial state");
    require(h.detections.front().header.frame_id.empty(), "empty detection valid");
    h.trace->target = true;
    h.until([&] { return std::find(h.states.begin(), h.states.end(), 2) != h.states.end(); }, "did not track");
    require(std::find(h.states.begin(), h.states.end(), 1) != h.states.end(), "missing TargetFound");
    require(std::any_of(h.detections.begin(), h.detections.end(), [](const auto &d) {
        return d.header.frame_id == "camera_frame";
    }), "no valid detection");
    h.trace->target = false; h.trace->nano_ok = false;
    h.until([&] { return std::find(h.states.begin(), h.states.end(), 3) != h.states.end(); }, "did not lose target");
    h.trace->target = true; h.trace->nano_ok = true;
    const auto state_count{h.states.size()};
    h.until([&] { return h.states.size() > state_count && h.states.back() == 2; }, "did not recover");
    h.node->stop(); h.pump(50ms);
    require(!h.images.empty(), "no images");
    {
        std::lock_guard<std::mutex> lock{h.source->mutex};
        for (const auto &img : h.images) {
            const auto ts{rclcpp::Time(img.header.stamp).nanoseconds() / 1000};
            require(img.encoding == "bgr8" && img.width == 100 && img.height == 80, "image contract");
            require(std::find(h.source->timestamps.begin(), h.source->timestamps.end(), ts)
                != h.source->timestamps.end(), "image timestamp changed");
        }
        for (const auto &frame : h.source->frames) {
            const auto view{frame->mapRead()};
            require(cv::countNonZero(view->mat() != 42) == 0, "drawing modified shared frame");
        }
        require(rclcpp::Time(h.detections.front().header.stamp).nanoseconds() / 1000
            > static_cast<std::int64_t>(h.source->timestamps.front()), "prediction used frame time");
    }
    h.trace->target = false;
    h.source->limit = 1;
    h.node->run();
    h.until([&] { return h.source->stops >= 2; }, "EOF did not clean up");
    h.pump(30ms);
    const auto previous{h.states.size()};
    h.node->run();
    h.until([&] { return h.source->stops >= 3; }, "EOF direct ON failed");
    h.pump(30ms);
    require(h.states.size() == previous + 1 && h.states.back() == 0, "restart suppressed Detecting");
    require(h.trace->assemblies == 1, "models reloaded");
    h.trigger("OFF"); h.trigger("OFF");
}
void cleanupAndFailures() {
    Harness h{options({{"visualized_result", false}})};
    h.trace->target = true;
    h.trace->block = true;
    h.node->run();
    {
        std::unique_lock<std::mutex> lock{h.trace->mutex};
        require(h.trace->cv.wait_for(lock, 2s, [&] { return h.trace->entered; }), "inference not entered");
    }
    auto stopped{std::async(std::launch::async, [&] { h.node->stop(); })};
    require(stopped.wait_for(40ms) == std::future_status::timeout, "OFF returned before inference");
    {
        std::lock_guard<std::mutex> lock{h.trace->mutex};
        h.trace->release = true;
        h.trace->cv.notify_all();
    }
    require(stopped.wait_for(2s) == std::future_status::ready, "OFF deadlocked");
    stopped.get();
    h.trace->target = false;
    h.pump(20ms);
    require(h.images.empty(), "disabled images published");
    h.source->tail_race = true; h.source->limit = 1;
    const auto before{h.detections.size()};
    h.node->run();
    h.until([&] { return h.source->stops >= 2; }, "tail EOF failed");
    h.pump(30ms);
    require(h.detections.size() == before + 1, "tail lost in EOF race");
    require(h.detections.back().header.frame_id.empty(), "old target leaked into new round");
    h.source->fail_start = true;
    h.node->run(); h.until([&] { return h.source->stops >= 3; }, "start failure cleanup");
    h.source->fail_start = false; h.source->fail_input = true;
    h.node->run(); h.until([&] { return h.source->stops >= 4; }, "input failure cleanup");
    h.source->fail_input = false;
    h.node->run(); h.until([&] { return h.source->stops >= 5; }, "retry failed");
    h.source->limit = 10000;
    h.source->long_gap = true;
    h.node->run(); h.pump(100ms);
    require(h.source->active && h.source->delivered == 1, "Running timeout ended round");
    const auto stopping{std::chrono::steady_clock::now()};
    h.node->stop();
    require(std::chrono::steady_clock::now() - stopping < 1s, "OFF waited for next frame");
    int launches{};
    Harness thread_failure{options(), [&launches](std::function<void()> work) {
        if (++launches == 1) { throw std::runtime_error("injected worker creation failure"); }
        return std::thread{std::move(work)};
    }};
    thread_failure.node->run();
    require(thread_failure.source->starts == 0, "failed thread started source");
    thread_failure.node->run();
    thread_failure.until([&] { return thread_failure.source->starts == 1; }, "thread failure retry");
    thread_failure.node->stop();
}
void cameraContracts() {
    for (const auto *mode : {"continuous", "software_trigger"}) {
        const bool software{std::string{mode} == "software_trigger"};
        auto params{options({{"video_source", software ? "video10" : "/dev/video10"},
            {"mv_camera.capture_mode", mode}, {"mv_camera.trigger_rate_hz", software ? 20.0 : 0.0},
            {"mv_camera.media_device", "/dev/media0"}, {"mv_camera.sensor_entity", "test sensor"},
            {"mv_camera.buffer_count", 6}, {"mv_camera.fps", 60}, {"mv_camera.width_px", 100},
            {"mv_camera.height_px", 80}, {"mv_camera.output_width_px", 0}, {"mv_camera.output_height_px", 0}})};
        Harness h{params};
        require(h.source->config.type == FrameSourceType::Camera && h.source->config.path == "/dev/video10"
            && h.source->config.trigger_mode == software && h.source->config.camera_trigger_rate_hz == (software ? 20 : 0)
            && h.source->config.camera_buffer_count == 6 && h.source->config.camera_sensor_entity == "test sensor",
            "Camera parameter mapping failed");
        require(DroneTrackerNodeTestAccess::intrinsics(*h.node).fx == 100, "Camera intrinsics rescaled");
        require(!h.node->set_parameter({"mv_camera.trigger_rate_hz", 15.0}).successful
            && !h.node->set_parameter({"mv_camera.capture_mode", "continuous"}).successful,
            "Runtime camera parameter changed");
        require(h.source->starts == 0, "Camera initialization started acquisition");
        h.source->fail_start = true;
        h.trigger("ON");
        h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "Failed camera start not idle");
        h.source->fail_start = false;
        h.trigger("ON"); h.trigger("ON");
        h.until([&] { return h.detections.size() >= 3; }, "Camera no-target messages missing");
        require(h.source->starts == 2 && h.states.front() == 0 && h.detections.front().header.frame_id.empty(),
                "Camera duplicate ON or Detecting contract");
        h.trace->target = true;
        h.until([&] { return std::find(h.states.begin(), h.states.end(), 2) != h.states.end(); }, "Camera did not track");
        require(std::find(h.states.begin(), h.states.end(), 1) != h.states.end(), "Camera TargetFound missing");
        h.trace->target = false; h.trace->nano_ok = false;
        h.until([&] { return std::find(h.states.begin(), h.states.end(), 3) != h.states.end(); }, "Camera lost state missing");
        h.node->stop(); h.pump(30ms);
        const auto before{h.states.size()};
        h.trace->target = false; h.trace->nano_ok = true;
        h.trigger("ON");
        h.until([&] { return h.states.size() > before; }, "Camera new round state missing");
        require(h.states.back() == 0 && h.trace->assemblies == 1, "Camera stale round/model reload");
        h.source->fail_input = true;
        h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "Camera input failure not cleaned");
        h.pump(20ms);
        require(h.states.back() == 0, "Camera failure fabricated TargetLost");
        h.trigger("OFF"); h.trigger("OFF");
        for (const auto &img : h.images) {
            const auto ts{rclcpp::Time(img.header.stamp).nanoseconds() / 1000};
            std::lock_guard<std::mutex> lock{h.source->mutex};
            require(img.encoding == "bgr8" && img.width == 100 && img.height == 80
                && std::find(h.source->timestamps.begin(), h.source->timestamps.end(), ts) != h.source->timestamps.end(),
                "Camera image time/format mismatch");
        }
        require(!h.images.empty(), "Camera images missing");
        params.append_parameter_override("visualized_result", false);
        {
            Harness off{params};
            require(DroneTrackerNodeTestAccess::rotation(*off.node) == cv::Matx33d::eye(), "Missing pose not identity");
            auto imu{off.observer->create_publisher<sensor_msgs::msg::Imu>("/fpv/gimbal", rclcpp::QoS{1}.best_effort())};
            off.pump(150ms);
            sensor_msgs::msg::Imu pose{};
            pose.header.stamp = off.observer->now(); pose.orientation.w = std::sqrt(0.5); pose.orientation.z = std::sqrt(0.5);
            imu->publish(pose); off.pump(30ms);
            require(std::abs(DroneTrackerNodeTestAccess::rotation(*off.node)(0, 1) + 1.0) < 1e-9,
                    "Camera pose lookup changed");
            off.trace->target = true; off.trace->block = true;
            off.node->run();
            {
                std::unique_lock<std::mutex> lock{off.trace->mutex};
                require(off.trace->cv.wait_for(lock, 2s, [&] { return off.trace->entered; }), "Camera inference not entered");
            }
            auto stopped{std::async(std::launch::async, [&] { off.node->stop(); })};
            require(stopped.wait_for(30ms) == std::future_status::timeout, "Camera OFF did not wait for model");
            {
                std::lock_guard<std::mutex> lock{off.trace->mutex};
                off.trace->release = true; off.trace->cv.notify_all();
            }
            require(stopped.wait_for(2s) == std::future_status::ready, "Camera OFF deadlocked");
            stopped.get(); off.pump(30ms);
            require(off.images.empty(), "Disabled camera image published");
            off.trace->target = false;
            const auto previous{off.detections.size()};
            off.node->run();
            off.until([&] { return off.detections.size() > previous; }, "Camera retry after model wait failed");
            off.node->stop(); off.pump(20ms);
            require(off.detections.back().header.frame_id.empty() && off.trace->assemblies == 1,
                    "In-flight old camera result leaked");
        }
        params.append_parameter_override("need_save", true);
        bool rejected{false};
        try { Harness invalid{params}; } catch (const std::exception &error) {
            rejected = std::string{error.what()}.find("need_save=false") != std::string::npos;
        }
        require(rejected, "Camera recording was not rejected at initialization");
    }
    {
        auto params{options({{"video_source", "/dev/video10"},
            {"mv_camera.sensor_model", "sc233hgs"}, {"mv_camera.capture_mode", "continuous"},
            {"mv_camera.trigger_rate_hz", 0.0}, {"mv_camera.media_device", "/dev/media0"},
            {"mv_camera.sensor_entity", "sc233hgs 4-0030"}, {"mv_camera.buffer_count", 4},
            {"mv_camera.fps", 60}, {"mv_camera.width_px", 100}, {"mv_camera.height_px", 80},
            {"mv_camera.output_width_px", 0}, {"mv_camera.output_height_px", 0}})};
        Harness h{params};
        require(h.source->config.camera_sensor_model == CameraSensorModel::Sc233hgs
            && !h.source->config.trigger_mode && h.source->config.camera_trigger_rate_hz == 0.0,
            "SC233HGS continuous parameter mapping failed");
        require(!h.node->set_parameter({"mv_camera.sensor_model", "ar0234"}).successful
            && h.node->set_parameter({"mv_camera.sensor_model", "sc233hgs"}).successful == false,
            "Sensor model runtime change was accepted");
        h.source->warming_up = true;
        h.node->run();
        h.pump(80ms);
        require(h.detections.empty() && h.images.empty() && h.source->delivered == 0,
                "Warmup published observations");
        h.node->stop();
        h.pump(30ms);
        require(h.detections.empty() && h.images.empty(), "Warmup OFF published frames");
        h.node->run();
        h.source->fail_input = true;
        h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "Warmup failure did not clean up");
        require(std::find(h.states.begin(), h.states.end(), 3) == h.states.end(), "Warmup fabricated TargetLost");
        h.source->fail_input = false;
        h.source->warming_up = false;
        h.node->run();
        h.until([&] { return !h.detections.empty(); }, "Warmup failure recovery produced no frames");
        h.source->fail_input = true;
        h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "Streaming failure did not clean up");
        require(h.trace->assemblies == 1, "Warmup recovery reloaded models");
    }
    {
        auto params{options({{"video_source", "/dev/video10"},
            {"mv_camera.sensor_model", "sc233hgs"}, {"mv_camera.capture_mode", "software_trigger"},
            {"mv_camera.trigger_rate_hz", 20.0}, {"mv_camera.media_device", "/dev/media0"},
            {"mv_camera.sensor_entity", "sc233hgs 4-0030"}})};
        bool rejected{};
        try { Harness invalid{params}; } catch (const std::invalid_argument &error) {
            rejected = std::string{error.what()}.find("does not support software trigger") != std::string::npos;
        }
        require(rejected, "SC233HGS software trigger was accepted by node");
    }
    {
        auto params{options({{"video_source", "/dev/video10"},
            {"mv_camera.sensor_model", "unknown"}, {"mv_camera.capture_mode", "continuous"},
            {"mv_camera.trigger_rate_hz", 0.0}, {"mv_camera.media_device", "/dev/media0"},
            {"mv_camera.sensor_entity", "test sensor"}})};
        bool rejected{};
        try { Harness invalid{params}; } catch (const std::invalid_argument &error) {
            rejected = std::string{error.what()}.find("ar0234 or sc233hgs") != std::string::npos;
        }
        require(rejected, "Unknown sensor model was accepted by node");
    }
}
void realVideo() {
    Harness h{options(), {}, {}, std::string{ROS_VIDEO_FIXTURES} + "/long.mov"};
    h.node->run();
    h.until([&] { return !h.detections.empty(); }, "real video first frame");
    h.pump(100ms);
    require(!DroneTrackerNodeTestAccess::idle(*h.node), "ten-second PTS gap ended round");
    const auto before{std::chrono::steady_clock::now()};
    h.node->stop();
    require(std::chrono::steady_clock::now() - before < 1s, "real PTS stop blocked");
    const auto count{h.detections.size()};
    h.node->run();
    h.until([&] { return h.detections.size() > count; }, "real video did not restart");
    h.node->stop();
    Harness eof{options(), {}, {}, std::string{ROS_VIDEO_FIXTURES} + "/420.mov"};
    eof.node->run();
    eof.until([&] { return DroneTrackerNodeTestAccess::idle(*eof.node); }, "real EOF cleanup");
    require(!eof.images.empty(), "real gray frames not published");
    const auto previous{eof.detections.size()};
    eof.node->run();
    eof.until([&] { return DroneTrackerNodeTestAccess::idle(*eof.node); }, "real EOF direct restart");
    require(eof.detections.size() > previous && eof.trace->assemblies == 1, "real EOF models not reused");
}
void independentCameraRecording() {
    for (const bool display_enabled : {false, true}) {
        for (const bool images : {false, true}) {
            std::atomic<int> bag_operations{};
            auto display{std::make_shared<DisplayTrace>()};
            Harness h{options({{"video_source", "/dev/video10"}, {"mv_camera.sensor_model", "sc233hgs"},
                {"mv_camera.capture_mode", "continuous"}, {"mv_camera.trigger_rate_hz", 0.0},
                {"mv_camera.media_device", "/dev/media0"}, {"mv_camera.sensor_entity", "sc233hgs 4-0030"},
                {"mv_camera.width_px", 100}, {"mv_camera.height_px", 80}, {"mv_camera.fps", 60},
                {"mv_camera.output_width_px", 0}, {"mv_camera.output_height_px", 0},
                {"recording.enabled", true}, {"osd.enabled", display_enabled}, {"visualized_result", images}}),
                {}, [&](const char *) { ++bag_operations; }, {}, std::make_unique<TestDisplay>(display)};
            require(h.source->config.camera_recording.enabled
                && h.source->config.camera_recording.bitrate_bps == 12000000
                && h.source->config.camera_recording.sync_interval_ms == 1000,
                "Recording startup configuration not forwarded");
            // 缺少测试模型文件时，身份读取错误属于录像准备故障，不能阻止可控跟踪器初始化。
            require(!h.source->config.camera_recording.preparation_error.empty(),
                "Recording identity failure was not isolated");
            h.source->recording_failed = true;
            h.source->warming_up = true;
            h.node->run(); h.node->run(); h.pump(80ms);
            require(h.source->starts == 1 && h.detections.empty() && h.images.empty(),
                "Recording failure changed duplicate ON or warmup behavior");
            h.source->warming_up = false;
            h.until([&] { return h.detections.size() >= 5; }, "Recording failure stopped healthy tracking");
            require(h.source->active && h.states.size() == 1 && h.states.front() == 0,
                "Recording failure changed tracking state");
            h.node->stop(); h.pump(80ms);
            require(h.images.empty() == !images && display->initializes == (display_enabled ? 1 : 0),
                "Recording changed image or OSD switches");
            const auto count{h.detections.size()};
            h.node->run();
            h.until([&] { return h.detections.size() > count; }, "Recording failure blocked next ON");
            h.node->stop();
            require(h.trace->assemblies == 1 && bag_operations == 0,
                "Independent recording reloaded models or enabled ROS bag");
        }
    }
}
void recording() {
    const auto root{std::filesystem::temp_directory_path()
        / ("ros-video-bags-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(root);
    Harness h{options({{"need_save", true}, {"save_path", root.string()}})};
    auto imu{h.observer->create_publisher<sensor_msgs::msg::Imu>("/fpv/gimbal", rclcpp::QoS{1}.best_effort())};
    auto odom{h.observer->create_publisher<nav_msgs::msg::Odometry>("/fpv/odom", rclcpp::QoS{10}.best_effort())};
    h.pump(200ms);
    for (int round{0}; round < 2; ++round) {
        h.source->limit = round == 0 ? 10000 : 15;
        h.node->run();
        std::atomic_bool sending{true};
        std::thread publisher{[&] {
            while (sending) {
                sensor_msgs::msg::Imu message{};
                message.header.stamp = h.observer->now();
                message.orientation.w = std::sqrt(0.5);
                message.orientation.z = std::sqrt(0.5);
                imu->publish(message);
                nav_msgs::msg::Odometry position{};
                position.header.stamp = message.header.stamp;
                odom->publish(position);
                std::this_thread::sleep_for(2ms);
            }
        }};
        h.pump(300ms);
        // 发布线程在 OFF/EOF 和 bag 关闭期间持续发送。
        h.node->stop(); h.pump(30ms);
        sending = false;
        publisher.join();
        const auto rotation{DroneTrackerNodeTestAccess::rotation(*h.node)};
        require(std::abs(rotation(0, 1) + 1.0) < 1e-9 && std::abs(rotation(1, 0) - 1.0) < 1e-9,
            "gimbal orientation mapping changed");
    }
    int bags{};
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        rosbag2_cpp::Reader reader{};
        reader.open(entry.path().string());
        std::vector<std::string> topics{};
        while (reader.has_next()) { topics.push_back(reader.read_next()->topic_name); }
        for (const auto *topic : {"/fpv/gimbal", "/fpv/odom", "/fpv/target_bbox", "/fpv/tracker_image"}) {
            require(std::find(topics.begin(), topics.end(), topic) != topics.end(), "closed bag missing topic");
        }
        ++bags;
    }
    require(bags == 2, "incorrect recording rounds");
    builtin_interfaces::msg::Time fixed{};
    fixed.sec = 123;
    h.node->run(fixed); h.pump(50ms); h.node->stop();
    const int before{h.source->starts};
    h.node->run(fixed);
    require(h.source->starts == before, "existing recording overwritten");
    for (const auto *failure : {"open", "write"}) {
        std::atomic_bool inject{true};
        Harness broken{options({{"need_save", true}, {"save_path", root.string()}}), {},
            [failure, &inject](const char *operation) {
                if (inject && std::string{operation} == failure) { throw std::runtime_error("injected bag failure"); }
            }};
        broken.node->run(); broken.pump(100ms); broken.node->stop();
        require(!broken.source->active, "recording failure left source running");
        if (std::string{failure} == "open") { require(broken.source->starts == 0, "open failure started source"); }
        inject = false;
        broken.node->run(); broken.pump(100ms); broken.node->stop();
        require(broken.source->delivered > 0, "recording failure prevented retry");
    }
    std::filesystem::remove_all(root);
}
} // namespace
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    try {
        configuration();
        osdCompatibility();
        messagesAndRestart();
        cleanupAndFailures();
        recording();
        realVideo();
        cameraContracts();
        independentCameraRecording();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
