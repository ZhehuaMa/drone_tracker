#include "drone_tracker_node.h"
#include "ros_test_support.h"
#include "platform/rk3576/video_frame_source.h"
#include "platform/rk3576/host_frame.h"

#include <filesystem>
#include <future>
#include <iostream>
#include <rosbag2_cpp/reader.hpp>
#include <rclcpp/serialization.hpp>

// 测试不链接生产平台，因此没有真实 RKNN 推理或运行期假模型参数。
std::unique_ptr<PlatformFactory> CreatePlatformFactory() { throw std::logic_error("Test must inject factory"); }
struct DroneTrackerNodeTestAccess {
    static const diagnostics::SharedRegion *diagnostics(const DroneTrackerNode &node) { return node.diagnostic_client_ ? node.diagnostic_client_->region() : nullptr; }
    static bool idle(const DroneTrackerNode &node) { return node.run_state_ == DroneTrackerNode::RunState::Idle; }
    static std::uint64_t round(const DroneTrackerNode &node) { return node.round_number_.load(); }
    static std::uint64_t replacements(DroneTrackerNode &node) {
        std::lock_guard<std::mutex> lock{node.control_mutex_}; return node.tracking_replaced_;
    }
    static rclcpp::QoS endpointQos(const DroneTrackerNode &node, const std::string &topic) {
        if (topic == "/fpv/target_bbox") { return node.detection_pub_->get_actual_qos(); }
        if (topic == "/fpv/target_los") { return node.los_pub_->get_actual_qos(); }
        if (topic == "/fpv/tracker_image") { return node.image_pub_->get_actual_qos(); }
        if (topic == "/fpv/gimbal") { return node.gimbal_sub_->get_actual_qos(); }
        return node.mavros_imu_sub_->get_actual_qos();
    }
    static void cameraPose(DroneTrackerNode &node, const sensor_msgs::msg::Imu::SharedPtr &message) {
        node.imu_callback(message);
    }
    static CameraIntrinsics intrinsics(const DroneTrackerNode &node) { return node.cam_intrinsics_; }
    static BagSession::Status bagStatus(DroneTrackerNode &node) {
        std::lock_guard<std::mutex> lock{node.bag_status_mutex_};
        return node.last_bag_status_;
    }
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
template<typename Message>
Message decodeBag(const rosbag2_storage::SerializedBagMessage &item) {
    rclcpp::SerializedMessage serialized{*item.serialized_data};
    rclcpp::Serialization<Message> serialization{};
    Message result{};
    serialization.deserialize_message(&serialized, &result);
    return result;
}
struct SourceTrace {
    std::string video{};
    FrameSourceConfig config{};
    std::atomic<int> starts{}, stops{}, delivered{};
    std::atomic_bool active{}, eof{}, fail_start{}, fail_input{}, tail_race{}, long_gap{};
    std::atomic<int> limit{10000};
    std::atomic<std::uint64_t> fixed_timestamp{}, fixed_timestamp_step{};
    std::atomic_bool warming_up{};
    std::atomic_bool recording_failed{};
    std::atomic<int> legacy_recording_requests{}, session_starts{}, session_stops{};
    std::atomic_bool session_control{}, session_active{};
    std::mutex mutex{};
    std::vector<std::uint64_t> timestamps{};
    std::vector<FrameRef> frames{};
};
class Source final : public FrameSource, public CameraRecordingControl {
public:
    explicit Source(std::shared_ptr<SourceTrace> trace) : trace_{std::move(trace)} {}
    CameraRecordingControl *recordingControl() noexcept override { return this; }
    void useSessionControl(std::uint64_t) override { trace_->session_control = true; }
    void requestRecording(std::uint64_t, bool enabled, RecordingEndReason) override {
        if (!trace_->config.camera_recording.enabled) { return; }
        if (trace_->session_active.exchange(enabled) == enabled) { return; }
        if (enabled) { ++trace_->session_starts; } else { ++trace_->session_stops; }
    }
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
        if (trace_->tail_race) {
            if (trace_->delivered < trace_->limit) { return next(frame); }
            trace_->tail_race = false;
        }
        return false;
    }
    void configureRecording(bool enabled, const std::string &, const std::string &) override {
        if (enabled) { ++trace_->legacy_recording_requests; }
    }
private:
    bool next(FrameRef &frame) {
        const auto generated_timestamp{std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - 20000};
        const auto timestamp{trace_->fixed_timestamp.load() ? trace_->fixed_timestamp.load()
            + trace_->fixed_timestamp_step.load() * trace_->delivered.load() : static_cast<std::uint64_t>(generated_timestamp)};
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
    std::vector<geometry_msgs::msg::TwistStamped> observations{};
    rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr los{};
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
        node->startService();
        detection = observer->create_subscription<vision_msgs::msg::Detection2D>("/fpv/target_bbox", 100,
            [this](vision_msgs::msg::Detection2D::SharedPtr msg) { detections.push_back(*msg); });
        los = observer->create_subscription<geometry_msgs::msg::TwistStamped>("/fpv/target_los", 100,
            [this](geometry_msgs::msg::TwistStamped::SharedPtr msg) { observations.push_back(*msg); });
        image = observer->create_subscription<sensor_msgs::msg::Image>("/fpv/tracker_image", rclcpp::QoS{100}.best_effort(),
            [this](sensor_msgs::msg::Image::SharedPtr msg) { images.push_back(*msg); });
        state = observer->create_subscription<std_msgs::msg::Int8>("/fpv/tracker_state", 100,
            [this](std_msgs::msg::Int8::SharedPtr msg) { states.push_back(msg->data); });
        trigger_pub = observer->create_publisher<std_msgs::msg::Header>("/fpv/tracker_trigger", rclcpp::QoS{1}.best_effort());
        executor.add_node(node);
        executor.add_node(observer);
        pump(150ms);
    }
    ~Harness() { node->shutdown(); }
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
    for (const std::string path : {"legacy", "cached-overlay"}) {
        auto display{std::make_shared<DisplayTrace>()};
        Harness h{options({{"osd.enabled", true}, {"osd.render_path", path}}), {}, {}, {},
                  std::make_unique<TestDisplay>(display)};
        require(h.node->get_parameter("osd.render_path").as_string() == path, "render path override lost");
        h.node->run();
        h.until([&] { return h.detections.size() >= 5; }, "render path blocked shared consumer");
        h.node->stop();
        require(h.source->starts == 1 && display->initializes == 1, "render path created extra consumer");
    }
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
        {"osd.render_path", "invalid"}, {"osd.battery_cells", -1}, {"osd.refresh_hz", std::numeric_limits<double>::infinity()},
        {"osd.connector", ""}, {"los.rate_filter_tau", -1.0},
        {"los.rate_filter_tau", std::numeric_limits<double>::infinity()}}) {
        auto source{std::make_shared<SourceTrace>()};
        bool rejected{false};
        try {
            auto node{std::make_shared<DroneTrackerNode>(options({parameter}),
                std::make_unique<Factory>(std::make_shared<Trace>(), source, std::make_shared<ModelPaths>()))};
        } catch (const std::exception &) { rejected = true; }
        require(rejected && source->starts == 0 && !source->active, "invalid configuration started source");
    }
    Harness h{};
    for (const auto &parameter : std::vector<rclcpp::Parameter>{{"publish_prediction", true}, {"los.rate_filter_tau", 0.0}}) {
        const auto result{h.node->set_parameter(parameter)};
        require(!result.successful, "observation startup parameter changed at runtime");
        require(result.reason.find("restart") != std::string::npos,
            "observation startup parameter rejection did not explain restart");
    }
    const auto intrinsics{DroneTrackerNodeTestAccess::intrinsics(*h.node)};
    require(intrinsics.fx == 100.0 && intrinsics.fy == 110.0 && intrinsics.px == 50.0
        && intrinsics.py == 40.0, "video intrinsics scaled");
    require(h.paths->yolo == "/models/yolo.rknn", "absolute path prefixed");
    require(h.paths->head.find("/models/head.rknn") != std::string::npos, "relative path not resolved");
    require(h.node->get_parameter("osd.render_path").as_string() == "cached-overlay", "OSD default path changed");
    for (const auto &parameter : std::vector<rclcpp::Parameter>{{"use_sim_time", true}, {"camera.fx", 200.0},
        {"tracker.enable_dcf", true}, {"video_source", "other.mov"}, {"need_save", true},
        {"osd.enabled", true}, {"osd.timing_enabled", true}, {"osd.render_path", "cached-overlay"}, {"osd.width", 1280}, {"recording.enabled", true},
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
            == static_cast<std::int64_t>(h.source->timestamps.front()), "default did not use capture time");
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
        const auto bag_root{std::filesystem::temp_directory_path() / ("camera-bag-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
        std::filesystem::create_directories(bag_root);
        params.append_parameter_override("need_save", true);
        params.append_parameter_override("save_path", bag_root.string());
        Harness bag{params};
        require(bag.source->starts == 0 && bag.source->legacy_recording_requests == 0,
            "RK3576 camera bag request reached legacy recording boundary");
        bag.node->run();
        bag.until([&] { return !bag.detections.empty(); }, "Camera bag stopped tracking");
        bag.node->stop();
        require(bag.source->legacy_recording_requests == 0
            && DroneTrackerNodeTestAccess::bagStatus(*bag.node).phase == "complete",
            "Camera bag did not close independently");
        std::filesystem::remove_all(bag_root);
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
        require(h.detections.size() == 1 && h.detections.front().header.frame_id.empty()
            && h.images.empty(), "Warmup OFF missing single invalid notification");
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
    for (const auto &input : {std::string{"test.mov"}, std::string{"/dev/video10"}}) {
        std::vector<rclcpp::Parameter> extra{{"video_source", input}, {"recording.enabled", true}};
        if (input.rfind("/dev/", 0) == 0) {
            extra.emplace_back("mv_camera.capture_mode", "continuous");
            extra.emplace_back("mv_camera.trigger_rate_hz", 0.0);
            extra.emplace_back("mv_camera.media_device", "/dev/media0");
            extra.emplace_back("mv_camera.sensor_entity", "ar0234 test");
        }
        Harness unsupported{options(extra)};
        require(!unsupported.source->config.camera_recording.enabled,
            "unsupported input received independent recording request");
        unsupported.node->run();
        unsupported.until([&] { return !unsupported.detections.empty(); },
            "unsupported recording stopped healthy tracking");
        unsupported.node->stop();
    }
    {
        Harness invalid{options({{"video_source", "/dev/video10"}, {"mv_camera.sensor_model", "sc233hgs"},
            {"mv_camera.capture_mode", "continuous"}, {"mv_camera.trigger_rate_hz", 0.0},
            {"mv_camera.media_device", "/dev/media0"}, {"mv_camera.sensor_entity", "sc233hgs test"},
            {"recording.enabled", true}, {"recording.bitrate_bps", 0}})};
        require(!invalid.source->config.camera_recording.preparation_error.empty(),
            "invalid recording parameter did not become deferred session error");
        invalid.node->run();
        invalid.until([&] { return !invalid.detections.empty(); },
            "invalid recording parameter stopped tracking");
        invalid.node->stop();
    }
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
void observationEndpoints() {
    Harness h{};
    auto check{[&](const char *topic, bool publisher, rmw_qos_reliability_policy_t reliability, std::size_t depth) {
        const auto endpoints{publisher ? h.observer->get_publishers_info_by_topic(topic)
            : h.observer->get_subscriptions_info_by_topic(topic)};
        const auto it{std::find_if(endpoints.begin(), endpoints.end(), [](const auto &endpoint) {
            return endpoint.node_name() == "drone_tracker_node";
        })};
        require(it != endpoints.end(), "observation endpoint missing");
        const auto qos{DroneTrackerNodeTestAccess::endpointQos(*h.node, topic).get_rmw_qos_profile()};
        require(qos.reliability == reliability && qos.depth == depth, "observation endpoint QoS mismatch");
    }};
    check("/fpv/target_bbox", true, RMW_QOS_POLICY_RELIABILITY_RELIABLE, 10);
    check("/fpv/target_los", true, RMW_QOS_POLICY_RELIABILITY_RELIABLE, 10);
    check("/fpv/tracker_image", true, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, 1);
    check("/fpv/gimbal", false, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, 10);
    check("/mavros/imu/data", false, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, 100);
    h.node->stop(); h.pump(30ms);
    require(h.detections.empty() && h.observations.empty() && h.images.empty(), "OFF before ON published");
    auto imu{h.observer->create_publisher<sensor_msgs::msg::Imu>("/mavros/imu/data", rclcpp::QoS{100}.best_effort())};
    h.trace->target = true;
    h.node->run();
    for (int index{}; index < 20; ++index) {
        sensor_msgs::msg::Imu message{};
        message.header.stamp = h.observer->now(); message.orientation.w = 1;
        imu->publish(message); h.pump(10ms);
    }
    h.until([&] { return h.detections.size() > 4 && h.observations.size() == h.detections.size(); }, "paired endpoints");
    require(std::any_of(h.detections.begin(), h.detections.end(), [](const auto &message) {
        return !message.header.frame_id.empty();
    }), "no valid bbox for MAVROS-only test");
    require(std::all_of(h.observations.begin(), h.observations.end(), [](const auto &message) {
        return message.header.frame_id.empty();
    }), "MAVROS-only input became camera pose");
    h.node->stop(); h.pump(50ms);
    require(h.observations.size() == h.detections.size(), "paired topic counts");
    require(h.detections.back().header.frame_id.empty() && h.observations.back().header.frame_id.empty(), "OFF valid");
    for (std::size_t index{}; index < h.detections.size(); ++index) {
        require(h.detections[index].header.stamp == h.observations[index].header.stamp, "paired time mismatch");
    }
    const auto count{h.detections.size()}, images{h.images.size()};
    h.node->stop(); h.pump(50ms);
    require(h.detections.size() == count && h.observations.size() == count && h.images.size() == images,
        "duplicate OFF or ROS preview after OFF");
}

void repeatedObservationKeepsImageAndState() {
    Harness h{options({{"publish_prediction", true}})};
    const auto now_us{h.observer->now().nanoseconds() / 1000};
    h.source->fixed_timestamp = now_us - 100000;
    h.source->fixed_timestamp_step = 1000;
    h.source->limit = 8;
    auto pose{std::make_shared<sensor_msgs::msg::Imu>()};
    pose->header.stamp = rclcpp::Time{(now_us - 10000) * 1000}; pose->orientation.w = 1;
    DroneTrackerNodeTestAccess::cameraPose(*h.node, pose);
    h.node->run();
    h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "repeated pose round did not end");
    h.pump(50ms);
    require(h.detections.size() == 1 && h.observations.size() == 1, "repeated reference was published");
    require(h.images.size() == 8 && !h.states.empty(), "observation suppression also suppressed images or state");
}

void observationTemplates() {
    for (const char *name : {"rk3576_video_params.yaml", "rk3576_ar0234_camera_params.yaml",
                           "rk3576_sc233hgs_production_params.yaml"}) {
        auto config{options({{"osd.enabled", false}, {"need_save", false}, {"recording.enabled", false}})};
        config.arguments({"--ros-args", "--params-file", std::string{OBSERVATION_REPO_ROOT} + "/parameters/" + name});
        Harness h{config};
        require(!h.node->get_parameter("publish_prediction").as_bool()
            && h.node->get_parameter("los.rate_filter_tau").as_double() == .12,
            "observation template defaults differ");
        const auto camera{DroneTrackerNodeTestAccess::intrinsics(*h.node)};
        require(camera.fx == 100 && camera.fy == 110, "template implicitly rescaled explicit intrinsics");
    }
}

void pairedBagAdmission() {
    const auto root{std::filesystem::temp_directory_path()
        / ("bag-pair-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(root);
    for (int scenario{}; scenario < 4; ++scenario) {
        std::mutex mutex{};
        std::condition_variable changed{};
        bool entered{}, release{};
        int writes{};
        BagSession::Status status{};
        BagSession session{(root / std::to_string(scenario)).string(), [&](const char *operation) {
            if (std::string{operation} == "open") {
                std::unique_lock<std::mutex> lock{mutex};
                entered = true; changed.notify_all(); changed.wait(lock, [&] { return release; });
            }
            if (std::string{operation} == "write" && scenario == 3 && ++writes == 2) {
                throw std::runtime_error("second half write failure");
            }
        }, [&](const BagSession::Status &result) { status = result; }};
        {
            std::unique_lock<std::mutex> lock{mutex};
            require(changed.wait_for(lock, 2s, [&] { return entered; }), "pair admission gate");
        }
        auto bbox{std::make_shared<vision_msgs::msg::Detection2D>()};
        auto los{std::make_shared<geometry_msgs::msg::TwistStamped>()};
        auto imu{std::make_shared<sensor_msgs::msg::Imu>()};
        bool accepted{};
        if (scenario == 0) {
            for (std::size_t index{}; index < BagSession::max_messages - 1; ++index) {
                require(session.submit(imu, "/fpv/gimbal", rclcpp::Time{1}), "fill pair queue");
            }
            accepted = session.submitPair(bbox, los, rclcpp::Time{1});
        } else if (scenario == 1) {
            accepted = session.submitPair(bbox, los, rclcpp::Time{1}, BagSession::max_bytes - 1, 2);
        } else if (scenario == 2) {
            session.seal();
            accepted = session.submitPair(bbox, los, rclcpp::Time{1});
        } else { accepted = session.submitPair(bbox, los, rclcpp::Time{1}); }
        require(accepted == (scenario == 3), "half pair admitted at capacity or seal");
        const auto snapshot{session.snapshot()};
        if (scenario < 2) {
            require(snapshot.topics.at("/fpv/target_bbox").attempted == 1
                && snapshot.topics.at("/fpv/target_bbox").dropped == 1
                && snapshot.topics.at("/fpv/target_los").dropped == 1, "pair drop counts differ");
        }
        {
            std::lock_guard<std::mutex> lock{mutex}; release = true;
        }
        changed.notify_all(); session.stop();
        require(status.phase == (scenario == 3 ? "failed" : scenario == 2 ? "complete" : "incomplete"),
            "pair incomplete state lost");
        if (scenario == 3) {
            require(status.topics.at("/fpv/target_bbox").written == 1
                && status.topics.at("/fpv/target_los").written == 0, "partial disk write not reported");
        }
    }
    std::filesystem::remove_all(root);
}

void bagQueueLimits() {
    const auto root{std::filesystem::temp_directory_path()
        / ("bag-queue-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(root);
    std::mutex gate{};
    std::condition_variable changed{};
    bool entered{}, release{};
    BagSession::Status result{};
    BagSession session{(root / "round").string(),
        [&](const char *operation) {
            if (std::string{operation} != "open") { return; }
            std::unique_lock<std::mutex> lock{gate};
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release; });
        }, [&](const BagSession::Status &status) { result = status; }};
    {
        std::unique_lock<std::mutex> lock{gate};
        require(changed.wait_for(lock, 2s, [&] { return entered; }), "bag worker did not reach open gate");
    }
    auto pose{std::make_shared<sensor_msgs::msg::Imu>()};
    const auto begin{std::chrono::steady_clock::now()};
    for (std::size_t i{}; i < BagSession::max_messages; ++i) {
        require(session.submit(pose, "/fpv/gimbal", rclcpp::Time{static_cast<std::int64_t>(i)}),
            "bounded bag rejected within message capacity");
    }
    require(!session.submit(pose, "/fpv/gimbal", rclcpp::Time{1})
        && !session.submit(pose, "/fpv/gimbal", rclcpp::Time{1}, BagSession::max_bytes + 1),
        "bounded bag accepted overflow");
    require(std::chrono::steady_clock::now() - begin < 1s,
        "bag producer waited for blocked writer");
    {
        std::lock_guard<std::mutex> lock{gate};
        release = true;
    }
    changed.notify_all();
    session.stop();
    require(result.phase == "incomplete" && result.topics.at("/fpv/gimbal").accepted == BagSession::max_messages
        && result.topics.at("/fpv/gimbal").written == BagSession::max_messages
        && result.topics.at("/fpv/gimbal").dropped == 2, "bag overflow status or counts incorrect");
    rosbag2_cpp::Reader reader{};
    reader.open((root / "round").string());
    std::int64_t expected{};
    while (reader.has_next()) {
        const auto item{reader.read_next()};
        require(item->topic_name == "/fpv/gimbal" && item->time_stamp == expected,
            "bag worker changed accepted per-topic order or timestamp");
        ++expected;
    }
    require(expected == static_cast<std::int64_t>(BagSession::max_messages),
        "bag overflow lost an accepted queued message");
    {
        std::lock_guard<std::mutex> lock{gate};
        entered = false;
        release = false;
    }
    BagSession byte_session{(root / "bytes").string(),
        [&](const char *operation) {
            if (std::string{operation} != "open") { return; }
            std::unique_lock<std::mutex> lock{gate};
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release; });
        }, [&](const BagSession::Status &status) { result = status; }};
    {
        std::unique_lock<std::mutex> lock{gate};
        require(changed.wait_for(lock, 2s, [&] { return entered; }), "byte-limit worker did not reach gate");
    }
    const std::size_t large{40U * 1024U * 1024U};
    require(byte_session.submit(pose, "/fpv/gimbal", rclcpp::Time{1}, large)
        && !byte_session.submit(pose, "/fpv/gimbal", rclcpp::Time{2}, large),
        "bag byte capacity did not reject second payload");
    {
        std::lock_guard<std::mutex> lock{gate};
        release = true;
    }
    changed.notify_all();
    byte_session.stop();
    require(result.phase == "incomplete" && result.topics.at("/fpv/gimbal").accepted == 1
        && result.topics.at("/fpv/gimbal").dropped == 1,
        "bag byte overflow status incorrect");
    std::filesystem::remove_all(root);
}
void bagWithoutImagesAndDualRecording() {
    const auto root{std::filesystem::temp_directory_path()
        / ("bag-no-images-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(root);
    Harness h{options({{"video_source", "/dev/video10"}, {"mv_camera.sensor_model", "sc233hgs"},
        {"mv_camera.capture_mode", "continuous"}, {"mv_camera.trigger_rate_hz", 0.0},
        {"mv_camera.media_device", "/dev/media0"}, {"mv_camera.sensor_entity", "sc233hgs test"},
        {"recording.enabled", true}, {"need_save", true}, {"save_path", root.string()},
        {"visualized_result", false}})};
    require(h.source->config.camera_recording.enabled && h.source->legacy_recording_requests == 0,
        "dual recording switches were coupled at initialization");
    h.source->recording_failed = true;
    h.node->run();
    h.until([&] { return h.detections.size() >= 5; }, "camera recording failure stopped bag or tracking");
    h.node->stop();
    const auto status{DroneTrackerNodeTestAccess::bagStatus(*h.node)};
    require(status.phase == "complete" && status.topics.at("/fpv/target_bbox").written > 0
        && h.images.empty() && h.source->legacy_recording_requests == 0,
        "dual recording or image disablement changed bag state");
    int bags{};
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        rosbag2_cpp::Reader reader{};
        reader.open(entry.path().string());
        const auto metadata{reader.get_all_topics_and_types()};
        require(std::none_of(metadata.begin(), metadata.end(), [](const auto &topic) {
            return topic.name == "/fpv/tracker_image";
        }), "image metadata persisted");
        require(std::none_of(metadata.begin(), metadata.end(), [](const auto &topic) {
            return topic.name == "/fpv/tracker_image";
        }), "disabled image topic appeared in bag metadata");
        std::uint64_t boxes{};
        while (reader.has_next()) {
            const auto item{reader.read_next()};
            require(item->topic_name != "/fpv/tracker_image", "disabled image message appeared in bag");
            if (item->topic_name == "/fpv/target_bbox") { ++boxes; }
        }
        require(boxes == status.topics.at("/fpv/target_bbox").written,
            "no-image bag detection count differs from node count");
        ++bags;
    }
    require(bags == 1, "dual recording produced incorrect bag round count");
    auto display{std::make_shared<DisplayTrace>()};
    Harness failed{options({{"video_source", "/dev/video10"}, {"mv_camera.sensor_model", "sc233hgs"},
        {"mv_camera.capture_mode", "continuous"}, {"mv_camera.trigger_rate_hz", 0.0},
        {"mv_camera.media_device", "/dev/media0"}, {"mv_camera.sensor_entity", "sc233hgs test"},
        {"recording.enabled", true}, {"need_save", true}, {"save_path", root.string()},
        {"visualized_result", false}, {"osd.enabled", true}}), {},
        [](const char *operation) {
            if (std::string{operation} == "write") { throw std::runtime_error("injected bag write failure"); }
        }, {}, std::make_unique<TestDisplay>(display)};
    failed.node->run();
    failed.until([&] { return failed.detections.size() >= 5; }, "bag write fault stopped tracking");
    require(failed.source->active && failed.source->config.camera_recording.enabled,
        "bag write fault stopped independent recording");
    failed.node->stop();
    failed.until([&] { return DroneTrackerNodeTestAccess::idle(*failed.node); }, "continuous bag fault close");
    require(DroneTrackerNodeTestAccess::bagStatus(*failed.node).phase == "failed",
        "dual recording bag fault was not isolated");
    {
        std::lock_guard<std::mutex> lock{display->mutex};
        require(std::any_of(display->states.begin(), display->states.end(), [](const auto &state) {
            return state.new_video;
        }), "bag write fault stopped healthy HDMI frames");
    }
    std::filesystem::remove_all(root);
}
void recording() {
    const auto root{std::filesystem::temp_directory_path()
        / ("ros-video-bags-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(root);
    Harness h{options({{"need_save", true}, {"save_path", root.string()}})};
    auto imu{h.observer->create_publisher<sensor_msgs::msg::Imu>("/fpv/gimbal", rclcpp::QoS{1}.best_effort())};
    auto mavros{h.observer->create_publisher<sensor_msgs::msg::Imu>("/mavros/imu/data", rclcpp::QoS{100}.best_effort())};
    auto odom{h.observer->create_publisher<nav_msgs::msg::Odometry>("/fpv/odom", rclcpp::QoS{10}.best_effort())};
    h.pump(200ms);
    std::vector<BagSession::Status> statuses{};
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
                mavros->publish(message);
                nav_msgs::msg::Odometry position{};
                position.header.stamp = message.header.stamp;
                odom->publish(position);
                std::this_thread::sleep_for(2ms);
            }
        }};
        h.pump(300ms);
        if (round == 1) {
            h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); },
                "video EOF did not close bag while callbacks continued");
        }
        // 发布线程在 OFF/EOF 和 bag 关闭期间持续发送。
        const auto close_begin{std::chrono::steady_clock::now()};
        h.node->stop(); h.pump(30ms);
        require(std::chrono::steady_clock::now() - close_begin < 3s, "normal bag round cleanup exceeded 3 seconds");
        statuses.push_back(DroneTrackerNodeTestAccess::bagStatus(*h.node));
        sending = false;
        publisher.join();
        const auto rotation{DroneTrackerNodeTestAccess::rotation(*h.node)};
        require(std::abs(rotation(0, 1) + 1.0) < 1e-9 && std::abs(rotation(1, 0) - 1.0) < 1e-9,
            "gimbal orientation mapping changed");
    }
    int bags{};
    std::map<std::string, std::uint64_t> read_counts{};
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        rosbag2_cpp::Reader reader{};
        reader.open(entry.path().string());
        std::vector<std::string> topics{};
        while (reader.has_next()) {
            const auto item{reader.read_next()};
            topics.push_back(item->topic_name);
            ++read_counts[item->topic_name];
            if (item->topic_name == "/fpv/gimbal") {
                const auto message{decodeBag<sensor_msgs::msg::Imu>(*item)};
                require(rclcpp::Time{message.header.stamp}.nanoseconds() == item->time_stamp
                    && message.orientation.w > 0.0, "bag gimbal payload or timestamp changed");
            } else if (item->topic_name == "/fpv/odom") {
                const auto message{decodeBag<nav_msgs::msg::Odometry>(*item)};
                require(rclcpp::Time{message.header.stamp}.nanoseconds() == item->time_stamp,
                    "bag odometry timestamp changed");
            } else if (item->topic_name == "/fpv/target_bbox") {
                const auto message{decodeBag<vision_msgs::msg::Detection2D>(*item)};
                require(rclcpp::Time{message.header.stamp}.nanoseconds() == item->time_stamp
                    && std::isfinite(message.bbox.size_x), "bag detection payload or timestamp changed");
            } else if (item->topic_name == "/fpv/target_los") {
                const auto message{decodeBag<geometry_msgs::msg::TwistStamped>(*item)};
                require(rclcpp::Time{message.header.stamp}.nanoseconds() == item->time_stamp, "LOS bag timestamp");
            } else if (item->topic_name == "/mavros/imu/data") {
                const auto message{decodeBag<sensor_msgs::msg::Imu>(*item)};
                require(rclcpp::Time{message.header.stamp}.nanoseconds() == item->time_stamp
                    && message.orientation.w == std::sqrt(0.5), "raw MAVROS bag payload");
            } else if (item->topic_name == "/fpv/tracker_image") {
                const auto message{decodeBag<sensor_msgs::msg::Image>(*item)};
                require(rclcpp::Time{message.header.stamp}.nanoseconds() == item->time_stamp
                    && message.encoding == "bgr8" && !message.data.empty(), "bag image payload or timestamp changed");
            }
        }
        const auto metadata{reader.get_all_topics_and_types()};
        require(std::none_of(metadata.begin(), metadata.end(), [](const auto &topic) {
            return topic.name == "/fpv/tracker_image";
        }), "image metadata persisted");
        for (const auto *topic : {"/fpv/gimbal", "/fpv/odom", "/fpv/target_bbox", "/fpv/target_los", "/mavros/imu/data"}) {
            require(std::find(topics.begin(), topics.end(), topic) != topics.end(), "closed bag missing topic");
            require(std::any_of(metadata.begin(), metadata.end(), [topic](const auto &value) {
                return value.name == topic && !value.type.empty();
            }), "closed bag missing typed topic metadata");
        }
        ++bags;
    }
    require(bags == 2, "incorrect recording rounds");
    std::map<std::string, std::uint64_t> accepted{};
    for (const auto &status : statuses) {
        require(status.phase == "complete", "normal bag was not marked complete");
        for (const auto &topic : status.topics) {
            require(topic.second.dropped == 0 && topic.second.accepted == topic.second.written,
                "complete bag has missing accepted messages");
            accepted[topic.first] += topic.second.accepted;
        }
    }
    require(accepted == read_counts, "bag contents disagree with accepted message counts");
    builtin_interfaces::msg::Time fixed{};
    fixed.sec = 123;
    h.node->run(fixed); h.pump(50ms); h.node->stop();
    const int before{h.source->starts};
    h.node->run(fixed);
    h.pump(50ms); h.node->stop();
    require(h.source->starts > before
        && DroneTrackerNodeTestAccess::bagStatus(*h.node).phase == "failed",
        "existing recording did not fail independently");
    for (const auto *failure : {"open", "write", "close"}) {
        std::atomic_bool inject{true};
        Harness broken{options({{"need_save", true}, {"save_path", root.string()}}), {},
            [failure, &inject](const char *operation) {
                if (inject && std::string{operation} == failure) { throw std::runtime_error("injected bag failure"); }
            }};
        broken.node->run(); broken.pump(100ms); broken.node->stop();
        require(!broken.source->active && broken.source->starts > 0 && !broken.detections.empty(),
            "bag failure changed source lifecycle");
        const auto failed_status{DroneTrackerNodeTestAccess::bagStatus(*broken.node)};
        require(failed_status.phase == "failed" && failed_status.reason.find("injected bag failure") != std::string::npos,
            "bag failure not reported");
        if (std::string{failure} == "write") {
            std::uint64_t accepted{}, written{};
            for (const auto &topic : failed_status.topics) {
                accepted += topic.second.accepted;
                written += topic.second.written;
            }
            require(accepted > written, "bag write failure did not retain unwritten count");
        }
        inject = false;
        broken.node->run(); broken.pump(100ms); broken.node->stop();
        require(broken.source->delivered > 0, "recording failure prevented retry");
    }
    std::filesystem::remove_all(root);
}
void diagnosticBoundaries() {
    for (const char *mode : {"basic", "detail"}) {
        Harness h{options({{"diagnostics.mode", mode}})};
        const auto changed{h.node->set_parameter(rclcpp::Parameter{"diagnostics.mode", "off"})};
        require(!changed.successful, "诊断模式运行期未拒绝");
        h.source->limit = 2; h.source->fixed_timestamp = 1000000;
        const auto before{h.source->stops.load()}; h.node->run();
        h.until([&] { return h.source->stops > before; }, "diagnostic repeated EOF"); h.pump(50ms);
        require(h.detections.size() == 1, "重复时间改变发布规则");
        const auto *region{DroneTrackerNodeTestAccess::diagnostics(*h.node)};
        require(region, "diagnostic region missing");
        const auto &ring{region->producers[0].normal};
        bool early{}, skipped{}, published{};
        for (std::uint64_t index{}; index < ring.write.load(); ++index) {
            const auto &value{ring.events[index % diagnostics::NORMAL_CAPACITY]};
            early = early || (value.type == diagnostics::Type::UpdateEnd && value.reason == diagnostics::Reason::RepeatedTime);
            skipped = skipped || value.type == diagnostics::Type::PublishSkipped;
            published = published || (value.type == diagnostics::Type::PublishEnd && (value.flags & diagnostics::Complete));
        }
        require(early && skipped && published, "节点未消费/发布边界证据缺失");
        // 使等待超时后 EOF 队列仍含三帧，实际覆盖节点补取的跳过和选择事件。
        const auto first_tail_event{ring.write.load()};
        std::size_t first_tail_frame{};
        {
            std::lock_guard<std::mutex> lock{h.source->mutex};
            first_tail_frame = h.source->timestamps.size();
        }
        h.source->fixed_timestamp = 0; h.source->tail_race = true; h.source->limit = 3;
        const auto before_tail{h.source->stops.load()}; h.node->run();
        h.until([&] { return h.source->stops > before_tail; }, "diagnostic tail EOF"); h.pump(50ms);
        require(h.detections.size() == 2, "EOF 补取末帧丢失");
        std::vector<std::uint64_t> tail_times{};
        {
            std::lock_guard<std::mutex> lock{h.source->mutex};
            tail_times.assign(h.source->timestamps.begin() + first_tail_frame, h.source->timestamps.end());
        }
        require(tail_times.size() == 3, "EOF 补取未取得三帧测试输入");
        std::size_t tail_index{};
        for (auto index{first_tail_event}; index < ring.write.load(); ++index) {
            const auto &value{ring.events[index % diagnostics::NORMAL_CAPACITY]};
            if (value.type != diagnostics::Type::FrameSkip && value.type != diagnostics::Type::FrameSelect) {
                continue;
            }
            require(tail_index < tail_times.size(), "EOF 补取重复记录帧去向");
            require(value.type == (tail_index < 2 ? diagnostics::Type::FrameSkip : diagnostics::Type::FrameSelect),
                    "EOF 补取跳过与选择顺序不符");
            require(value.frame_time_us == tail_times[tail_index] && (value.flags & diagnostics::FrameTimeKnown),
                    "EOF 补取帧时间数值或已知有效位不符");
            ++tail_index;
        }
        require(tail_index == 3, "EOF 补取缺少实际帧去向记录");
    }
}

