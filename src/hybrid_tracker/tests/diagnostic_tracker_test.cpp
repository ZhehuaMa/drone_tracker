#include "diagnostics/recorder.h"
#include "diagnostics/publication.h"
#include "diagnostic_test_access.h"
#include "yolo_detector.h"
#include "nano_tracker.h"
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
#include "diagnostics/storage.h"
#include "platform/platform_factory.h"
#include "platform/frame_source_ops.h"
#include "platform/jetson/tracker_assembly.h"
#include "hybrid_tracker.h"
#include "model_test_support.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <type_traits>

namespace {
using model_test::require;
static_assert(!std::is_copy_constructible_v<TrackerComponents>);
static_assert(std::is_nothrow_move_constructible_v<TrackerComponents>);
struct Trace {
    std::array<std::atomic<int>, 5> destroyed{};
    std::atomic<int> begins{};
    std::atomic<int> calls{};
    std::atomic<int> starts{};
    std::atomic<int> active{};
    std::atomic<int> unsafe_destroy{};
    std::atomic<int> templates{}, searches{}, fail_templates{}, fail_searches{}, search_delay_ms{};
    std::atomic<int> preprocess_error{}, template_error{static_cast<int>(model::ModelError::ExecutionFailure)};
    bool throw_template{};
    std::atomic<float> target_x{32.0f};
    std::mutex mutex{};
    std::condition_variable cv{};
    bool block{};
    bool entered{};
    bool release{};
};
struct Lifetime {
    std::shared_ptr<Trace> trace;
    int slot;
    ~Lifetime() {
        ++trace->destroyed[slot];
        if (trace->active != 0) {
            ++trace->unsafe_destroy;
        }
    }
};
class View final : public FrameReadView {
public:
    explicit View(cv::Mat image) : image_{std::move(image)} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override {
        return image_.channels() == 1 ? FramePixelFormat::Gray8 : FramePixelFormat::Bgr8;
    }
    std::size_t rowStrideBytes() const noexcept override { return image_.step; }
    const cv::Mat &mat() const noexcept override { return image_; }

private:
    cv::Mat image_;
};
// 不同模板实例是不同真实资源类型，无字符串标识或工厂实例令牌。
template <int Tag> class TestFrame final : public Frame {
public:
    TestFrame(const cv::Mat &image, std::uint64_t timestamp) : image_{image.clone()}, timestamp_{timestamp} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override {
        return image_.channels() == 1 ? FramePixelFormat::Gray8 : FramePixelFormat::Bgr8;
    }
    std::uint64_t timestampUs() const noexcept override { return timestamp_; }
    diagnostics::FrameIdentity diagnosticIdentity() const noexcept override {
        return {timestamp_ / 10000, 1, static_cast<std::uint32_t>(timestamp_ / 10000 + 1000),
                diagnostics::CaptureKnown | diagnostics::DriverKnown};
    }
    std::unique_ptr<FrameReadView> mapRead() const override { return std::make_unique<View>(image_); }

private:
    cv::Mat image_;
    std::uint64_t timestamp_;
};
class Builder final : public FrameBuilder {
public:
    PlatformResult<FrameRef> copyFrom(const cv::Mat &image, std::uint64_t timestamp) override {
        if (image.empty() || (image.type() != CV_8UC1 && image.type() != CV_8UC3)) {
            return PlatformResult<FrameRef>::failure({"mock", "FrameBuilder", {}, "Unsupported image"});
        }
        return {std::make_shared<TestFrame<1>>(image, timestamp)};
    }
};
class Compatibility final : public FrameCompatibility {
public:
    explicit Compatibility(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 4} {}
    PlatformDiagnostic validate(const FrameRef &frame) const override {
        if (!std::dynamic_pointer_cast<const TestFrame<1>>(frame)) {
            return {"mock", "FrameCompatibility", {}, "PlatformTypeMismatch"};
        }
        return {};
    }

private:
    Lifetime lifetime_;
};
class Input final : public PreparedModelInput {
public:
    explicit Input(ModelInputContract contract) : contract_{std::move(contract)} {}
    const ModelInputContract &contract() const noexcept override { return contract_; }

private:
    ModelInputContract contract_;
};
class Session final : public YoloFrameSession {
public:
    explicit Session(ModelInputContract contract) : contract_{std::move(contract)} {}
    cv::Size sourceSize() const noexcept override { return {100, 80}; }
    PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region) override {
        const cv::Size size{region.empty() ? sourceSize() : region.size()};
        return {
            YoloPreparedRegion{std::make_shared<Input>(contract_), calculateLetterboxTransform(size, {64, 64}).value}};
    }

private:
    ModelInputContract contract_;
};
class YoloPre final : public YoloPreprocessor {
public:
    explicit YoloPre(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 1} {}
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &,
                                                                   const ModelInputContract &c) override {
        ++lifetime_.trace->begins;
        return {std::make_unique<Session>(c)};
    }

