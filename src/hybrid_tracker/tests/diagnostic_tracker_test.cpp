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

bool HybridTrackerDiagnosticTestAccess::unreadResult(const YoloV8Detector &detector, YoloResult &result) {
    std::lock_guard<std::mutex> lock{detector.m_mutex};
    if (detector.m_executing || !detector.m_result_ready) {
        return false;
    }
    result = detector.m_last_result;
    return true;
}

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
    bool fail_yolo_prepare{}, throw_yolo_infer{};
    std::atomic<float> target_x{32.0f};
    std::atomic<float> target_score{0.9f};
    bool second_target{};
    std::vector<std::uint64_t> observed_source_times{};
    std::mutex mutex{};
    std::condition_variable cv{};
    bool block{};
    bool entered{};
    bool release{};
    bool block_template{};
    bool template_entered{};
    bool release_template{};
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
    explicit Session(ModelInputContract contract, std::shared_ptr<Trace> trace)
        : contract_{std::move(contract)}, trace_{std::move(trace)} {}
    cv::Size sourceSize() const noexcept override { return {100, 80}; }
    PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region) override {
        if (trace_->fail_yolo_prepare) {
            return PreprocessResult<YoloPreparedRegion>::failure(PreprocessError::EmptyFrame, "injected rejection");
        }
        const cv::Size size{region.empty() ? sourceSize() : region.size()};
        return {
            YoloPreparedRegion{std::make_shared<Input>(contract_), calculateLetterboxTransform(size, {64, 64}).value}};
    }

private:
    ModelInputContract contract_;
    std::shared_ptr<Trace> trace_;
};
class YoloPre final : public YoloPreprocessor {
public:
    explicit YoloPre(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 1} {}
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &frame,
                                                                   const ModelInputContract &c) override {
        ++lifetime_.trace->begins;
        {
            std::lock_guard<std::mutex> lock{lifetime_.trace->mutex};
            lifetime_.trace->observed_source_times.push_back(frame->timestampUs());
        }
        return {std::make_unique<Session>(c, lifetime_.trace)};
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
        if (t.throw_yolo_infer) { throw std::runtime_error{"injected inference failure"}; }
        ++t.active;
        {
            std::unique_lock<std::mutex> lock{t.mutex};
            t.entered = true;
            t.cv.notify_all();
            require(t.cv.wait_for(lock, std::chrono::seconds{2}, [&t] { return !t.block || t.release; }),
                    "inference gate exceeded its deadline");
        }
        --t.active;
        std::vector<float> values(50, 0);
        values[0] = t.target_x.load();
        values[10] = 32;
        values[20] = 12.8f;
        values[30] = 12.8f;
        values[40] = t.target_score.load();
        if (t.second_target) {
            values[1] = 58; values[11] = 32; values[21] = 12.8f; values[31] = 12.8f; values[41] = 0.94f;
        }
        return {{numericTensor({1, 5, 10}, std::move(values)), 1, 2}};
    }

private:
    Lifetime lifetime_;
};
class Nano final : public model::NanoTrackBackend {
public:
    explicit Nano(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 2} {}
    model::Status setTemplate(const PreparedInputRef &) override {
        auto &trace{*lifetime_.trace};
        {
            std::unique_lock<std::mutex> lock{trace.mutex};
            trace.template_entered = true;
            trace.cv.notify_all();
            require(trace.cv.wait_for(lock, std::chrono::seconds{2}, [&] {
                return !trace.block_template || trace.release_template;
            }), "template gate exceeded its deadline");
        }
        const int count{++trace.templates};
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
std::atomic<std::int64_t> decision_ns{1000000000};
FrameContext context(std::uint64_t id) {
    FrameContext value{};
    value.frame = std::make_shared<TestFrame<1>>(cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, id * 10000);
    value.now_us = id * 10000 + 100;
    decision_ns = static_cast<std::int64_t>(id * 10000000 + 1000000000);
    value.decision_clock = [] { return decision_ns.load(); };
    value.evidence = {tracking::ClockMapping::StandaloneAdmissionV1, decision_ns.load(), true};
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
void testActualInferenceObservation() {
    for (int failure{0}; failure < 3; ++failure) {
        Recording recording{diagnostics::Mode::Basic};
        auto trace{std::make_shared<Trace>()};
        trace->fail_yolo_prepare = failure == 1;
        trace->throw_yolo_infer = failure == 2;
        YoloV8Detector detector{std::make_unique<Yolo>(trace), std::make_unique<YoloPre>(trace),
            0.5f, 0.45f, 0, 1.0f};
        require(detector.init(), "inference observation detector init");
        require(detector.request(context(1).frame, 1), "inference observation request");
        YoloResult result{};
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        bool read{};
        while (!(read = detector.get_result(result)) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        detector.stop();
        require(read, "inference observation result timeout");
        int observed{}, stages{};
        for (const auto &value : captured(*recording.region)) {
            if (value.type == diagnostics::Type::DetectStage) {
                ++stages;
                observed += (value.flags & diagnostics::Observed) != 0;
            }
        }
        require(stages > 0 && observed == trace->calls.load(), "stage count is not actual inference count");
        require(failure == 1 ? observed == 0 : observed > 0, "inference attempted marker incorrect");
    }
}
void testHistoryMissAndPartialCatchup() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        for (bool evicted : {false, true}) {
            Recording recording{mode};
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
            waitIdle(*tracker);
            YoloResult old{};
            old.frame_id = 1; old.source.width = 100; old.source.height = 80;
            old.source.capture_time_us = 10000; old.has_target = true;
            old.best.box = {40, 30, 20, 20};
            HybridTrackerDiagnosticTestAccess::result(*tracker, old);
            tracker->update(context(26));
            tracker->finishRound();
            tracker.reset();
            const auto events{captured(*recording.region)};
            if (mode == diagnostics::Mode::Off) { continue; }
            if (evicted) {
                require(hasEvent(*recording.region, diagnostics::Type::HistoryLookup, diagnostics::Reason::HistoryMiss),
                        "来源淘汰未记录真实 miss");
                report(*recording.region, "history", "不能推断为检测无目标或初始化失败");
            } else {
                const auto failed{std::count_if(events.begin(), events.end(), [](const auto &event) {
                    return event.type == diagnostics::Type::CatchupStep && !(event.flags & diagnostics::Complete);
                })};
                require(failed == 1, "catch-up did not stop at first failed intermediate step");
                require(!std::any_of(events.begin(), events.end(), [](const auto &event) {
                    return event.type == diagnostics::Type::Output && event.values[1];
                }), "failed catch-up authorized output");
            }
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
    for (unsigned frame{1}; frame <= 200; ++frame) {
        auto input{context(frame)};
        input.decision_clock = [] { return 1000000000LL; };
        input.evidence.anchor_ns = 1000000000;
        tracker->update(input);
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
    auto trace{std::make_shared<Trace>()}; auto config{settings()}; config.yolo_interval = 1;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "tracking failure init");
    for (unsigned id{1}; id <= 3; ++id) { tracker->update(context(id)); waitIdle(*tracker); }
    require(tracker->validity()->snapshot().allowed, "failure prerequisite was not verified");
    trace->fail_searches = trace->searches.load() + 1;
    const auto failed{tracker->update(context(4))}; waitIdle(*tracker);
    require(!failed.tracking && tracker->validity()->pending().has_value(),
        "tracking failure retained permission despite detector result");
    tracker->finishRound();
}
void testActualJumpCounters() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()};
    auto config{settings()}; config.yolo_interval = 1; config.iou_reset_threshold = 0.01f;
    config.roi_scale_factor = 100;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "jump stub tracker init");
    for (unsigned id{1}; id <= 3; ++id) { tracker->update(context(id)); waitIdle(*tracker); }
    for (unsigned id{4}; id <= 8; ++id) {
        HybridTrackerDiagnosticTestAccess::jump(*tracker, 5, -1, -1);
        const auto output{tracker->update(context(id))}; waitIdle(*tracker);
        require(output.consumed && output.tracking, "inactive jump rule blocked valid update");
    }
    require(!hasEvent(*recording.region, diagnostics::Type::Decision, diagnostics::Reason::NanoJump)
        && !hasEvent(*recording.region, diagnostics::Type::Decision, diagnostics::Reason::YoloJump),
        "inactive jump counter retained decision authority");
    tracker->finishRound();
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
void testHistoricalArbitrationAndWatchdog() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        for (const int interval : {1, 4}) {
            for (const bool delayed_match : {false, true}) {
                Recording recording{mode};
                auto trace{std::make_shared<Trace>()}; auto config{settings()};
                config.yolo_interval = interval;
                auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
                require(tracker->init(), "elapsed candidate tracker init");
                HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame);
                const auto old{tracker->validity()->snapshot()};
                require(tracker->validity()->revoke(tracking::LossReason::Expired, decision_ns.load(), 10000),
                    "expiry prerequisite did not revoke");
                const auto attempt{tracker->validity()->begin(old.round, 0, 0, decision_ns.load(), 1000)};
                require(attempt && tracker->validity()->prepared(*attempt), "provisional test setup");
                trace->block = true;
                const int limit{5 * interval * 3};
                for (int index{1}; index <= limit + 1; ++index) {
                    tracker->update(context(index + 1));
                    const auto counters{HybridTrackerDiagnosticTestAccess::counters(*tracker)};
                    require(counters[1] == 0 && counters[2] != 0,
                        "missing detector reply abandoned provisional work by frame count");
                }
                auto input{context(limit + 3)};
                if (delayed_match) {
                    YoloResult reply{}; reply.frame_id = 500; reply.has_target = true;
                    reply.best = {{40, 30, 20, 20}, 0.9f, 0};
                    reply.source.frame_id = reply.frame_id; reply.source.generation = old.round;
                    reply.source.capture_time_us = input.frame->timestampUs();
                    reply.source.attempt = attempt->attempt; reply.source.width = 100; reply.source.height = 80;
                    reply.source.tracker_valid = true; reply.source.tracker_box = reply.best.box;
                    reply.source.evidence = input.evidence;
                    HybridTrackerDiagnosticTestAccess::result(*tracker, reply);
                    require(tracker->update(input).tracking,
                        "eligible delayed result could not verify before elapsed candidate deadline");
                } else {
                    decision_ns = attempt->attempt_deadline_ns;
                    input.evidence.anchor_ns = decision_ns.load();
                    require(!tracker->update(input).tracking
                        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[2] == 0,
                        "candidate was not abandoned at exact elapsed deadline");
                }
                const auto loss{tracker->validity()->pending()};
                require(loss.has_value(), "original expiry event disappeared");
                tracker->validity()->dispose(*loss);
                require(!tracker->validity()->pending(), "candidate cleanup emitted duplicate loss");
                { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = true; }
                trace->cv.notify_all(); tracker->finishRound();
            }
        }
        Recording recording{mode}; auto trace{std::make_shared<Trace>()};
        auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
        require(tracker->init(), "metadata tracker init");
        unsigned id{1};
        auto reply{[&](bool comparable, bool target, bool stale, std::uint64_t generation) {
            HybridTrackerDiagnosticTestAccess::prime(*tracker, context(id).frame, 4, 60);
            YoloResult result{}; result.frame_id = 744; result.has_target = target;
            result.best.box = {0, 0, 20, 20}; result.source.tracker_box = result.best.box;
            result.source.width = 100; result.source.height = 80; result.source.tracker_valid = comparable;
            result.source.generation = generation;
            result.source.evidence = {tracking::ClockMapping::StandaloneAdmissionV1,
                decision_ns.load() - (stale ? 200000000 : 0), true};
            HybridTrackerDiagnosticTestAccess::result(*tracker, result);
            return tracker->update(context(++id));
        }};
        reply(false, true, false, 0);
        require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 4, "missing context changed counter");
        reply(true, true, true, 0);
        require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 4, "stale result changed counter");
        reply(true, false, false, 999);
        require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 4, "old round changed counter");
        reply(true, true, false, 0);
        require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0, "metadata-only matching did not confirm");
        const auto lost{reply(true, false, false, 0)};
        require(!lost.tracking && tracker->validity()->pending().has_value(), "first fresh negative failed to revoke");
        tracker->finishRound();
    }
}

