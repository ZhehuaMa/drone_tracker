#include "drone_tracker_node.h"
#include "../cpu_observer.h"

#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sys/wait.h>
#include <unistd.h>
#include "diagnostics/recorder.h"
#include "tests/ros_test_support.h"
#include <yaml-cpp/yaml.h>

// CPU contract tests inject their components through the same factory boundary as the regression entry point.
std::unique_ptr<PlatformFactory> CreatePlatformFactory() {
    throw std::logic_error{"Round test must inject its factory"};
}
struct DroneTrackerNodeTestAccess {
    static bool idle(const DroneTrackerNode &node) {
        return node.run_state_.load() == DroneTrackerNode::RunState::Idle;
    }
};
namespace {
using namespace std::chrono_literals;
using model_test::require;
struct SourceTrace {
    std::atomic<bool> running{};
    std::atomic<std::uint64_t> delivered{};
    std::uint64_t epoch{};
    std::mutex mutex{};
    std::vector<std::weak_ptr<const Frame>> frames{};
    std::set<std::uint64_t> times{};
};
class Source final : public FrameSource {
public:
    explicit Source(std::shared_ptr<SourceTrace> trace) : trace_{std::move(trace)} {}
    bool start() override {
        trace_->epoch = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        trace_->running = true;
        return true;
    }
    bool stop() override { trace_->running = false; return true; }
    bool isRunning() const override { return trace_->running; }
    FrameSourceStatus status() const override {
        FrameSourceStatus value{};
        value.state = trace_->delivered == 30 ? FrameSourceState::Eof
            : trace_->running ? FrameSourceState::Running : FrameSourceState::Stopped;
        return value;
    }
    bool getImageSize(int &width, int &height) const override { width = 100; height = 80; return true; }
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override {
        frame.reset();
        std::this_thread::sleep_for(std::min(timeout, std::chrono::microseconds{20000}));
        if (!trace_->running || trace_->delivered == 30) { return false; }
        const std::uint64_t index{trace_->delivered.fetch_add(1)};
        const std::uint64_t time{trace_->epoch + index * 20000};
        frame = std::make_shared<ros_test::TestFrame<1>>(cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, time);
        std::lock_guard<std::mutex> lock{trace_->mutex};
        trace_->times.insert(time);
        trace_->frames.push_back(frame);
        return true;
    }
    bool tryFrame(FrameRef &frame) override { frame.reset(); return false; }
    void configureRecording(bool enabled, const std::string &, const std::string &) override {
        require(!enabled, "round test unexpectedly enabled recording");
    }
private:
    std::shared_ptr<SourceTrace> trace_;
};
class Factory final : public PlatformFactory {
public:
    Factory(std::shared_ptr<ros_test::Trace> trace, std::shared_ptr<SourceTrace> source)
        : trace_{std::move(trace)}, source_{std::move(source)} {}
    PlatformResult<std::unique_ptr<FrameSource>> createFrameSource(const FrameSourceConfig &config) override {
        require(!config.use_history_timestamp && !config.camera_recording.enabled,
            "round test changed prediction time or enabled capture recording");
        return {std::make_unique<Source>(source_)};
    }
    PlatformResult<std::unique_ptr<FrameBuilder>> createFrameBuilder() override {
        return {std::make_unique<ros_test::Builder>()};
    }
    PlatformResult<TrackerComponents> createTrackerComponents(const ModelPaths &) override {
        ++trace_->assemblies;
        return {ros_test::components(trace_)};
    }
private:
    std::shared_ptr<ros_test::Trace> trace_;
    std::shared_ptr<SourceTrace> source_;
};
rclcpp::NodeOptions options() {
    rclcpp::NodeOptions value{};
    value.parameter_overrides({{"video_source", "round-test.mov"},
        {"tracker.yolo_engine", "/models/yolo.rknn"},
        {"tracker.nano_template_backbone", "/models/template.rknn"},
        {"tracker.nano_search_backbone", "/models/search.rknn"}, {"tracker.nano_head", "/models/head.rknn"},
        {"camera.fx", 100.0}, {"camera.fy", 110.0}, {"camera.cx", 50.0}, {"camera.cy", 40.0},
        {"tracker.enable_dcf", false}, {"tracker.yolo_interval", 1}, {"tracker.lost_max_count", 2},
        {"tracker.frame_buffer_size", 8}, {"visualized_result", false}, {"need_save", false},
        {"recording.enabled", false}, {"osd.enabled", false}, {"use_history_timestamp", false},
        {"use_sim_time", false}, {"diagnostics.mode", "off"}});
    return value;
}
std::vector<diagnostics::Event> events(const diagnostics::SharedRegion &region) {
    std::vector<diagnostics::Event> result{};
    for (const auto &producer : region.producers) {
        require(producer.position.dropped == 0, "round test dropped diagnostic evidence");
        for (std::uint64_t index{}; index < producer.normal.write; ++index) {
            result.push_back(producer.normal.events[index]);
        }
        for (std::uint64_t index{}; index < producer.critical.write; ++index) {
            result.push_back(producer.critical.events[index]);
        }
    }
    return result;
}
void round(int number, const std::filesystem::path &output) {
    auto trace{std::make_shared<ros_test::Trace>()};
    trace->target = number == 1;
    auto source{std::make_shared<SourceTrace>()};
    auto region{std::make_unique<diagnostics::SharedRegion>()};
    region->run_id = static_cast<std::uint64_t>(::getpid());
    region->round_id = 1;
    diagnostics::Recorder recorder{*region, -1, diagnostics::Mode::Basic};
    auto node{std::make_shared<DroneTrackerNode>(options(), std::make_unique<Factory>(trace, source))};
    // The in-process recorder records real call boundaries; it does not claim collector or NPU qualification.
    diagnostics::active.store(&recorder);
    rclcpp::executors::SingleThreadedExecutor executor{};
    executor.add_node(node);
    node->run();
    const auto deadline{std::chrono::steady_clock::now() + 5s};
    while (!DroneTrackerNodeTestAccess::idle(*node) && std::chrono::steady_clock::now() < deadline) {
        executor.spin_some();
        std::this_thread::sleep_for(2ms);
    }
    require(DroneTrackerNodeTestAccess::idle(*node) && source->delivered == 30,
        "node did not complete bounded round");
    node->stop();
    executor.remove_node(node);
    node.reset();
    diagnostics::active.store(nullptr);
    require(trace->assemblies == 1 && trace->active == 0 && trace->unsafe_destroy == 0,
        "previous round model execution survived teardown");
    for (const auto &destroyed : trace->destroyed) {
        require(destroyed == 1, "round component retained after node destruction");
    }
    for (const auto &frame : source->frames) {
        require(frame.expired(), "round retained frame in request slot or history cache after teardown");
    }
    const auto captured{events(*region)};
    std::set<std::uint64_t> history{}, requests{}, completed{}, read{};
    std::size_t valid_outputs{}, publish_count{}, initialize_count{}, history_lookups{};
    bool first_update{}, first_request{}, first_output{};
    for (const auto &event : captured) {
        require(event.run_id == static_cast<std::uint64_t>(::getpid()) && event.round_id == 1,
            "diagnostic event inherited previous process ownership");
        if ((event.flags & diagnostics::FrameTimeKnown) != 0) {
            require(source->times.count(event.frame_time_us) != 0,
                "result, history or output used a frame from another round");
        }
        if (event.type == diagnostics::Type::UpdateStart && !first_update) {
            require(event.update_id == 1, "fresh process inherited update counter");
            first_update = true;
        }
        if (event.type == diagnostics::Type::HistoryAdd) {
            if (history.empty()) { require(event.tracker_frame_id == 1, "history cache did not start empty"); }
            history.insert(event.tracker_frame_id);
        }
        if (event.type == diagnostics::Type::RequestDecision && (event.flags & diagnostics::Complete) != 0) {
            if (!first_request) {
                require(event.request_id == 1, "fresh detector inherited request slot counter");
                for (std::size_t index{}; index < 6; ++index) {
                    require(event.values[index] == 0, "fresh detector inherited executing, pending or result slot");
                }
                first_request = true;
            }
            requests.insert(event.request_id);
        }
        if (event.type == diagnostics::Type::DetectEnd) { completed.insert(event.request_id); }
        if (event.type == diagnostics::Type::ResultRead) { read.insert(event.request_id); }
        if (event.type == diagnostics::Type::HistoryLookup) { ++history_lookups; }
        if (event.type == diagnostics::Type::InitializeStart) { ++initialize_count; }
        if (event.type == diagnostics::Type::Output) {
            if (!first_output) {
                require((event.flags & diagnostics::BoxValid) == 0, "round inherited previous tracking output");
                first_output = true;
            }
            if ((event.flags & diagnostics::BoxValid) != 0) { ++valid_outputs; }
        }
        if (event.type == diagnostics::Type::PublishEnd) {
            require((event.flags & diagnostics::Complete) != 0, "production publish call failed");
            ++publish_count;
        }
    }
    require(first_update && first_request && first_output && !history.empty() && !completed.empty()
        && !read.empty() && publish_count == 30, "round omitted detection, cache, result or publication evidence");
    for (const auto request : completed) {
        require(requests.count(request) != 0, "detector completed inherited request");
    }
    for (const auto request : read) {
        require(completed.count(request) != 0, "node read previous process result slot");
    }
    for (const auto &event : captured) {
        if (event.type == diagnostics::Type::HistoryLookup) {
            require(event.reason != diagnostics::Reason::HistoryMiss
                && history.count(event.source_tracker_frame_id) != 0,
                "lookup used history identity outside the current round");
        }
    }
    if (number == 1) {
        require(valid_outputs > 0 && initialize_count > 0 && history_lookups > 0,
            "first round did not populate tracking and history state");
    } else {
        require(valid_outputs == 0 && initialize_count == 0 && history_lookups == 0,
            "no-target second round inherited prior detection, history or tracking state");
    }
    std::ofstream summary{output};
    summary << "{\"pid\":" << ::getpid() << ",\"round\":" << number << ",\"epoch_us\":" << source->epoch
        << ",\"last_frame_us\":" << *source->times.rbegin() << ",\"valid_outputs\":" << valid_outputs
        << ",\"publish_count\":" << publish_count << "}\n";
    require(static_cast<bool>(summary), "round summary write failed");
}
void launch(const std::string &executable, int number, const std::filesystem::path &output) {
    const std::string argument{std::to_string(number)}, path{output.string()};
    const int pid{::fork()};
    require(pid >= 0, "round fork failed");
    if (pid == 0) {
        ::execl(executable.c_str(), executable.c_str(), "--round", argument.c_str(), path.c_str(), nullptr);
        ::_exit(127);
    }
    int result{};
    const auto deadline{std::chrono::steady_clock::now() + 10s};
    for (;;) {
        const int waited{::waitpid(pid, &result, WNOHANG)};
        if (waited == pid) { break; }
        if (waited < 0 && errno != EINTR) { throw std::runtime_error{"round wait failed"}; }
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &result, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error{"round test process exceeded external deadline"};
        }
        std::this_thread::sleep_for(10ms);
    }
    require(WIFEXITED(result) && WEXITSTATUS(result) == 0, "independent node round failed");
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string{argv[1]} == "--round") {
            int ros_argc{1};
            rclcpp::init(ros_argc, argv);
            auto observation{std::make_unique<tracking_regression::CpuObserver>(32768)};
            const auto cpu_output{std::filesystem::path{argv[3]}.parent_path() / (std::string{"cpu-"} + argv[2])};
            std::filesystem::create_directory(cpu_output);
            try {
                round(std::stoi(argv[2]), argv[3]);
                observation->exportRecords(cpu_output);
                const auto evidence{YAML::LoadFile((cpu_output / "cpu-observation.json").string())};
                require(evidence["writers"][0]["count"].as<int>() > 0
                    && evidence["writers"][2]["count"].as<int>() > 0, "round omitted actual update/detection CPU points");
                require(evidence["writers"][0]["overflow"].as<int>() == 0
                    && evidence["writers"][2]["overflow"].as<int>() == 0, "round CPU observer overflowed");
            }
            catch (...) { diagnostics::active.store(nullptr); rclcpp::shutdown(); throw; }
            rclcpp::shutdown();
            return 0;
        }
        require(argc == 1, "round test received unexpected arguments");
        const auto root{std::filesystem::temp_directory_path()
            / ("tracking-regression-rounds-" + std::to_string(::getpid()))};
        require(std::filesystem::create_directory(root), "round test directory already exists");
        try {
            const std::string executable{std::filesystem::canonical(argv[0]).string()};
            launch(executable, 1, root / "first.json");
            launch(executable, 2, root / "second.json");
            const auto first{YAML::LoadFile((root / "first.json").string())};
            const auto second{YAML::LoadFile((root / "second.json").string())};
            require(first["pid"].as<int>() != second["pid"].as<int>(), "rounds reused same process");
            require(second["epoch_us"].as<std::uint64_t>() > first["last_frame_us"].as<std::uint64_t>(),
                "second round output timestamps inherited first round");
            require(first["valid_outputs"].as<int>() > 0 && second["valid_outputs"].as<int>() == 0,
                "target ownership leaked between rounds");
            std::filesystem::remove_all(root);
        } catch (...) { std::filesystem::remove_all(root); throw; }
        std::cout << "Independent production node rounds reset request, result, history and output ownership\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