private:
    Lifetime lifetime_;
};
class NanoPre final : public NanoTrackPreprocessor {
public:
    explicit NanoPre(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 3} {}
    PreparedInputResult prepareTemplate(const FrameRef &, const cv::Point2f &, int,
                                        const ModelInputContract &c) override {
        if (lifetime_.trace->preprocess_error != 0) {
            return PreparedInputResult::failure(static_cast<PreprocessError>(lifetime_.trace->preprocess_error.load()),
                                                "template preparation injected");
        }
        return {std::make_shared<Input>(c)};
    }
    PreparedInputResult prepareSearch(const FrameRef &, const cv::Point2f &, int,
                                      const ModelInputContract &c) override {
        return {std::make_shared<Input>(c)};
    }
    void reset() noexcept override {}

private:
    Lifetime lifetime_;
};
class NumericStorage final : public model::TensorStorage {
public:
    explicit NumericStorage(std::vector<float> values) : values_{std::move(values)} {}
    const float *hostData() const noexcept override { return values_.data(); }
    std::size_t byteSize() const noexcept override { return values_.size() * sizeof(float); }

private:
    std::vector<float> values_{};
};
model::Tensor numericTensor(TensorShape shape, std::vector<float> values) {
    const auto bytes{values.size() * sizeof(float)};
    return {{std::move(shape), TensorElementType::Float32, TensorLayout::Linear, bytes, {}},
            std::make_shared<NumericStorage>(std::move(values))};
}
class Yolo final : public model::YoloBackend {
public:
    explicit Yolo(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 0} {}
    model::Result<int> classCount() const override { return {1}; }
    ModelInputContract inputContract() const override {
        return {{1, 3, 64, 64}, TensorElementType::Float32, TensorLayout::Nchw, 1.0f / 255.0f};
    }
    model::Result<model::YoloPrediction> infer(const PreparedInputRef &) override {
        auto &t{*lifetime_.trace};
        ++t.calls;
        ++t.active;
        {
            std::unique_lock<std::mutex> lock{t.mutex};
            t.entered = true;
            t.cv.notify_all();
            t.cv.wait(lock, [&t] { return !t.block || t.release; });
        }
        --t.active;
        std::vector<float> values(50, 0);
        values[0] = t.target_x.load();
        values[10] = 32;
        values[20] = 12.8f;
        values[30] = 12.8f;
        values[40] = 0.9f;
        return {{numericTensor({1, 5, 10}, std::move(values)), 1, 2}};
    }

private:
    Lifetime lifetime_;
};
class Nano final : public model::NanoTrackBackend {
public:
    explicit Nano(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 2} {}
    model::Status setTemplate(const PreparedInputRef &) override {
        const int count{++lifetime_.trace->templates};
        if (lifetime_.trace->throw_template) { throw std::runtime_error{"unknown numeric code"}; }
        if (count <= lifetime_.trace->fail_templates.load()) {
            return model::Status::failure(
                {static_cast<model::ModelError>(lifetime_.trace->template_error.load()), "mock", {}, {}, {}, "template injected"});
        }
        return {true};
    }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &) override {
        std::this_thread::sleep_for(std::chrono::milliseconds{lifetime_.trace->search_delay_ms.load()});
        const int count{++lifetime_.trace->searches};
        if (count <= lifetime_.trace->fail_searches.load()) {
            return model::Result<model::NanoPrediction>::failure(
                {model::ModelError::ExecutionFailure, "mock", {}, {}, {}, "search injected"});
        }
        return {{model_test::tensor({1, 2, 15, 15}), model_test::tensor({1, 4, 15, 15}, 5)}};
    }
    void reset() noexcept override {}