void testOverwrite() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        Recording recording{mode};
        auto trace{std::make_shared<Trace>()};
        trace->block = true;
        auto value{components(trace)};
        YoloV8Detector detector{std::move(value.yolo), std::move(value.yolo_preprocessor), 0.5f, 0.4f, 0, 1.0f};
        require(detector.init(), "detector init");
        require(detector.request(context(1).frame, 1), "first accepted");
        waitModel(trace, 1);
        require(detector.request(context(2).frame, 2), "推理中下一请求应被接受");
        require(detector.request(context(3).frame, 3), "pending replacement must succeed");
        {
            std::lock_guard<std::mutex> lock{trace->mutex};
            trace->release = true;
        }
        trace->cv.notify_all();
        const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        YoloResult ready{};
        while (!HybridTrackerDiagnosticTestAccess::unreadResult(detector, ready) &&
               std::chrono::steady_clock::now() < end) {
            std::this_thread::yield();
        }
        YoloResult result{};
        require(detector.get_result(result) && result.frame_id == 1, "unread first result was overwritten");
        DetectorControl accepted{}; accepted.accepted_job = result.job_id;
        detector.control(accepted);
        const auto next_deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        while (!HybridTrackerDiagnosticTestAccess::unreadResult(detector, ready)
            && std::chrono::steady_clock::now() < next_deadline) { std::this_thread::yield(); }
        require(detector.get_result(result) && result.frame_id == 3, "latest pending result identity incorrect");
        detector.stop();
        require(trace->observed_source_times == std::vector<std::uint64_t>{10000, 30000},
                "replacement changed executing pixels or picked superseded input");
        if (mode != diagnostics::Mode::Off) {
            require(!hasEvent(*recording.region, diagnostics::Type::ResultOverwrite, diagnostics::Reason::None),
                    "protected unread result was overwritten");
            const auto events{captured(*recording.region)};
            require(std::count_if(events.begin(), events.end(), [](const auto &event) {
                return event.type == diagnostics::Type::RequestCancel
                    && event.reason == diagnostics::Reason::Superseded && event.source_tracker_frame_id == 2;
            }) == 1, "pending replacement was not accounted exactly once");
            report(*recording.region, "overwrite", "Superseded");
        }
    }
}

void testSubmittedSnapshotAfterNanoFailure() {
    for (auto mode : {diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        for (bool fail_search : {false, true}) {
            Recording recording{mode};
            auto trace{std::make_shared<Trace>()};
            auto config{settings()};
            config.yolo_interval = 1;
            config.iou_reset_threshold = 0;
            config.iou_calibrate_threshold = 1;
            auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
            require(tracker->init(), "snapshot tracker init");
            HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame, 4, 0);

            // Release even on an assertion failure, before the tracker joins its worker.
            struct InferenceBarrier {
                std::shared_ptr<Trace> trace;
                explicit InferenceBarrier(std::shared_ptr<Trace> value) : trace{std::move(value)} {
                    std::lock_guard<std::mutex> lock{trace->mutex};
                    trace->block = true;
                }
                ~InferenceBarrier() { release(); }
                void release() {
                    {
                        std::lock_guard<std::mutex> lock{trace->mutex};
                        trace->release = true;
                    }
                    trace->cv.notify_all();
                }
            } barrier{trace};

            trace->fail_searches = fail_search ? 1 : 0;
            const auto source_output{tracker->update(context(1))};
            waitModel(trace, 1);
            require(trace->searches == 1, "snapshot source did not execute exactly one NanoTrack update");
            require(source_output.consumed && source_output.tracking == !fail_search,
                    "snapshot source lost its visible display box");
            barrier.release();
            waitIdle(*tracker);

            // Consume the real detector result on a later, successful NanoTrack update.
            // SourceContext.BoxValid is copied directly from YoloResult.source.tracker_valid.
            const auto current_output{tracker->update(context(2))};
            waitIdle(*tracker);
            require(current_output.consumed,
                    "snapshot result was not consumed after a successful current update");
            const auto events{captured(*recording.region)};
            const auto snapshot{std::find_if(events.begin(), events.end(), [](const auto &event) {
                return event.type == diagnostics::Type::SourceContext && event.source_tracker_frame_id == 1;
            })};
            require(snapshot != events.end() && snapshot->frame_time_us == 10000
                        && snapshot->source_update_id == 1 && snapshot->request_id != 0,
                    "real submitted snapshot lost its source frame, time or request identity");
            require(((snapshot->flags & diagnostics::BoxValid) != 0) == !fail_search,
                    "real submitted tracker_valid confused a held display box with a source observation");
            require(snapshot->values[0] == 1 && snapshot->values[1] != 0,
                    "source snapshot lost original pixel availability or generation");
            require((snapshot->flags & diagnostics::OtherBoxValid) != 0
                && snapshot->other_box[2] > 0 && snapshot->other_box[3] > 0,
                "source snapshot lost original filter prediction");
            require(snapshot->reason == (fail_search ? diagnostics::Reason::ContextUnavailable
                                                    : diagnostics::Reason::None),
                    "real submitted snapshot availability disagrees with NanoTrack success");
            if (fail_search) {
                require(!current_output.tracking, "failed source snapshot gained false confirmation from a newer update");
            }
            tracker->finishRound();
        }
    }
}