rclcpp::NodeOptions continuousOptions(bool images, const std::string &path = "cached-overlay") {
    return options({{"video_source", "/dev/video10"}, {"mv_camera.sensor_model", "sc233hgs"},
        {"mv_camera.capture_mode", "continuous"}, {"mv_camera.trigger_rate_hz", 0.0},
        {"mv_camera.media_device", "/dev/media0"}, {"mv_camera.sensor_entity", "sc233hgs test"},
        {"osd.enabled", true}, {"osd.render_path", path}, {"visualized_result", images},
        {"recording.enabled", true}, {"mv_camera.width_px", 100}, {"mv_camera.height_px", 80},
        {"mv_camera.output_width_px", 0}, {"mv_camera.output_height_px", 0}});
}
std::uint64_t displayedSequence(const std::shared_ptr<DisplayTrace> &display) {
    std::lock_guard<std::mutex> lock{display->mutex};
    return display->states.empty() ? 0 : display->states.back().sequence;
}
void continuousDiagnosticBoundaries() {
    for (const char *mode : {"basic", "detail"}) {
        auto config{continuousOptions(false)};
        auto parameters{config.parameter_overrides()};
        parameters.emplace_back("diagnostics.mode", mode);
        config.parameter_overrides(parameters);
        auto display{std::make_shared<DisplayTrace>()};
        Harness h{config, {}, {}, {}, std::make_unique<TestDisplay>(display)};
        const auto *region{DroneTrackerNodeTestAccess::diagnostics(*h.node)};
        require(region, "Continuous preview lost diagnostics initialization");
        require(region->producers[0].position.generated.load() == 0,
            "Preview submitted tracking events to the Node producer");
        h.trace->target = true;
        h.node->run();
        h.until([&] { return std::find(h.states.begin(), h.states.end(), 2) != h.states.end(); },
            "Diagnostic continuous round did not reach Tracking");
        {
            std::lock_guard<std::mutex> lock{h.trace->mutex};
            h.trace->block_nano = true;
        }
        h.until([&] { std::lock_guard<std::mutex> lock{h.trace->mutex}; return h.trace->nano_entered; },
            "Diagnostic continuous round did not block NanoTrack");
        h.pump(80ms);
        h.node->stop();
        const auto sequence{displayedSequence(display)};
        h.until([&] { return displayedSequence(display) > sequence + 2; },
            "Diagnostic dispatch blocked OFF preview");
        {
            std::lock_guard<std::mutex> lock{h.trace->mutex};
            h.trace->release_nano = true;
            h.trace->block_nano = false;
        }
        h.trace->cv.notify_all();
        h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); },
            "First diagnostic continuous round did not finish");
        const auto published{h.detections.size()};
        h.node->run();
        h.until([&] { return h.detections.size() > published; },
            "Second diagnostic continuous round did not publish");
        h.node->stop();
        h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); },
            "Second diagnostic continuous round did not finish");
        h.node->shutdown();
        const auto &node_ring{region->producers[static_cast<std::size_t>(diagnostics::Producer::Node)].normal};
        std::array<unsigned, 3> starts{}, ends{}, publications{};
        for (std::uint64_t index{}; index < node_ring.write.load(std::memory_order_acquire); ++index) {
            const auto &value{node_ring.events[index % diagnostics::NORMAL_CAPACITY]};
            require(value.type != diagnostics::Type::FrameSelect && value.type != diagnostics::Type::FrameSkip,
                "Dispatcher shared the tracking diagnostic producer");
            require(value.round_id == 1 || value.round_id == 2, "Continuous diagnostics lost tracking round identity");
            if (value.type == diagnostics::Type::RoundStart) { ++starts[value.round_id]; }
            if (value.type == diagnostics::Type::RoundEnd) { ++ends[value.round_id]; }
            if (value.type == diagnostics::Type::PublishEnd) { ++publications[value.round_id]; }
        }
        require(starts[1] == 1 && starts[2] == 1 && ends[1] == 1 && ends[2] == 1
            && publications[1] > 0 && publications[2] > 0, "Continuous diagnostic round boundaries changed");
        const auto &control_ring{region->producers[static_cast<std::size_t>(diagnostics::Producer::Dispatcher)].normal};
        bool selected{}, replaced{};
        for (std::uint64_t index{}; index < control_ring.write.load(std::memory_order_acquire); ++index) {
            const auto &value{control_ring.events[index % diagnostics::NORMAL_CAPACITY]};
            require(value.type == diagnostics::Type::RunStart || value.type == diagnostics::Type::FrameSelect
                || value.type == diagnostics::Type::FrameSkip, "Tracking wrote to the dispatcher producer");
            if (value.type == diagnostics::Type::FrameSelect || value.type == diagnostics::Type::FrameSkip) {
                require(value.flags & diagnostics::FrameTimeKnown, "Dispatcher diagnostic lost frame time");
            }
            selected = selected || value.type == diagnostics::Type::FrameSelect;
            replaced = replaced || (value.type == diagnostics::Type::FrameSkip
                && value.reason == diagnostics::Reason::LatestSkip);
        }
        require(selected && replaced, "Dispatcher diagnostic selection/replacement missing");
    }
}
void continuousCameraLifecycle() {
    for (bool images : {false, true}) {
        for (const std::string path : {"legacy", "cached-overlay"}) {
            auto display{std::make_shared<DisplayTrace>()};
            Harness h{continuousOptions(images, path), {}, {}, {}, std::make_unique<TestDisplay>(display)};
            h.until([&] { return displayedSequence(display) > 5; }, "automatic camera preview missing");
            require(h.source->starts == 1 && h.source->session_starts == 0
                && h.trace->calls == 0 && h.detections.empty() && h.images.empty() && h.states.empty(),
                "Preview began tracking or recording");
            {
                std::lock_guard<std::mutex> lock{h.trace->mutex};
                h.trace->block = true;
            }
            h.node->run(); h.node->run();
            h.until([&] { std::lock_guard<std::mutex> lock{h.trace->mutex}; return h.trace->entered; },
                "controlled YOLO did not start");
            h.node->stop();
            const auto before{displayedSequence(display)};
            h.until([&] { return displayedSequence(display) > before + 3; }, "OFF waited for YOLO cleanup");
            h.node->run(); h.node->stop(); h.node->run();
            h.pump(100ms);
            require(DroneTrackerNodeTestAccess::round(*h.node) == 1 && h.trace->active == 1,
                "Rapid requests started a round before old YOLO returned");
            {
                std::lock_guard<std::mutex> lock{display->mutex};
                require(display->states.back().status == osd::DisplayStatus::Live
                    && display->states.back().boxes.empty() && !display->states.back().has_tracking_result,
                    "Stopping preview froze or revived old target");
            }
            {
                std::lock_guard<std::mutex> lock{h.trace->mutex};
                h.trace->release = true;
            }
            h.trace->cv.notify_all();
            h.until([&] { return DroneTrackerNodeTestAccess::round(*h.node) == 2 && !h.detections.empty(); },
                "Last desired ON did not create one next round");
            h.pump(100ms);
            require(DroneTrackerNodeTestAccess::round(*h.node) == 2 && h.source->starts == 1,
                "Requests accumulated rounds or reopened camera");
            require(h.images.empty() == !images, "Continuous camera changed image switch");
            h.node->stop();
            h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "continuous round did not finish");
            h.pump(80ms);
            const auto detections{h.detections.size()}, image_count{h.images.size()};
            h.pump(80ms);
            require(h.detections.size() == detections && h.images.size() == image_count && h.source->active,
                "OFF continued business output or stopped stream");
            h.node->shutdown(); h.node->shutdown();
            require(!h.source->active && h.trace->assemblies == 1 && h.trace->unsafe_destroy == 0,
                "Final shutdown left capture or destroyed active models");
        }
    }
}
void continuousBlockedNanoAndTelemetry() {
    auto display{std::make_shared<DisplayTrace>()};
    Harness h{continuousOptions(true), {}, {}, {}, std::make_unique<TestDisplay>(display)};
    h.trace->target = true;
    {
        std::lock_guard<std::mutex> lock{h.trace->mutex}; h.trace->block_nano = true;
    }
    h.node->run();
    h.until([&] { std::lock_guard<std::mutex> lock{h.trace->mutex}; return h.trace->nano_entered; },
        "NanoTrack did not reach controlled block");
    h.pump(100ms);
    require(DroneTrackerNodeTestAccess::replacements(*h.node) > 0, "Slow tracking did not replace bounded slot");
    const auto sequence{displayedSequence(display)};
    h.trigger("MISSION");
    const auto stop_begin{std::chrono::steady_clock::now()};
    h.trigger("OFF");
    require(std::chrono::steady_clock::now() - stop_begin < 500ms, "OFF callback blocked on NanoTrack");
    auto battery{h.observer->create_publisher<sensor_msgs::msg::BatteryState>("/mavros/battery", 10)};
    auto failsafe{h.observer->create_publisher<std_msgs::msg::Bool>("/fpv/failsafe", 10)};
    sensor_msgs::msg::BatteryState value{}; value.present = true; value.voltage = 24;
    h.until([&] {
        battery->publish(value);
        std_msgs::msg::Bool alert{}; alert.data = true; failsafe->publish(alert);
        std::lock_guard<std::mutex> lock{display->mutex};
        return !display->states.empty() && display->states.back().sequence > sequence + 3
            && display->states.back().telemetry.battery.value
            && display->states.back().telemetry.battery.value->voltage == 24
            && display->states.back().telemetry.failsafe.value == true;
    }, "Telemetry or preview stopped while NanoTrack still blocked");
    {
        std::lock_guard<std::mutex> lock{display->mutex};
        require(display->states.back().mission_active && display->states.back().boxes.empty(),
            "OFF changed MISSION or left target decorations");
    }
    h.pump(100ms);
    const auto output_count{h.detections.size()};
    {
        std::lock_guard<std::mutex> lock{h.trace->mutex}; h.trace->release_nano = true;
    }
    h.trace->cv.notify_all();
    h.until([&] { return DroneTrackerNodeTestAccess::idle(*h.node); }, "Nano round did not close");
    h.pump(80ms);
    require(h.detections.size() == output_count, "Late Nano result published after OFF");
    h.trace->throw_frame = true;
    h.node->run();
    h.until([&] { return DroneTrackerNodeTestAccess::round(*h.node) == 2
        && DroneTrackerNodeTestAccess::idle(*h.node); }, "Tracking failure did not isolate round");
    const auto failed_sequence{displayedSequence(display)};
    h.until([&] { return displayedSequence(display) > failed_sequence + 2; }, "Tracking failure stopped preview");
    h.source->fail_input = true;
    h.until([&] { return h.node->serviceFailed(); }, "Camera failure did not notify main execution");
    require(h.node->serviceFailure().find("Camera") != std::string::npos, "Camera failure cause lost");
    h.node->shutdown();
}
void continuousStartupFailures() {
    auto calls{std::make_shared<int>(0)};
    auto display{std::make_shared<DisplayTrace>()};
    bool rejected{};
    try {
        Harness h{continuousOptions(false), [calls](std::function<void()> work) {
            if (++*calls == 2) { throw std::runtime_error("injected dispatcher launch failure"); }
            return std::thread{std::move(work)};
        }, {}, {}, std::make_unique<TestDisplay>(display)};
    } catch (const std::exception &) { rejected = true; }
    require(rejected, "Dispatcher thread launch failure was ignored");
    require(display->shutdowns == 1, "Partial startup did not close display");
}
} // namespace
int main(int argc, char **argv) {
    const bool diagnostic_only{argc == 2 && std::string{argv[1]} == "--diagnostic-boundaries"};
    if (diagnostic_only) { argc = 1; }
    rclcpp::init(argc, argv);
    try {
        const bool continuous_only{argc > 1 && std::string{argv[1]} == "--continuous-camera"};
        if (continuous_only) {
            continuousDiagnosticBoundaries();
            continuousCameraLifecycle();
            continuousBlockedNanoAndTelemetry();
            continuousStartupFailures();
            rclcpp::shutdown();
            return 0;
        }
        diagnosticBoundaries();
        if (diagnostic_only) {
            std::cout << "真实节点重复时间与 EOF 三帧补取的时间有效位检查通过\n";
            rclcpp::shutdown();
            return 0;
        }
        configuration();
        observationEndpoints();
        repeatedObservationKeepsImageAndState();
        observationTemplates();
        pairedBagAdmission();
        osdCompatibility();
        messagesAndRestart();
        cleanupAndFailures();
        recording();
        bagQueueLimits();
        bagWithoutImagesAndDualRecording();
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