private:
    Lifetime lifetime_;
};
TrackerComponents components(const std::shared_ptr<Trace> &trace) {
    TrackerComponents result{};
    result.yolo = std::make_unique<Yolo>(trace);
    result.yolo_preprocessor = std::make_unique<YoloPre>(trace);
    result.nano = std::make_unique<Nano>(trace);
    result.nano_preprocessor = std::make_unique<NanoPre>(trace);
    result.compatibility = std::make_unique<Compatibility>(trace);
    return result;
}
DetectionThreadLauncher launcher(const std::shared_ptr<Trace> &trace, bool fail = false) {
    return [trace, fail](std::function<void()> work) {
        ++trace->starts;
        if (fail) {
            throw std::runtime_error("injected thread startup failure");
        }
        return std::thread{std::move(work)};
    };
}
TrackerSettings settings() {
    TrackerSettings result{};
    result.enable_dcf = false;
    result.kf_fx = result.kf_fy = 100;
    return result;
}

std::vector<diagnostics::Event> captured(const diagnostics::SharedRegion &region) {
    std::vector<diagnostics::Event> values{};
    for (const auto &producer : region.producers) {
        const auto normal{producer.normal.write.load(std::memory_order_acquire)};
        const auto critical{producer.critical.write.load(std::memory_order_acquire)};
        for (std::uint64_t index{}; index < normal; ++index) {
            values.push_back(producer.normal.events[index % diagnostics::NORMAL_CAPACITY]);
        }
        for (std::uint64_t index{}; index < critical; ++index) {
            values.push_back(producer.critical.events[index % diagnostics::CRITICAL_CAPACITY]);
        }
    }
    return values;
}
bool hasEvent(const diagnostics::SharedRegion &region, diagnostics::Type type, diagnostics::Reason reason) {
    const auto events{captured(region)};
    return std::any_of(events.begin(), events.end(),
                       [=](const auto &event) { return event.type == type && event.reason == reason; });
}
FrameContext context(std::uint64_t id) {
    FrameContext value{};
    value.frame = std::make_shared<TestFrame<1>>(cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, id * 10000);
    value.now_us = id * 10000 + 100;
    return value;
}
void waitModel(const std::shared_ptr<Trace> &trace, int count) {
    const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
    while (trace->calls.load() < count && std::chrono::steady_clock::now() < end) {
        std::this_thread::yield();
    }
    require(trace->calls.load() >= count, "模型未进入受控调用");
}
struct Recording {
    std::unique_ptr<diagnostics::SharedRegion> region{std::make_unique<diagnostics::SharedRegion>()};
    diagnostics::Recorder recorder;
    explicit Recording(diagnostics::Mode mode) : recorder{*region, -1, mode} {
        region->run_id = 99;
        diagnostics::current_update = diagnostics::current_tracker_frame = diagnostics::current_request = 0;
        diagnostics::current_producer = diagnostics::Producer::Node;
        diagnostics::active.store(mode == diagnostics::Mode::Off ? nullptr : &recorder);
    }
    ~Recording() { diagnostics::active.store(nullptr); }
};
const char *inspector{};
void waitIdle(const HybridTracker &tracker) {
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
    while (!HybridTrackerDiagnosticTestAccess::idle(tracker) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    require(HybridTrackerDiagnosticTestAccess::idle(tracker), "受控检测未在截止内完成");
}
void report(const diagnostics::SharedRegion &region, const std::string &name, const std::string &expected) {
    const auto directory{std::filesystem::temp_directory_path() /
                         ("diagnostic-algorithm-" + name + std::to_string(::getpid()))};
    std::filesystem::remove_all(directory);
    diagnostics::StorageConfig config{};
    config.directory = directory / "raw";
    config.minimum_free_bytes = 0;
    auto metadata{std::make_unique<diagnostics::SharedRegion>()};
    metadata->run_id = region.run_id;
    diagnostics::Storage storage{config, *metadata};
    require(storage.open(), "报告日志创建失败");
    const auto values{captured(region)};
    require(storage.append(values.data(), values.size()) && storage.finish(true), "真实事件保存失败");
    const auto raw{storage.path().string()}, output{(directory / "export").string()};
    const pid_t child{::fork()};
    if (child == 0) {
        ::execl(inspector, inspector, raw.c_str(), "--output", output.c_str(), nullptr);
        ::_exit(127);
    }
    int status{};
    ::waitpid(child, &status, 0);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "检查器失败");
    std::ifstream input{directory / "export/timeline.txt"};
    const std::string timeline{std::istreambuf_iterator<char>{input}, {}};
    require(timeline.find(expected) != std::string::npos, "中文报告与注入事实不一致");
    std::filesystem::remove_all(directory);
}
void testHistoryMissAndPartialCatchup() {
    for (bool evicted : {false, true}) {
        Recording recording{diagnostics::Mode::Basic};
        auto trace{std::make_shared<Trace>()};
        trace->block = true;
        trace->fail_searches = 2;
        auto config{settings()};
        config.frame_buffer_size = evicted ? 3 : 30;
        auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
        require(tracker->init(), "tracker init");
        tracker->update(context(1));
        waitModel(trace, 1);
        for (unsigned frame{2}; frame <= 25; ++frame) {
            tracker->update(context(frame));
        }
        {
            std::lock_guard<std::mutex> lock{trace->mutex};
            trace->release = true;
        }
        trace->cv.notify_all();
        const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        while (!hasEvent(*recording.region, diagnostics::Type::DetectEnd, diagnostics::Reason::None) &&
               std::chrono::steady_clock::now() < end) {
            std::this_thread::yield();
        }
        // Wait for the second accepted request to publish too; both sources are deliberately old.
        waitIdle(*tracker);
        waitIdle(*tracker);
        tracker->update(context(26));
        tracker->finishRound();
        tracker.reset();
        const auto events{captured(*recording.region)};
        if (evicted) {
            require(hasEvent(*recording.region, diagnostics::Type::HistoryLookup, diagnostics::Reason::HistoryMiss),
                    "来源淘汰未记录真实 miss");
            report(*recording.region, "history", "不能推断为检测无目标或初始化失败");
        } else {
            bool partial{};
            std::uint32_t step_count{}, failed_steps{};
            for (const auto &event : events) {
                if (event.type == diagnostics::Type::CatchupStep) {
                    require(event.frame.capture_id >= 2 && event.frame.capture_id <= 26,
                            "补跟踪记录未关联真实历史帧");
                    require(event.frame_time_us == event.frame.capture_id * 10000
                                && (event.flags & diagnostics::FrameTimeKnown),
                            "补跟踪帧时间数值或已知有效位不符");
                    ++step_count;
                    if (!(event.flags & diagnostics::Complete)) { ++failed_steps; }
                }
                if (event.type == diagnostics::Type::CatchupEnd && event.counts[2] == 2 && event.counts[0] > 2 &&
                    (event.flags & diagnostics::Complete)) {
                    partial = true;
                }
            }
            require(step_count > 2 && failed_steps == 2, "未同时验证成功与失败补跟踪步骤的时间");
            require(partial, "部分补帧失败改变继续规则或未记录汇总");
            report(*recording.region, "partial", "失败 2；整体恢复成功");
        }
    }
}
void testInitFailureAndLowIou() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    trace->fail_templates = 1;
    auto config{settings()};
    config.yolo_interval = 1;
    config.iou_reset_threshold = 1.1f;
    config.iou_calibrate_threshold = 1.2f;
    config.max_position_jump = 10000;
    config.max_size_jump_ratio = 10000;
    config.lost_max_count = 5;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "tracker init");
    for (unsigned frame{1}; frame <= 25; ++frame) {
        tracker->update(context(frame));
        waitIdle(*tracker);
    }
    tracker->finishRound();
    tracker.reset();
    require(hasEvent(*recording.region, diagnostics::Type::InitializeEnd, diagnostics::Reason::InitFailed),
            "单次初始化失败未记录");
    bool fifth{};
    for (const auto &event : captured(*recording.region)) {
        if (event.type == diagnostics::Type::Decision && event.reason == diagnostics::Reason::LowIou &&
            event.counts[0] == 4 && event.counts[1] == 5 && (event.flags & diagnostics::Critical)) {
            fifth = true;
        }
    }
    require(fifth, "第五次低 IoU 被误记无目标或未记录清框");
    report(*recording.region, "iou", "低IoU");
}
void testInitializationErrorCodes() {
    for (unsigned scenario{}; scenario < 5; ++scenario) {
        Recording recording{diagnostics::Mode::Basic};
        auto trace{std::make_shared<Trace>()};
        if (scenario < 2) {
            trace->preprocess_error = static_cast<int>(scenario == 0
                ? PreprocessError::InputBusy : PreprocessError::PlatformFailure);
        } else if (scenario < 4) {
            trace->fail_templates = 1;
            trace->template_error = static_cast<int>(scenario == 2
                ? model::ModelError::ContractMismatch : model::ModelError::ExecutionFailure);
        } else { trace->throw_template = true; }
        NanoTracker tracker{std::make_unique<Nano>(trace), std::make_unique<NanoPre>(trace)};
        tracker.init(context(1).frame, {40, 30, 20, 20});
        require(!tracker.isInitialized(), "错误注入未失败");
        const auto events{captured(*recording.region)};
        const auto failure{std::find_if(events.begin(), events.end(), [](const auto &value) {
            return value.type == diagnostics::Type::InitializeEnd && value.reason == diagnostics::Reason::InitFailed;
        })};
        require(failure != events.end(), "缺少初始化底层错误");
        require(((failure->flags & diagnostics::ErrorCodeKnown) != 0) == (scenario < 4), "错误有效性伪造");
        if (scenario < 4) {
            require(failure->values[1] == (scenario < 2 ? 1U : 2U) &&
                    failure->values[2] == static_cast<unsigned>(scenario < 2
                        ? trace->preprocess_error.load() : trace->template_error.load()),
                    "真实错误域或码丢失");
        }
        report(*recording.region, "error" + std::to_string(scenario), scenario == 4 ? "数值错误未知" :
               (scenario < 2 ? "数值错误域=预处理，码=" : "数值错误域=模型，码="));
    }
}
void testNanoFailureWithHighIou() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    trace->fail_searches = 100;
    auto config{settings()};
    config.yolo_interval = 1;
    config.iou_reset_threshold = 0.1f;
    config.roi_scale_factor = 100;
    config.max_position_jump = config.max_size_jump_ratio = 10000;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "tracker init");
    for (unsigned id{1}; id <= 5; ++id) { tracker->update(context(id)); waitIdle(*tracker); }
    const auto events{captured(*recording.region)};
    require(std::any_of(events.begin(), events.end(), [](const auto &value) {
        return value.type == diagnostics::Type::Decision && value.reason == diagnostics::Reason::NanoUpdateFailed
            && value.iou >= value.threshold && value.values[2] == 0;
    }), "高IoU但Nano失败被误记低IoU");
    tracker->finishRound(); tracker.reset();
    report(*recording.region, "high-iou", "跟踪更新失败");
}
void testActualJumpCounters() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    auto config{settings()};
    config.yolo_interval = 100;
    config.jump_tolerance = 3;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "tracker init");
    tracker->update(context(1)); waitIdle(*tracker);
    tracker->update(context(2)); waitIdle(*tracker);
    HybridTrackerDiagnosticTestAccess::jump(*tracker, 0, -1, -1);
    tracker->update(context(3)); tracker->update(context(4));
    HybridTrackerDiagnosticTestAccess::jump(*tracker, 2, 10000, 10000);
    tracker->update(context(5));
    HybridTrackerDiagnosticTestAccess::jump(*tracker, 5, -1, -1);
    tracker->update(context(6));
    bool first{}, second{}, recovered{}, reset{}, basis{};
    for (const auto &value : captured(*recording.region)) {
        if (value.type == diagnostics::Type::JumpComparison && value.values[0] == 1 &&
            (value.flags & diagnostics::OtherBoxValid)) { basis = true; }
        if (value.type != diagnostics::Type::Decision || !(value.flags & diagnostics::NanoCounter)) { continue; }
        first |= value.reason == diagnostics::Reason::NanoJump
                 && value.counts[0] == 0 && value.counts[1] == 1 && value.counts[2] == 1;
        second |= value.reason == diagnostics::Reason::NanoJump
                  && value.counts[0] == 1 && value.counts[1] == 2 && value.counts[2] == 2;
        recovered |= value.reason == diagnostics::Reason::None && value.counts[0] == 2 && value.counts[2] == 0;
        reset |= value.reason == diagnostics::Reason::NanoJump
                 && value.counts[0] == 5 && value.counts[1] == 6 && value.counts[2] == 0
                 && (value.flags & diagnostics::Critical)
                 && (value.flags & diagnostics::OtherBoxValid) && value.other_box[2] > 0;
    }
    require(first && second && recovered && reset && basis, "跳变未达门槛、归零或实际计数/比较框缺失");
    tracker->finishRound(); tracker.reset();
    report(*recording.region, "jump-counters", "实际计数 [5 6 0 3");
}
std::vector<std::pair<TrackerOutput, std::pair<float, std::uint64_t>>> runMode(diagnostics::Mode mode,
                                                                               std::array<int, 3> &calls) {
    Recording recording{mode};
    auto trace{std::make_shared<Trace>()};
    auto config{settings()};
    config.yolo_interval = 1;
    config.max_position_jump = 10000;
    config.max_size_jump_ratio = 10000;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "mode init");
    std::vector<std::pair<TrackerOutput, std::pair<float, std::uint64_t>>> outputs{};
    auto frame{context(1)};
    auto view{frame.frame->mapRead()};
    const auto hash{[](const cv::Mat &image) {
        std::uint64_t value{1469598103934665603ULL};
        for (int row{}; row < image.rows; ++row) {
            for (int column{}; column < image.cols * image.channels(); ++column) {
                value = (value ^ image.ptr<unsigned char>(row)[column]) * 1099511628211ULL;
            }
        }
        return value;
    }};
    const auto before{hash(view->mat())};
    outputs.push_back({tracker->update(frame), {tracker->lastNanoScore(), static_cast<std::uint64_t>(frame.now_us)}});
    waitIdle(*tracker);
    for (unsigned index{2}; index <= 8; ++index) {
        auto input{context(index)};
        outputs.push_back(
            {tracker->update(input), {tracker->lastNanoScore(), static_cast<std::uint64_t>(input.now_us)}});
        waitIdle(*tracker);
    }
    outputs.push_back({tracker->update(context(8)),
                       {tracker->lastNanoScore(),
                        static_cast<std::uint64_t>(context(8).now_us)}}); // repeated time must not consume/publish.
    require(!outputs.back().first.consumed, "重复时间未早退");
    require(hash(view->mat()) == before, "图像内容变化");
    tracker->finishRound();
    tracker.reset();
    calls = {trace->calls.load(), trace->templates.load(), trace->searches.load()};
    require(frame.frame.use_count() == 1, "诊断保留额外帧引用");
    if (mode == diagnostics::Mode::Detail) {
        require(hasEvent(*recording.region, diagnostics::Type::Timing, diagnostics::Reason::None), "detail 未记录阶段");
    }
    return outputs;
}
void testOverwrite() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    trace->block = true;
    auto value{components(trace)};
    YoloV8Detector detector{std::move(value.yolo), std::move(value.yolo_preprocessor), 0.5f, 0.4f, 0, 1.0f};
    require(detector.init(), "detector init");
    require(detector.request(context(1).frame, 1), "first accepted");
    waitModel(trace, 1);
    require(detector.request(context(2).frame, 2), "推理中下一请求应被接受");
    require(!detector.request(context(3).frame, 3), "pending occupied must reject");
    {
        std::lock_guard<std::mutex> lock{trace->mutex};
        trace->release = true;
    }
    trace->cv.notify_all();
    const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
    while (!hasEvent(*recording.region, diagnostics::Type::ResultOverwrite, diagnostics::Reason::None) &&
           std::chrono::steady_clock::now() < end) {
        std::this_thread::yield();
    }
    YoloResult result{};
    require(detector.get_result(result) && result.frame_id == 2, "被覆盖结果或读取身份错误");
    detector.stop();
    require(hasEvent(*recording.region, diagnostics::Type::ResultOverwrite, diagnostics::Reason::None),
            "覆盖原因不可见");
    report(*recording.region, "overwrite", "结果覆盖");
}
class LatestSource final : public FrameSource {
public:
    bool start() override { return true; }
    bool stop() override { return true; }
    bool isRunning() const override { return true; }
    bool getImageSize(int &w, int &h) const override {
        w = 100;
        h = 80;
        return true;
    }
    bool waitFrame(FrameRef &frame, std::chrono::microseconds) override { return take(frame); }
    bool tryFrame(FrameRef &frame) override { return take(frame); }
    bool take(FrameRef &frame) {
        if (frames.empty()) {
            return false;
        }
        frame = std::move(frames.front());
        frames.pop_front();
        return true;
    }
    void configureRecording(bool, const std::string &, const std::string &) override {}
    std::deque<FrameRef> frames{};
};
void testLatestFrames() {
    Recording recording{diagnostics::Mode::Basic};
    LatestSource source{};
    for (unsigned id{1}; id <= 3; ++id) {
        source.frames.push_back(context(id).frame);
    }
    FrameRef chosen{};
    require(waitForLatestFrame(source, chosen, std::chrono::milliseconds{1}), "latest source");
    require(chosen->diagnosticIdentity().capture_id == 3, "selected wrong frame");
    std::vector<std::uint64_t> skipped{};
    for (const auto &value : captured(*recording.region)) {
        if (value.type == diagnostics::Type::FrameSkip) {
            skipped.push_back(value.frame.capture_id);
        }
    }
    require(skipped == std::vector<std::uint64_t>{1, 2}, "节点实际丢弃身份不完整");
}
void testTimeAndPublication() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
    require(tracker->init(), "time init");
    tracker->update(context(1));
    waitIdle(*tracker);
    tracker->update(context(2));
    waitIdle(*tracker);
    trace->search_delay_ms = 30;
    auto input{context(3)};
    tracker->update(input);
    waitIdle(*tracker);
    bool duration_known{};
    for (const auto &value : captured(*recording.region)) {
        if (value.type == diagnostics::Type::UpdateEnd &&
            value.prediction_us == static_cast<std::uint64_t>(input.now_us) && value.duration_ns >= 30000000) {
            duration_known = true;
        }
    }
    require(duration_known, "长调用覆盖了预测时间或缺少单调耗时");
    auto publication{diagnostics::event(diagnostics::Type::PublishStart)};
    publication.prediction_us = input.now_us;
    bool thrown{};
    try {
        diagnostics::observePublication(publication, [] { throw std::runtime_error{"publish injected"}; });
    } catch (const std::runtime_error &) {
        thrown = true;
    }
    require(thrown && hasEvent(*recording.region, diagnostics::Type::PublishEnd, diagnostics::Reason::PublishException),
            "发布异常未保留原传播行为");
    diagnostics::observePublication(publication, [] {});
    require(hasEvent(*recording.region, diagnostics::Type::PublishEnd, diagnostics::Reason::None), "发布正常返回缺失");
    tracker->finishRound();
    tracker.reset();
    report(*recording.region, "publication", "发布异常");
}
void testUnknownFrameTime() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
    require(tracker->init(), "unknown time init");
    FrameContext input{};
    input.now_us = 100;
    require(!tracker->update(input).consumed, "空输入被算法消费");
    tracker->finishRound();
    tracker.reset();
    std::uint32_t boundaries{};
    for (const auto &value : captured(*recording.region)) {
        if (value.type == diagnostics::Type::UpdateStart || value.type == diagnostics::Type::UpdateEnd) {
            require(value.frame_time_us == 0 && !(value.flags & diagnostics::FrameTimeKnown),
                    "没有帧的输入伪造已知时间");
            ++boundaries;
        }
    }
    require(boundaries == 2, "空输入起止记录缺失");
}
void testModes() {
    std::array<int, 3> reference_calls{};
    const auto reference{runMode(diagnostics::Mode::Off, reference_calls)};
    for (auto mode : {diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        std::array<int, 3> calls{};
        const auto observed{runMode(mode, calls)};
        require(calls == reference_calls, "模式改变模型调用次数");
        require(observed.size() == reference.size(), "输出长度不同");
        for (std::size_t index{}; index < observed.size(); ++index) {
            const auto &a{observed[index].first}, &b{reference[index].first};
            require(observed[index].second == reference[index].second, "分数或消息预测时间变化");
            require(a.consumed == b.consumed && a.tracking == b.tracking && a.state == b.state &&
                        a.display_box.cx == b.display_box.cx && a.display_box.cy == b.display_box.cy &&
                        a.display_box.width == b.display_box.width && a.display_box.height == b.display_box.height &&
                        a.realtime_box.cx == b.realtime_box.cx && a.realtime_box.cy == b.realtime_box.cy &&
                        a.realtime_box.width == b.realtime_box.width &&
                        a.realtime_box.height == b.realtime_box.height &&
                        a.display_box.is_valid == b.display_box.is_valid &&
                        a.realtime_box.is_valid == b.realtime_box.is_valid,
                    "模式改变真实算法输出");
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "inspector argument");
        inspector = argv[1];
        testLatestFrames();
        testTimeAndPublication();
        testUnknownFrameTime();
        testOverwrite();
        testHistoryMissAndPartialCatchup();
        testInitFailureAndLowIou();
        testInitializationErrorCodes();
        testNanoFailureWithHighIou();
        testActualJumpCounters();
        testModes();
        std::cout << "真实模拟模型的来源淘汰、初始化、低 IoU、部分补帧和三模式检查通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