// Release the worker before tracker destruction, including failed assertions.
struct WorkerGate {
    std::shared_ptr<Trace> trace;
    explicit WorkerGate(std::shared_ptr<Trace> value) : trace{std::move(value)} {
        std::lock_guard<std::mutex> lock{trace->mutex};
        trace->block = true;
        trace->release = false;
    }
    ~WorkerGate() { release(); }
    void release() {
        {
            std::lock_guard<std::mutex> lock{trace->mutex};
            trace->release = true;
        }
        trace->cv.notify_all();
    }
};
std::vector<diagnostics::Event> eventsOf(const Recording &recording, diagnostics::Type type,
                                      std::uint64_t update) {
    auto events{captured(*recording.region)};
    events.erase(std::remove_if(events.begin(), events.end(), [=](const auto &event) {
        return event.type != type || event.update_id != update;
    }), events.end());
    return events;
}
void testDetectingSnapshotOrder() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        for (bool positive : {false, true}) {
            Recording recording{mode};
            auto trace{std::make_shared<Trace>()};
            trace->target_score = positive ? 0.9f : 0.0f;
            auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
            require(tracker->init(), "Detecting snapshot init");
            tracker->update(context(1));
            waitIdle(*tracker);
            WorkerGate gate{trace};
            const auto output{tracker->update(context(2))};
            require(output.consumed && !output.tracking && output.state == TrackerState::Detecting
                && trace->templates == (positive ? 1 : 0),
                    "saved Detecting result was lost or had the wrong sign");
            // Each negative request may traverse several stages; wait for B's frame session.
            const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
            while (trace->begins < 2 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            require(trace->begins >= 2, "B was not submitted before processing the saved result");
            if (mode != diagnostics::Mode::Off) {
                const auto reads{eventsOf(recording, diagnostics::Type::ResultRead, 2)};
                const auto requests{eventsOf(recording, diagnostics::Type::RequestDecision, 2)};
                require(reads.size() == 1 && requests.size() == 1
                            && reads[0].mono_ns <= requests[0].mono_ns
                            && (requests[0].flags & diagnostics::Complete)
                            && reads[0].request_id != requests[0].request_id
                            && reads[0].source_tracker_frame_id == 1
                            && reads[0].frame.capture_id == 1 && reads[0].frame_time_us == 10000
                            && requests[0].source_tracker_frame_id == 2 && requests[0].frame.capture_id == 2,
                        "Detecting did not read A once before admitting B with separate provenance");
                if (positive) {
                    const auto lookup{eventsOf(recording, diagnostics::Type::HistoryLookup, 2)};
                    require(lookup.size() == 1 && lookup[0].request_id == reads[0].request_id
                                && requests[0].mono_ns <= lookup[0].mono_ns,
                            "Detecting recovered A before submitting B or used B's identity");
                }
            }
            gate.release();
            waitIdle(*tracker);
            tracker->finishRound();
        }
        Recording recording{mode};
        auto trace{std::make_shared<Trace>()};
        auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
        require(tracker->init(), "empty snapshot init");
        WorkerGate gate{trace};
        tracker->update(context(1));
        waitModel(trace, 1);
        const auto output{tracker->update(context(2))};
        require(output.consumed && output.state == TrackerState::Detecting && trace->templates == 0,
                "empty snapshot waited for or invented an inference result");
        if (mode != diagnostics::Mode::Off) {
            require(eventsOf(recording, diagnostics::Type::ResultRead, 2).empty(),
                    "empty snapshot invented a read");
            const auto requests{eventsOf(recording, diagnostics::Type::RequestDecision, 2)};
            require(requests.size() == 1 && (requests[0].flags & diagnostics::Complete),
                    "empty snapshot did not keep the pending submission opportunity");
        }
        gate.release();
        tracker->finishRound();
    }
}

struct SnapshotHook {
    explicit SnapshotHook(std::function<void()> callback) {
        HybridTrackerDiagnosticTestAccess::snapshot_hook = std::move(callback);
    }
    ~SnapshotHook() { HybridTrackerDiagnosticTestAccess::snapshot_hook = {}; }
};
void testTrackingSnapshotOrderAndEarlyReturns() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        for (bool submit : {false, true}) {
            Recording recording{mode};
            auto trace{std::make_shared<Trace>()};
            auto config{settings()};
            config.yolo_interval = submit ? 1 : 2;
            auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
            require(tracker->init(), "Tracking snapshot init");
            HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame, 3, 8);
            YoloResult saved{};
            saved.frame_id = 700; saved.request_id = 9001; saved.source_update_id = 21;
            saved.source.capture_time_us = 1234;
            HybridTrackerDiagnosticTestAccess::result(*tracker, saved);
            int snapshots{};
            SnapshotHook hook{[&] {
                ++snapshots;
                require(trace->searches == 1, "Tracking snapshot preceded current NanoTrack observation");
            }};
            const auto output{tracker->update(context(1))};
            require(snapshots == 1 && output.consumed && output.tracking,
                    "Tracking skipped its one snapshot on a submission or interval frame");
            require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 3
                        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[1] == 0,
                    "missing source context changed permission counter or failed to reset watchdog");
            waitIdle(*tracker);
            require((trace->begins > 0) == submit, "Tracking changed submission cadence");
            if (mode != diagnostics::Mode::Off) {
                const auto reads{eventsOf(recording, diagnostics::Type::ResultRead, 1)};
                const auto decisions{eventsOf(recording, submit ? diagnostics::Type::RequestDecision
                                                               : diagnostics::Type::RequestSkipped, 1)};
                require(reads.size() == 1 && reads[0].request_id == 9001 && reads[0].frame_time_us == 1234
                            && reads[0].source_update_id == 21 && decisions.size() == 1
                            && reads[0].mono_ns <= decisions[0].mono_ns,
                        "Tracking did not preserve saved identity/read-before-request on both cadence paths");
            }
            tracker->finishRound();
        }
        Recording recording{mode};
        auto trace{std::make_shared<Trace>()};
        auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
        require(tracker->init(), "early-return snapshot init");
        HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame);
        tracker->update(context(1));
        YoloResult saved{}; saved.frame_id = 1; saved.request_id = 9002;
        HybridTrackerDiagnosticTestAccess::result(*tracker, saved);
        int snapshots{};
        SnapshotHook hook{[&] { ++snapshots; }};
        require(!tracker->update(context(1)).consumed, "repeated timestamp was accepted");
        require(!tracker->update({}).consumed, "invalid frame was accepted");
        HybridTrackerDiagnosticTestAccess::jump(*tracker, 0, -1, -1);
        // One jump reaches the original persistent-jump early return.
        for (unsigned frame{2}; frame <= static_cast<unsigned>(settings().jump_tolerance + 1); ++frame) {
            tracker->update(context(frame));
        }
        require(snapshots == settings().jump_tolerance,
                "invalid/repeated/persistent-jump early returns consumed an extra snapshot");
        tracker->finishRound();
    }
}
void testCompletionAfterSnapshot() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        for (bool save_positive : {false, true}) {
            Recording recording{mode};
            auto trace{std::make_shared<Trace>()};
            auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
            require(tracker->init(), "after-snapshot init");
            WorkerGate gate{trace};
            tracker->update(context(1));
            waitModel(trace, 1);
            if (save_positive) {
                YoloResult saved{};
                saved.frame_id = 1; saved.request_id = 9003; saved.source_update_id = 1;
                saved.has_target = true; saved.best.box = {40, 30, 20, 20};
                saved.source.capture_time_us = 10000; saved.source.width = 100; saved.source.height = 80;
                HybridTrackerDiagnosticTestAccess::result(*tracker, saved);
            }
            int snapshots{};
            {
                SnapshotHook hook{[&] {
                    ++snapshots;
                    gate.release();
                    waitIdle(*tracker);
                }};
                const auto output{tracker->update(context(2))};
                require(snapshots == 1 && trace->begins == 1,
                        "completion in the read/request gap was cleared, retried or admitted over unread output");
                require(!output.tracking && output.state == TrackerState::Detecting
                            && trace->templates == (save_positive ? 1 : 0),
                        "late output replaced empty/A snapshot or rejected request discarded saved A");
            }
            if (mode != diagnostics::Mode::Off) {
                const auto reads{eventsOf(recording, diagnostics::Type::ResultRead, 2)};
                const auto requests{eventsOf(recording, diagnostics::Type::RequestDecision, 2)};
                require(reads.size() == (save_positive ? 1u : 0u)
                            && (!save_positive || reads[0].request_id == 9003)
                            && requests.size() == 1 && requests[0].reason == diagnostics::Reason::UnreadResult
                            && !(requests[0].flags & diagnostics::Complete),
                        "after-snapshot completion lost its unread rejection or was read a second time");
            }
            const auto template_count{trace->templates.load()};
            tracker->update(context(3));
            waitIdle(*tracker);
            if (mode != diagnostics::Mode::Off) {
                const auto reads{eventsOf(recording, diagnostics::Type::ResultRead, 3)};
                require(reads.size() == 1 && reads[0].request_id != 9003
                            && reads[0].source_tracker_frame_id == 1 && reads[0].frame_time_us == 10000
                            && reads[0].box[2] > 0 && (reads[0].flags & diagnostics::HasTarget),
                        "later update lost or modified the real worker completion");
            }
            require(!save_positive || trace->templates == template_count,
                    "saved positive A was initialized twice");
            tracker->finishRound();
        }
    }
}
void testRequestOverlapsRecoveryAndRoundCleanup() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        Recording recording{mode};
        auto trace{std::make_shared<Trace>()};
        auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
        require(tracker->init(), "recovery overlap init");
        auto first{context(1)};
        std::weak_ptr<const Frame> old_frame{first.frame};
        tracker->update(first);
        first.frame.reset();
        waitIdle(*tracker);
        {
            std::lock_guard<std::mutex> lock{trace->mutex};
            trace->block_template = true;
        }
        auto update{std::async(std::launch::async, [&] { return tracker->update(context(2)); })};
        // This gate has its own timeout, so failed assertions cannot strand the update's future.
        {
            std::unique_lock<std::mutex> lock{trace->mutex};
            require(trace->cv.wait_for(lock, std::chrono::seconds{2}, [&] { return trace->template_entered; }),
                    "saved-result initialization did not enter its bounded gate");
        }
        waitIdle(*tracker);
        require(trace->begins == 2 && update.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready,
                "current request did not finish inference while saved-result recovery was blocked");
        {
            std::lock_guard<std::mutex> lock{trace->mutex};
            trace->release_template = true;
        }
        trace->cv.notify_all();
        require(update.wait_for(std::chrono::seconds{2}) == std::future_status::ready,
                "recovery did not finish after release");
        require(update.get().state == TrackerState::Detecting, "provisional initialization authorized output");
        if (mode != diagnostics::Mode::Off) {
            const auto requests{eventsOf(recording, diagnostics::Type::RequestDecision, 2)};
            const auto lookups{eventsOf(recording, diagnostics::Type::HistoryLookup, 2)};
            const auto reads{eventsOf(recording, diagnostics::Type::ResultRead, 2)};
            require(requests.size() == 1 && lookups.size() == 1 && reads.size() == 1
                        && requests[0].mono_ns <= lookups[0].mono_ns
                        && lookups[0].request_id == reads[0].request_id,
                    "recovery history lookup preceded the current request or used its identity");
            const auto controls{eventsOf(recording, diagnostics::Type::DetectorControl, 2)};
            const auto starts{eventsOf(recording, diagnostics::Type::InitializeStart, 2)};
            require(starts.size() == 1 && std::any_of(controls.begin(), controls.end(), [&](const auto &event) {
                return event.values[3] != 0 && event.counts[0] == static_cast<std::uint32_t>(tracking::Phase::PreparingProvisional)
                    && event.mono_ns <= starts[0].mono_ns;
            }), "SeedAccepted feedback waited for expensive preparation");
        }
        // D2 completed during blocked D1 initialization; its original preinitialization snapshot is unchanged.
        const auto verified{tracker->update(context(3))};
        require(verified.tracking && verified.state == TrackerState::TargetFound,
            "D2 completed before initialization could not verify independent replay T2");
        tracker->finishRound();
        tracker->reset();
        require(old_frame.expired(), "OFF/reset retained an old-round history image");
        const auto output{tracker->update(context(1))};
        require(output.state == TrackerState::Detecting && trace->templates == 1,
                "new round reused the old unread result when frame numbering restarted");
        tracker->finishRound();
        tracker.reset();
        require(trace->unsafe_destroy == 0 && trace->active == 0,
                "round cleanup destroyed model resources while inference was active");
        for (const auto &count : trace->destroyed) {
            require(count == 1, "round cleanup leaked a model/preprocessor resource");
        }
    }
}

void testOffWithExecutingCurrentRequest() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        Recording recording{mode};
        auto trace{std::make_shared<Trace>()};
        auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
        require(tracker->init(), "OFF executing-request init");
        tracker->update(context(1));
        waitIdle(*tracker);
        WorkerGate gate{trace};
        tracker->update(context(2));
        waitModel(trace, 2);
        auto off{std::async(std::launch::async, [&] { tracker->finishRound(); })};
        require(off.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready,
                "OFF returned while the current request still owned model inputs");
        gate.release();
        require(off.wait_for(std::chrono::seconds{2}) == std::future_status::ready,
                "OFF did not join the current inference before its deadline");
        off.get();
        tracker->reset();
        const auto output{tracker->update(context(1))};
        require(output.state == TrackerState::Detecting && trace->templates == 1,
                "executing OFF request initialized the next round");
        tracker->finishRound();
    }
}

void testUnreadResultAdmission() {
    for (auto mode : {diagnostics::Mode::Off, diagnostics::Mode::Basic, diagnostics::Mode::Detail}) {
        Recording recording{mode};
        auto trace{std::make_shared<Trace>()};
        auto value{components(trace)};
        YoloV8Detector detector{std::move(value.yolo), std::move(value.yolo_preprocessor), 0.5f, 0.4f, 0, 1.0f};
        require(detector.init(), "unread-result detector init");
        SourceComparison source{};
        source.generation = 17;
        source.frame_id = 101;
        source.update_id = 23;
        source.capture_time_us = 10000;
        source.width = 100;
        source.height = 80;
        source.tracker_box = {10, 20, 30, 40};
        source.prediction_box = {11, 21, 31, 41};
        source.tracker_valid = true;
        source.prediction_valid = false;
        require(detector.request(context(1).frame, 101, {}, source), "unread-result first request rejected");
        const auto wait_unread{[&](YoloResult &result) {
            const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
            while (!HybridTrackerDiagnosticTestAccess::unreadResult(detector, result)
                   && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            require(HybridTrackerDiagnosticTestAccess::unreadResult(detector, result),
                    "detector did not complete an unread result before the deadline");
        }};
        YoloResult before{};
        wait_unread(before);
        require(before.frame_id == 101 && before.has_target && before.request_id != 0 && !detector.isBusy(),
                "unread-result prerequisite was not an idle detector with a completed positive result");
        require(!detector.request(context(2).frame, 202), "new request overwrote an unread completed result");
        YoloResult after{};
        require(detector.get_result(after), "rejected submission made the original result unreadable");
        require(after.frame_id == before.frame_id && after.request_id == before.request_id
                    && after.source_update_id == before.source_update_id
                    && after.source_identity.capture_id == before.source_identity.capture_id
                    && after.source_identity.round_id == before.source_identity.round_id
                    && after.source_identity.driver_sequence == before.source_identity.driver_sequence
                    && after.source_identity.flags == before.source_identity.flags
                    && after.has_target == before.has_target && after.best.box == before.best.box
                    && after.best.score == before.best.score && after.best.classId == before.best.classId
                    && after.reason == before.reason,
                "rejected submission modified the original completed result");
        require(after.source.generation == source.generation && after.source.frame_id == source.frame_id
                    && after.source.update_id == source.update_id
                    && after.source.capture_time_us == source.capture_time_us
                    && after.source.width == source.width && after.source.height == source.height
                    && after.source.tracker_box == source.tracker_box
                    && after.source.prediction_box == source.prediction_box
                    && after.source.tracker_valid == source.tracker_valid
                    && after.source.prediction_valid == source.prediction_valid,
                "rejected submission modified the original source metadata");
        require(trace->calls == 1 && !detector.get_result(after),
                "rejected request executed inference or original result was consumed twice");
        if (mode != diagnostics::Mode::Off) {
            const auto events{captured(*recording.region)};
            require(std::any_of(events.begin(), events.end(), [](const auto &event) {
                return event.type == diagnostics::Type::RequestDecision
                    && event.source_tracker_frame_id == 202 && event.reason == diagnostics::Reason::UnreadResult
                    && event.values[2] == 1 && !(event.flags & diagnostics::Complete);
            }), "unread-result rejection reason was not recorded");
        }
        DetectorControl rejected{}; rejected.permission.round = source.generation;
        rejected.rejected_job = before.job_id;
        detector.control(rejected);
        auto next_source{source}; next_source.frame_id = 202; next_source.capture_time_us = 20000;
        next_source.evidence = context(2).evidence;
        require(detector.request(context(2).frame, 202, {}, next_source),
            "result consumption did not restore request admission");
        wait_unread(after);
        require(detector.get_result(after) && after.frame_id == 202 && after.request_id != before.request_id,
                "admission did not resume with the new request identity after consumption");
        detector.stop();
        require(trace->calls == 2 && trace->observed_source_times == std::vector<std::uint64_t>{10000, 20000},
                "rejected request executed or post-consumption request used the wrong image");
    }
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
void testCandidateStageProvenance() {
    auto trace{std::make_shared<Trace>()}; trace->second_target = true;
    auto parts{components(trace)};
    YoloV8Detector detector{std::move(parts.yolo), std::move(parts.yolo_preprocessor), 0.5f, 0.4f, 0, 1.0f};
    require(detector.init(), "candidate detector init");
    SourceComparison source{}; source.width = 100; source.height = 80;
    require(detector.request(context(1).frame, 1, {10,10,50,50}, source), "candidate ROI request");
    YoloResult result{};
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
    bool ready{};
    while (!(ready = detector.get_result(result)) && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
    detector.stop();
    require(ready && result.has_target && result.candidates.size() == 2 && result.best.score == 0.94f,
        "first-stage survivors or highest-score seed were discarded");
    require(result.stage_kind == 0 && result.stage_index == 0 && result.searched_region == cv::Rect{10,10,50,50}
        && trace->calls == 1, "candidate provenance claimed unsearched stages or wrong region");
}

void testFreshVerificationProtocol() {
    for (const bool stale_seed : {false, true}) {
        Recording recording{diagnostics::Mode::Basic};
        auto trace{std::make_shared<Trace>()}; auto config{settings()}; config.iou_reset_threshold = 0.1f;
        auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
        require(tracker->init(), "fresh verification init");
        WorkerGate gate{trace};
        const auto f1{context(1)}; tracker->update(f1); waitModel(trace, 1);
        const auto round{tracker->validity()->snapshot().round};
        const auto detection{[&](unsigned frame, std::int64_t anchor) {
            YoloResult result{}; result.frame_id = frame; result.source.frame_id = frame;
            result.source.generation = round; result.source.update_id = frame;
            result.source.capture_time_us = frame * 10000; result.source.width = 100; result.source.height = 80;
            result.source.evidence = {tracking::ClockMapping::StandaloneAdmissionV1, anchor, true};
            result.has_target = true; result.best = {{40, 30, 20, 20}, 0.88f, 0};
            result.candidates = {result.best}; return result;
        }};
        auto d1{detection(1, f1.evidence.anchor_ns)};
        HybridTrackerDiagnosticTestAccess::result(*tracker, d1);
        const unsigned seed_update{stale_seed ? 20U : 2U};
        const auto provisional{tracker->update(context(seed_update))};
        require(!provisional.tracking && provisional.state == TrackerState::Detecting
            && trace->templates == 1, "seed did not initialize immediately as provisional");
        const auto identity{tracker->validity()->snapshot()};
        require(identity.phase == tracking::Phase::VerifyingProvisional
            && identity.attempt_deadline_ns == decision_ns.load() + 1000000000,
            "seed acceptance timeout moved or initialization authorized output");
        auto d2{detection(2, decision_ns.load())};
        d2.source.capture_time_us = seed_update * 10000;
        // Request metadata predates initialization. The independent replay measurement must win over best score.
        d2.source.attempt = 0;
        d2.best = {{80, 60, 10, 10}, 0.94f, 0};
        d2.candidates.push_back(d2.best);
        HybridTrackerDiagnosticTestAccess::result(*tracker, d2);
        const auto verified{tracker->update(context(seed_update + 1))};
        require(verified.tracking && verified.state == TrackerState::TargetFound && verified.reset_los,
            "fresh preinitialization D2 did not verify same-image replay evidence");
        require(verified.permission.deadline_ns == d2.source.evidence.anchor_ns + 100000000,
            "confirmation lifetime renewed at arrival instead of original anchor");
        auto stale{detection(seed_update + 1, decision_ns.load() - 200000000)};
        stale.source.attempt = verified.permission.attempt; stale.source.tracker_valid = true;
        stale.source.tracker_box = {40, 30, 20, 20}; stale.has_target = false; stale.candidates.clear();
        HybridTrackerDiagnosticTestAccess::result(*tracker, stale);
        require(tracker->update(context(seed_update + 2)).tracking
            && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0, "stale negative changed current trust");
        auto ambiguous{detection(seed_update + 2, decision_ns.load())};
        ambiguous.frame_id = 4; ambiguous.source.frame_id = 4;
        ambiguous.source.attempt = verified.permission.attempt; ambiguous.source.tracker_valid = true;
        ambiguous.source.tracker_box = {40, 30, 20, 20};
        ambiguous.complete_coverage = false;
        ambiguous.candidates = {{{40,30,10,20},0.9f,0}, {{50,30,10,20},0.85f,0}};
        HybridTrackerDiagnosticTestAccess::result(*tracker, ambiguous);
        const auto rejected{tracker->update(context(seed_update + 3))};
        require(!rejected.tracking && tracker->validity()->pending(), "first ambiguity did not revoke trust");
        const auto replacement{tracker->validity()->snapshot()};
        require(replacement.attempt > verified.permission.attempt
            && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
            "adverse positive did not start independent zero-count recovery attempt");
        // Fresh negatives count only against B. A stale or old-A result cannot spend B's budget.
        for (unsigned count{1}; count <= 5; ++count) {
            auto negative{detection(900 + count, decision_ns.load())};
            negative.source.attempt = replacement.attempt; negative.source.tracker_valid = true;
            negative.source.capture_time_us = (seed_update + 2 + count) * 10000;
            negative.source.tracker_box = {40,30,20,20}; negative.has_target = false; negative.candidates.clear();
            HybridTrackerDiagnosticTestAccess::result(*tracker, negative);
            const auto output{tracker->update(context(seed_update + 3 + count))};
            require(!output.tracking, "provisional negative unexpectedly authorized output");
            require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == static_cast<int>(count),
                "per-instance adverse-result budget changed");
        }
        require(tracker->validity()->snapshot().phase == tracking::Phase::Searching,
            "fifth provisional adverse result did not abandon attempt");
        gate.release(); tracker->finishRound();
    }
}

void testVerifiedIntervalOneAndFour() {
    for (const int interval : {1, 4}) {
        Recording recording{diagnostics::Mode::Basic};
        auto trace{std::make_shared<Trace>()}; auto config{settings()}; config.yolo_interval = interval;
        auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
        require(tracker->init(), "configured cadence init");
        HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame);
        WorkerGate gate{trace};
        for (unsigned image{1}; image <= 8; ++image) {
            require(tracker->update(context(image)).tracking, "cadence changed verified permission");
            if (image == static_cast<unsigned>(interval)) { waitModel(trace, 1); }
        }
        const auto events{captured(*recording.region)};
        std::vector<std::uint64_t> offered{};
        for (const auto &event : events) {
            if (event.type == diagnostics::Type::RequestDecision) { offered.push_back(event.source_tracker_frame_id); }
        }
        const std::vector<std::uint64_t> expected{interval == 1
            ? std::vector<std::uint64_t>{1, 2, 3, 4, 5, 6, 7, 8} : std::vector<std::uint64_t>{4, 8}};
        require(offered == expected, "configured interval changed submission opportunities");
        gate.release(); tracker->finishRound();
    }
}

void testRegionalPlanVerdicts() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()}; auto config{settings()}; config.yolo_interval = 1;
    config.confirmation_timeout_ms = 1000; // Isolate plan verdicts from separately tested expiry boundaries.
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "regional verdict init");
    HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame);
    const auto identity{tracker->validity()->snapshot()};
    WorkerGate gate{trace}; tracker->update(context(2)); waitModel(trace, 1);
    std::uint64_t request{9000}; unsigned image{2};
    const auto regional{[&](std::uint64_t frame, std::uint64_t plan, bool complete, bool comparable, bool match,
                           diagnostics::Reason cancellation = diagnostics::Reason::None, bool ambiguous = false) {
        YoloResult result{}; result.request_id = ++request; result.frame_id = frame; result.plan_id = plan;
        result.complete_coverage = complete; result.source.frame_id = frame;
        result.plan_cancellation_reason = cancellation;
        result.source.generation = identity.round; result.source.attempt = identity.attempt;
        result.source.capture_time_us = 20000; result.source.width = 100; result.source.height = 80;
        result.source.evidence = {tracking::ClockMapping::StandaloneAdmissionV1, 1020000000, true};
        result.source.tracker_valid = comparable; result.source.tracker_box = {40, 30, 20, 20};
        result.has_target = true; result.best = {{0, 0, 10, 10}, 0.94f, 0};
        result.candidates = {result.best};
        if (match) { result.candidates.push_back({{40, 30, ambiguous ? 10.0f : 20.0f, 20}, 0.88f, 0}); }
        if (ambiguous) { result.candidates.push_back({{50, 30, 10, 20}, 0.87f, 0}); }
        HybridTrackerDiagnosticTestAccess::result(*tracker, result);
        return tracker->update(context(++image));
    }};
    require(regional(700, 77, false, true, false).tracking
        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
        "unrelated positive partial region revoked permission");
    require(regional(700, 77, false, true, true).tracking,
        "later unique match lost to unrelated higher score");
    require(regional(699, 76, true, true, false).tracking
        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
        "superseded old complete plan overrode newer agreement");
    require(regional(701, 78, false, true, false).tracking
        && regional(702, 78, true, true, false).tracking,
        "coverage on different source images was combined");
    require(regional(703, 79, false, false, false).tracking
        && regional(703, 79, true, true, false).tracking,
        "missing comparison stage became a completed adverse verdict");
    require(regional(704, 80, true, true, false, diagnostics::Reason::HistoryMiss).tracking
        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
        "cancelled plan's retained fresh comparison revoked permission or spent its budget");
    require(regional(705, 81, false, true, true, diagnostics::Reason::HistoryMiss, true).tracking
        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
        "cancelled regional ambiguity revoked permission or spent its budget");
    require(regional(706, 82, false, true, true, diagnostics::Reason::HistoryMiss).tracking
        && tracker->validity()->snapshot().confirmation_frame == 706,
        "cancelled plan erased separately eligible unique positive confirmation");
    require(!regional(707, 83, true, true, false).tracking
        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 1
        && tracker->validity()->pending(),
        "first applicable complete disagreement failed to revoke exactly once");
    regional(707, 83, true, true, false);
    require(HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 1,
        "one completed plan spent the adverse budget twice");
    gate.release(); tracker->finishRound();
    const auto events{captured(*recording.region)};
    require(std::none_of(events.begin(), events.end(), [](const auto &event) {
        return event.type == diagnostics::Type::PlanOutcome && event.values[0] >= 80 && event.values[0] <= 82;
    }), "cancelled plan received a second parent verdict from separately eligible stage evidence");
}

void testSeparateSeedRejectionGuards() {
    for (unsigned rejection{0}; rejection < 6; ++rejection) {
        auto trace{std::make_shared<Trace>()}; auto config{settings()};
        config.frame_buffer_size = rejection == 3 ? 1 : 30;
        auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
        require(tracker->init(), "seed rejection init");
        WorkerGate gate{trace}; const auto first{context(1)};
        tracker->update(first); waitModel(trace, 1); tracker->update(context(2));
        const auto round{tracker->validity()->snapshot().round};
        YoloResult seed{}; seed.frame_id = 1; seed.request_id = 10000;
        seed.source.frame_id = 1; seed.source.generation = round;
        seed.source.capture_time_us = 10000; seed.source.width = 100; seed.source.height = 80;
        seed.source.evidence = first.evidence; seed.source.attempt = 77;
        seed.has_target = true; seed.best = {{40, 30, 20, 20}, .9f, 0}; seed.candidates = {seed.best};
        if (rejection == 0) { seed.source.generation = round + 1; }
        if (rejection == 1) { seed.source.evidence.anchor_ns = 2000000000; }
        if (rejection == 2) { seed.source.frame_id = 99; }
        if (rejection == 4) { seed.best.box.width = 0; }
        if (rejection == 5) { seed.source.capture_time_us = 20000; }
        HybridTrackerDiagnosticTestAccess::result(*tracker, seed);
        require(!tracker->update(context(3)).tracking && trace->templates == 0
            && tracker->validity()->snapshot().phase == tracking::Phase::Searching,
            "wrong-round/future-clock/identity/evicted-pixel/geometry seed was accepted");
        if (rejection == 4) {
            seed.best.box.width = 20;
            HybridTrackerDiagnosticTestAccess::result(*tracker, seed);
            tracker->update(context(4));
            require(trace->templates == 0 && tracker->validity()->snapshot().phase == tracking::Phase::Searching,
                "disposed rejected job was reused as a seed");
        }
        gate.release(); tracker->finishRound();
    }
}

void testExpiredAttemptSeedAndReplay() {
    Recording recording{diagnostics::Mode::Basic};
    auto trace{std::make_shared<Trace>()}; auto config{settings()};
    config.frame_buffer_size = 60; config.iou_reset_threshold = 0.1f;
    auto tracker{HybridTracker::CreateInstance(config, components(trace), launcher(trace))};
    require(tracker->init(), "expired-attempt seed init");
    HybridTrackerDiagnosticTestAccess::prime(*tracker, context(1).frame);
    const auto a{tracker->validity()->snapshot()};
    WorkerGate gate{trace};
    for (unsigned image{2}; image <= 41; ++image) {
        if (image == 35) {
            require(tracker->validity()->revoke(tracking::LossReason::Expired, decision_ns.load(), 350000),
                "A did not expire");
        }
        tracker->update(context(image));
    }
    YoloResult d32{}; d32.request_id = 10000; d32.frame_id = 31; // Tracker's frame 1 was source image 2.
    d32.source.frame_id = d32.frame_id; d32.source.generation = a.round;
    d32.source.capture_time_us = 320000; d32.source.update_id = 31;
    d32.source.attempt = a.attempt; d32.source.permission_revision = a.revision;
    d32.source.width = 100; d32.source.height = 80;
    d32.source.evidence = {tracking::ClockMapping::StandaloneAdmissionV1, 1320000000, true};
    d32.source.tracker_valid = true; d32.source.tracker_box = {0, 0, 10, 10};
    d32.has_target = true; d32.best = {{40, 30, 20, 20}, 0.9f, 0}; d32.candidates = {d32.best};
    const auto provenance{d32.source};
    HybridTrackerDiagnosticTestAccess::result(*tracker, d32);
    const auto seeded{tracker->update(context(42))};
    const auto b{tracker->validity()->snapshot()};
    require(!seeded.tracking && b.attempt > a.attempt && b.phase == tracking::Phase::VerifyingProvisional
        && b.attempt_deadline_ns == 2420000000 && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
        "expired A's stale positive did not seed a distinct zero-count B at frame 42");
    require(d32.source.attempt == provenance.attempt && d32.source.permission_revision == provenance.permission_revision
        && d32.source.evidence.anchor_ns == provenance.evidence.anchor_ns,
        "new seed rewrote original request provenance");
    auto late{d32}; late.request_id = 10001; late.frame_id = 30; late.source.frame_id = 30;
    late.source.capture_time_us = 310000;
    HybridTrackerDiagnosticTestAccess::result(*tracker, late);
    tracker->update(context(43));
    require(tracker->validity()->snapshot().attempt == b.attempt
        && HybridTrackerDiagnosticTestAccess::counters(*tracker)[0] == 0,
        "old A positive replaced active B or spent its budget");
    auto d41{d32}; d41.request_id = 10002; d41.frame_id = 40; d41.source.frame_id = 40;
    d41.source.capture_time_us = 410000;
    d41.source.evidence.anchor_ns = 1410000000;
    HybridTrackerDiagnosticTestAccess::result(*tracker, d41);
    const auto verified{tracker->update(context(44))};
    require(verified.tracking && verified.permission.attempt == b.attempt && verified.reset_los,
        "fresh D41 could not verify B's independent replay T41 using immutable old-A request metadata");
    require(verified.permission.deadline_ns == 1510000000,
        "D41 verification renewed the old source anchor");
    HybridTrackerDiagnosticTestAccess::result(*tracker, d32);
    tracker->update(context(45));
    require(tracker->validity()->snapshot().attempt == b.attempt,
        "duplicate old seed changed B");
    gate.release(); tracker->finishRound();
}

void testRepeatedSearchingControlsPreserveCoverage() {
    auto trace{std::make_shared<Trace>()}; trace->target_score = 0;
    YoloV8Detector detector{std::make_unique<Yolo>(trace), std::make_unique<YoloPre>(trace),
        .5f, .45f, 0, 1.0f};
    require(detector.init(), "repeated Searching control init");
    DetectorControl control{}; control.permission.round = 4; control.permission.attempt = 7;
    control.permission.revision = 2; control.permission.phase = tracking::Phase::Searching;
    detector.control(control);
    SourceComparison source{}; source.generation = 4; source.frame_id = 1;
    source.capture_time_us = 10000; source.width = 100; source.height = 80;
    source.evidence = context(1).evidence;
    require(detector.request(context(1).frame, 1, {}, source), "Searching source admission");
    YoloResult result{}; std::uint64_t plan{};
    for (unsigned stage{0}; stage < 7; ++stage) {
        bool ready{}; const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        while (!(ready = detector.get_result(result)) && std::chrono::steady_clock::now() < end) {
            std::this_thread::yield();
        }
        require(ready && result.frame_id == 1 && (stage == 0 || result.plan_id == plan),
            "unchanged Searching control restarted or cancelled fixed-image coverage");
        plan = result.plan_id;
        detector.control(control);
    }
    require(result.complete_coverage && trace->calls == 7,
        "repeated Searching feedback prevented terminal discovery coverage");
    detector.stop();
}

void testExpiredTargetedPruningAndOldControl() {
    auto trace{std::make_shared<Trace>()}; trace->target_score = 0; trace->block = true;
    YoloV8Detector detector{std::make_unique<Yolo>(trace), std::make_unique<YoloPre>(trace),
        0.5f, 0.45f, 0, 1.0f};
    require(detector.init(), "expired targeted init");
    DetectorControl control{}; control.permission.round = 4; control.permission.attempt = 7;
    control.permission.revision = 1; control.permission.phase = tracking::Phase::VerifyingProvisional;
    control.permission.attempt_deadline_ns = 10000000000;
    control.clock = [] { return decision_ns.load(); }; detector.control(control);
    const auto submit{[&](unsigned id, bool targeted) {
        const auto input{context(id)};
        SourceComparison source{}; source.generation = 4; source.frame_id = id;
        source.width = 100; source.height = 80; source.capture_time_us = input.frame->timestampUs();
        source.evidence = input.evidence; source.attempt = 7;
        require(detector.request(input.frame, id, targeted ? cv::Rect{10, 10, 50, 50} : cv::Rect{}, source),
            "expired targeted submission");
    }};
    const auto read{[&] {
        YoloResult result{}; bool ready{};
        const auto end{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        while (!(ready = detector.get_result(result)) && std::chrono::steady_clock::now() < end) {
            std::this_thread::yield();
        }
        require(ready, "old control cancelled current discovery or result timed out"); return result;
    }};
    submit(10, false); waitModel(trace, 1); submit(11, true);
    decision_ns += 200000000;
    { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = true; }
    trace->cv.notify_all();
    const auto first{read()};
    auto obsolete{control}; obsolete.permission.round = 3; obsolete.permission.revision = 999;
    obsolete.rejected_job = first.job_id + 1; detector.control(obsolete);
    const auto second{read()}; const auto third{read()};
    require(first.frame_id == 10 && second.frame_id == 10 && third.frame_id == 10
        && third.source.generation == 4 && !second.targeted
        && second.source.evidence.anchor_ns == first.source.evidence.anchor_ns,
        "stale targeted input ran, source anchor renewed, or old control changed current round");
    detector.stop();
    require(std::find(trace->observed_source_times.begin(), trace->observed_source_times.end(), 110000)
        == trace->observed_source_times.end(), "expired targeted check was dispatched");
}

void testOneCallSchedulingAndCapacity() {
    Recording recording{diagnostics::Mode::Detail};
    auto trace{std::make_shared<Trace>()}; trace->target_score = 0; trace->block = true;
    YoloV8Detector detector{std::make_unique<Yolo>(trace), std::make_unique<YoloPre>(trace),
        0.5f, 0.45f, 0, 1.0f};
    require(detector.init(), "one-call scheduler init");
    DetectorControl control{};
    control.permission.round = 1; control.permission.attempt = 7; control.permission.revision = 1;
    control.permission.phase = tracking::Phase::VerifyingProvisional;
    control.permission.attempt_deadline_ns = 10000000000;
    control.clock = [] { return decision_ns.load(); };
    detector.control(control);
    const auto submit{[&](unsigned id, bool targeted) {
        const auto input{context(id)};
        SourceComparison source{}; source.generation = 1; source.frame_id = id;
        source.capture_time_us = input.frame->timestampUs(); source.width = 100; source.height = 80;
        source.evidence = input.evidence; source.attempt = 7;
        require(detector.request(input.frame, id, targeted ? cv::Rect{10, 10, 50, 50} : cv::Rect{}, source),
            "scheduler admission unexpectedly failed");
    }};
    const auto release{[&] {
        { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = true; }
        trace->cv.notify_all();
    }};
    const auto read{[&] {
        YoloResult result{};
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        // First observe completion without releasing the next call's capacity.
        while (!HybridTrackerDiagnosticTestAccess::unreadResult(detector, result)
            && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
        require(result.job_id != 0, "one-call completion timeout");
        { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = false; }
        require(detector.get_result(result), "protected result disappeared");
        return result;
    }};
    submit(10, false); waitModel(trace, 1);
    submit(11, true); submit(12, true); release();
    const auto initial{read()};
    require(initial.frame_id == 10 && !initial.targeted && !initial.complete_coverage,
        "whole-image miss claimed complete tile coverage");
    std::vector<YoloResult> results{};
    for (unsigned index{0}; index < 20; ++index) {
        waitModel(trace, index + 2);
        submit(index + 13, true);
        release(); results.push_back(read());
    }
    require(results[0].targeted && results[0].frame_id == 12
        && results[1].targeted && !results[2].targeted
        && results[3].targeted && results[4].targeted && !results[5].targeted,
        "eligible service did not follow T/T/D or latest pending replacement");
    for (const auto &result : results) {
        if (!result.targeted) {
            require(result.frame_id == 10 && result.plan_id == initial.plan_id,
                "new images restarted discovery or mixed source coverage");
        }
    }
    require(std::any_of(results.begin(), results.end(), [](const auto &result) {
        return result.frame_id == 10 && result.complete_coverage;
    }), "constant pending replacements starved final discovery coverage");
    // A local result and a protected unread result coexist; a third call must wait.
    release();
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
    YoloResult unread{};
    while (!HybridTrackerDiagnosticTestAccess::unreadResult(detector, unread)
        && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
    require(unread.job_id > results.back().job_id, "next result did not overlap local arbitration");
    const auto calls{trace->calls.load()};
    require(!detector.request(context(50).frame, 50), "unread result admission protection changed");
    require(trace->calls == calls && unread.job_id != results.back().job_id,
        "full result mailbox dispatched extra device work");
    detector.finishRound(); detector.stop();
    require(trace->active == 0 && trace->unsafe_destroy == 0, "OFF released active device resources");
    require(!hasEvent(*recording.region, diagnostics::Type::ResultOverwrite, diagnostics::Reason::None),
        "one-call mailbox overwrote an unread result");
}

/** @brief Barrier-controlled calls with a protected unread result between releases. */
struct SchedulerProbe {
    std::shared_ptr<Trace> trace{std::make_shared<Trace>()};
    YoloV8Detector detector{std::make_unique<Yolo>(trace), std::make_unique<YoloPre>(trace),
        0.5f, 0.45f, 0, 1.0f};
    DetectorControl control{};
    int calls{};
    SchedulerProbe() {
        context(1);
        trace->target_score = 0; trace->block = true;
        require(detector.init(), "scheduler probe init");
        control.permission.round = 4; control.permission.attempt = 7;
        control.permission.revision = 1;
        control.permission.phase = tracking::Phase::VerifyingProvisional;
        control.permission.attempt_deadline_ns = 10000000000;
        control.clock = [] { return decision_ns.load(); };
        detector.control(control);
    }
    ~SchedulerProbe() {
        { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = true; }
        trace->cv.notify_all(); detector.stop();
    }
    void submit(unsigned image, const cv::Rect &roi = {}) {
        const auto input{context(image)};
        SourceComparison source{}; source.generation = 4; source.frame_id = image;
        source.capture_time_us = input.frame->timestampUs(); source.width = 100; source.height = 80;
        source.attempt = 7; source.permission_revision = 1; source.evidence = input.evidence;
        require(detector.request(input.frame, image, roi, source), "scheduler probe submission");
    }
    void entered() { waitModel(trace, ++calls); }
    YoloResult finish() {
        { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = true; }
        trace->cv.notify_all();
        YoloResult result{};
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
        while (!HybridTrackerDiagnosticTestAccess::unreadResult(detector, result)
            && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
        require(result.job_id != 0, "scheduler probe completion timeout");
        { std::lock_guard<std::mutex> lock{trace->mutex}; trace->release = false; }
        require(detector.get_result(result), "scheduler probe result disappeared");
        return result;
    }
};

void testCancelledLastStageIsNotCompleteEvidence() {
    SchedulerProbe probe{};
    probe.submit(10);
    std::uint64_t plan{};
    for (unsigned stage{0}; stage < 6; ++stage) {
        probe.entered();
        const auto result{probe.finish()};
        plan = result.plan_id;
        require(result.frame_id == 10 && !result.complete_coverage, "early discovery completion");
    }
    probe.entered(); // Last tile owns the device; cancel without interrupting its input storage.
    probe.control.oldest_frame = 11;
    probe.detector.control(probe.control);
    probe.submit(11);
    const auto cancelled{probe.finish()};
    require(cancelled.frame_id == 10 && cancelled.plan_id == plan
        && cancelled.source.attempt == 7 && !cancelled.complete_coverage
        && cancelled.plan_cancellation_reason == diagnostics::Reason::HistoryMiss,
        "cancelled last in-flight tile advertised completed adverse coverage");
}

void testCancellationAfterPublicationPreservesUnreadIdentity() {
    SchedulerProbe probe{};
    probe.trace->target_score = 0.9f;
    probe.submit(10, {10, 10, 50, 50}); probe.entered();
    { std::lock_guard<std::mutex> lock{probe.trace->mutex}; probe.trace->release = true; }
    probe.trace->cv.notify_all();
    YoloResult published{};
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{2}};
    while (!HybridTrackerDiagnosticTestAccess::unreadResult(probe.detector, published)
        && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
    require(published.job_id != 0 && published.has_target, "unread cancellation prerequisite");
    probe.control.oldest_frame = 11;
    probe.detector.control(probe.control);
    YoloResult taken{};
    require(probe.detector.get_result(taken) && taken.job_id == published.job_id
        && taken.source.attempt == published.source.attempt && taken.best.box == published.best.box
        && taken.has_target && taken.plan_cancellation_reason == diagnostics::Reason::HistoryMiss,
        "post-publication cancellation lost immutable positive evidence or its cancellation disposition");
}

void testFirstMissFallbackAndPartialFeedback() {
    {
        SchedulerProbe probe{};
        const cv::Rect roi{10, 10, 50, 50};
        probe.submit(10, roi); probe.entered(); probe.submit(11, roi);
        const auto initial{probe.finish()};
        require(initial.targeted && initial.plan_id == 0 && initial.fallback_plan_id != 0,
            "first targeted miss did not retain its one immutable fallback link");
        probe.entered(); probe.submit(12, roi);
        const auto later_miss{probe.finish()};
        require(later_miss.targeted && later_miss.frame_id == 11 && later_miss.fallback_plan_id == 0,
            "later-image miss appended another fallback plan");
        probe.entered();
        const auto broad{probe.finish()};
        require(!broad.targeted && broad.frame_id == 10 && broad.plan_id == initial.fallback_plan_id
            && broad.stage_kind == 1 && !broad.complete_coverage,
            "first-miss fallback changed source, initial-job link or coverage");
        bool completed{};
        for (unsigned call{0}; call < 7; ++call) {
            probe.entered();
            const auto result{probe.finish()};
            if (result.targeted) {
                require(result.frame_id == 12 && result.fallback_plan_id == 0,
                    "pending miss displaced the active fallback");
            } else {
                require(result.frame_id == 10 && result.plan_id == initial.fallback_plan_id,
                    "fallback mixed later-image coverage");
                completed = completed || result.complete_coverage;
            }
        }
        require(completed, "fallback failed to complete its fixed-image coverage");
    }
    {
        SchedulerProbe probe{};
        probe.trace->target_score = 0.9f;
        probe.submit(10, {10, 10, 50, 50}); probe.entered();
        const auto initial{probe.finish()};
        require(initial.has_target && initial.fallback_plan_id != 0, "positive fallback missing");
        probe.control.partial_job = initial.job_id;
        probe.detector.control(probe.control);
        probe.entered();
        const auto broad{probe.finish()};
        require(broad.frame_id == 10 && broad.plan_id == initial.fallback_plan_id && broad.stage_kind == 1,
            "nonmatching-positive feedback did not enable same-image fallback asynchronously");
    }
}

void testCancelledRestartRotatesFirstTile() {
    SchedulerProbe probe{};
    probe.submit(10); probe.entered(); probe.submit(11);
    const auto whole{probe.finish()};
    probe.entered();
    probe.control.oldest_frame = 11;
    probe.detector.control(probe.control);
    const auto old_tile{probe.finish()};
    probe.entered(); const auto restarted{probe.finish()};
    probe.entered(); const auto new_tile{probe.finish()};
    require(old_tile.frame_id == 10 && old_tile.plan_id == whole.plan_id
        && restarted.frame_id == 11 && restarted.plan_id != whole.plan_id && restarted.stage_kind == 1
        && new_tile.frame_id == 11 && new_tile.plan_id == restarted.plan_id
        && old_tile.searched_region != new_tile.searched_region,
        "cancelled restart reused source coverage or failed to rotate its first tile");
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
        std::cerr << "Running testActualInferenceObservation\n"; testActualInferenceObservation();
        std::cerr << "Running testLatestFrames\n"; testLatestFrames();
        std::cerr << "Running testTimeAndPublication\n"; testTimeAndPublication();
        std::cerr << "Running testUnknownFrameTime\n"; testUnknownFrameTime();
        std::cerr << "Running testHistoricalArbitrationAndWatchdog\n"; testHistoricalArbitrationAndWatchdog();
        std::cerr << "Running testOverwrite\n"; testOverwrite();
        std::cerr << "Running testSubmittedSnapshotAfterNanoFailure\n"; testSubmittedSnapshotAfterNanoFailure();
        std::cerr << "Running testUnreadResultAdmission\n"; testUnreadResultAdmission();
        std::cerr << "Running testDetectingSnapshotOrder\n"; testDetectingSnapshotOrder();
        std::cerr << "Running testTrackingSnapshotOrderAndEarlyReturns\n"; testTrackingSnapshotOrderAndEarlyReturns();
        std::cerr << "Running testCompletionAfterSnapshot\n"; testCompletionAfterSnapshot();
        std::cerr << "Running testRequestOverlapsRecoveryAndRoundCleanup\n"; testRequestOverlapsRecoveryAndRoundCleanup();
        std::cerr << "Running testOffWithExecutingCurrentRequest\n"; testOffWithExecutingCurrentRequest();
        std::cerr << "Running testHistoryMissAndPartialCatchup\n"; testHistoryMissAndPartialCatchup();
        std::cerr << "Running testInitFailureAndLowIou\n"; testInitFailureAndLowIou();
        std::cerr << "Running testInitializationErrorCodes\n"; testInitializationErrorCodes();
        std::cerr << "Running testNanoFailureWithHighIou\n"; testNanoFailureWithHighIou();
        std::cerr << "Running testActualJumpCounters\n"; testActualJumpCounters();
        testVerifiedIntervalOneAndFour();
        testRegionalPlanVerdicts();
        testSeparateSeedRejectionGuards();
        testExpiredAttemptSeedAndReplay();
        testRepeatedSearchingControlsPreserveCoverage();
        testExpiredTargetedPruningAndOldControl();
        testOneCallSchedulingAndCapacity();
        testCancelledLastStageIsNotCompleteEvidence();
        testCancellationAfterPublicationPreservesUnreadIdentity();
        testFirstMissFallbackAndPartialFeedback();
        testCancelledRestartRotatesFirstTile();
        testCandidateStageProvenance();
        testFreshVerificationProtocol();
        std::cerr << "Running testModes\n"; testModes();
        std::cout << "真实模拟模型的来源淘汰、初始化、低 IoU、部分补帧和三模式检查通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
