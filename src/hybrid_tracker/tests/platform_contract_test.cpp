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
        if (trace->active != 0) { ++trace->unsafe_destroy; }
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
        return {YoloPreparedRegion{std::make_shared<Input>(contract_), calculateLetterboxTransform(size, {64, 64}).value}};
    }
private:
    ModelInputContract contract_;
};
class YoloPre final : public YoloPreprocessor {
public:
    explicit YoloPre(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 1} {}
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &, const ModelInputContract &c) override {
        ++lifetime_.trace->begins;
        return {std::make_unique<Session>(c)};
    }
private:
    Lifetime lifetime_;
};
class NanoPre final : public NanoTrackPreprocessor {
public:
    explicit NanoPre(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 3} {}
    PreparedInputResult prepareTemplate(const FrameRef &, const cv::Point2f &, int, const ModelInputContract &c) override {
        return {std::make_shared<Input>(c)};
    }
    PreparedInputResult prepareSearch(const FrameRef &, const cv::Point2f &, int, const ModelInputContract &c) override {
        return {std::make_shared<Input>(c)};
    }
    void reset() noexcept override {}
private:
    Lifetime lifetime_;
};
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
        return {{model_test::tensor({1, 5, 10}), 1, 2}};
    }
private:
    Lifetime lifetime_;
};
class Nano final : public model::NanoTrackBackend {
public:
    explicit Nano(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 2} {}
    model::Status setTemplate(const PreparedInputRef &) override { return {true}; }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &) override {
        return {{model_test::tensor({1, 2, 15, 15}), model_test::tensor({1, 4, 15, 15})}};
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
        if (fail) { throw std::runtime_error("injected thread startup failure"); }
        return std::thread{std::move(work)};
    };
}
TrackerSettings settings() {
    TrackerSettings result{};
    result.enable_dcf = false;
    result.kf_fx = result.kf_fy = 100;
    return result;
}
void checkReleased(const Trace &trace) {
    for (const auto &count : trace.destroyed) { require(count == 1, "组件没有恰好销毁一次"); }
    require(trace.active == 0 && trace.unsafe_destroy == 0, "检测尚未结束就销毁了资源");
}
void testMissingAndStartup() {
    for (int missing{0}; missing < 5; ++missing) {
        auto trace{std::make_shared<Trace>()};
        auto c{components(trace)};
        switch (missing) {
            case 0: c.yolo.reset(); break;
            case 1: c.yolo_preprocessor.reset(); break;
            case 2: c.nano.reset(); break;
            case 3: c.nano_preprocessor.reset(); break;
            case 4: c.compatibility.reset(); break;
        }
        auto tracker{HybridTracker::CreateInstance(settings(), std::move(c), launcher(trace))};
        require(!tracker->init(), "缺失组件仍初始化成功");
        require(trace->starts == 0, "缺失组件时启动了线程");
        checkReleased(*trace);
    }
    auto trace{std::make_shared<Trace>()};
    auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace, true))};
    require(!tracker->init(), "线程启动异常没有转换为初始化失败");
    require(trace->starts == 1, "未测试真实线程启动位置");
    checkReleased(*trace);
}
void testCompatibilityAndDestruction() {
    auto trace{std::make_shared<Trace>()};
    trace->block = true;
    auto tracker{HybridTracker::CreateInstance(settings(), components(trace), launcher(trace))};
    require(tracker->init(), "完整跟踪器初始化失败");
    cv::Mat image{80, 100, CV_8UC1, cv::Scalar{20}};
    FrameContext context{};
    context.now_us = 1000000;
    context.frame = std::make_shared<TestFrame<2>>(image, 1000000);
    const auto rejected{tracker->update(context)};
    require(!rejected.consumed && rejected.state == TrackerState::Detecting &&
            rejected.info.find("PlatformTypeMismatch") != std::string::npos, "错配帧未在真实入口拒绝");
    require(trace->begins == 0 && trace->calls == 0, "错配帧触发了检测");
    Builder first{}, independent{};
    context.frame = independent.copyFrom(image, 1000000).value;
    require(tracker->update(context).consumed, "错配帧占用了时间戳或拒绝独立创建器");
    {
        std::unique_lock<std::mutex> lock{trace->mutex};
        require(trace->cv.wait_for(lock, std::chrono::seconds{5}, [&] { return trace->entered; }), "检测未开始");
    }
    // OFF/ON 对应已有 reset；不卸载模型，也不新建检测线程。
    tracker->reset();
    for (const auto &count : trace->destroyed) { require(count == 0, "reset 卸载了模型或预处理器"); }
    require(tracker->init() && trace->starts == 1, "复用时重复创建检测线程");
    context.frame = first.copyFrom(image, 1000000).value;
    require(tracker->update(context).consumed, "reset 未重置时间戳");
    auto destroy{std::async(std::launch::async, [owned = std::move(tracker)]() mutable { owned.reset(); })};
    require(destroy.wait_for(std::chrono::milliseconds{30}) == std::future_status::timeout,
            "最终销毁没有等待正在运行的检测");
    for (const auto &count : trace->destroyed) { require(count == 0, "仍在检测时资源提前释放"); }
    {
        std::lock_guard<std::mutex> lock{trace->mutex};
        trace->release = true;
    }
    trace->cv.notify_all();
    require(destroy.wait_for(std::chrono::seconds{5}) == std::future_status::ready, "最终销毁遗留线程");
    destroy.get();
    checkReleased(*trace);
}

struct ExecutorTrace {
    std::array<int, 3> created{};
    std::array<int, 3> destroyed{};
    int calls{};
};
class Executor final : public model::ModelExecutor {
public:
    Executor(std::shared_ptr<ExecutorTrace> trace, int stage, bool invalid)
        : trace_{std::move(trace)}, stage_{stage} {
        ++trace_->created[stage_];
        if (stage == 0) {
            description_ = {"yolo", {{"image", 0, {1, 3, 64, 64}}}, {{"predictions", 0, {1, 5, 10}}}};
        } else if (stage == 1) {
            description_ = {"backbone", {{"image", 0, {1, 3, -1, -1}, {1, 3, 127, 127}, {1, 3, 255, 255}}},
                            {{"feature", 0, {1, 48, -1, -1}}}};
        } else {
            description_ = {"head", {{"template", 0, {1, 48, 8, 8}}, {"search", 1, {1, 48, 16, 16}}},
                            {{"cls", 0, {1, 2, 15, 15}}, {"loc", 1, {1, 4, 15, 15}}}};
        }
        if (invalid) { description_.inputs.clear(); }
    }
    ~Executor() override { ++trace_->destroyed[stage_]; }
    const model::ModelDescription &description() const noexcept override { return description_; }
    model::ExecutionResult execute(const std::vector<model::Tensor> &) override { ++trace_->calls; return {}; }
private:
    std::shared_ptr<ExecutorTrace> trace_;
    int stage_;
    model::ModelDescription description_{};
};
void testAssembly() {
    const ModelPaths paths{"/models/yolo", "/models/backbone", "/models/head"};
    const std::array<std::string, 3> names{"YOLO", "Backbone", "Head"};
    const std::array<std::string, 3> files{paths.yolo, paths.backbone, paths.head};
    for (bool invalid : {false, true}) {
        for (int fail{-1}; fail < 3; ++fail) {
            auto trace{std::make_shared<ExecutorTrace>()};
            auto resources{std::make_shared<Trace>()};
            int stage{};
            platform_jetson::AssemblyDependencies d{};
            d.load = [&](const std::string &path) -> model::LoadResult {
                const int current{stage++};
                require(path == files[current], "模型路径或加载顺序变化");
                if (current == fail && !invalid) {
                    return model::LoadResult::failure({model::ModelError::LoadFailure, path, {}, {}, {}, "injected"});
                }
                return {std::make_unique<Executor>(trace, current, current == fail && invalid)};
            };
            d.adapt = [](const PreparedInputRef &, const std::string &) -> model::Result<model::Tensor> { return {}; };
            d.yolo_preprocessor = [resources] { return std::make_unique<YoloPre>(resources); };
            d.nano_preprocessor = [resources] { return std::make_unique<NanoPre>(resources); };
            d.compatibility = [resources] { return std::make_unique<Compatibility>(resources); };
            {
                auto result{platform_jetson::assembleTracker(paths, d)};
                if (fail < 0) {
                    require(result && result.value.complete(), "真实装配函数未返回完整集合");
                } else {
                    require(!result && !result.value.yolo && !result.value.nano && !result.value.compatibility,
                            "装配失败返回半套组件");
                    require(result.error.platform == "jetson" && result.error.component == names[fail] &&
                            result.error.path == files[fail], "失败模型身份丢失");
                }
                require(trace->calls == 0 && resources->starts == 0, "装配阶段启动了执行");
            }
            require(trace->created == trace->destroyed, "部分模型没有恰好释放一次");
        }
    }
}
class Source final : public FrameSource {
public:
    bool start() override { running = true; return true; }
    bool stop() override { running = false; return true; }
    bool isRunning() const override { return running; }
    bool getImageSize(int &w, int &h) const override { w = 100; h = 80; return true; }
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override {
        ++waits;
        waited = timeout;
        return take(frame);
    }
    bool tryFrame(FrameRef &frame) override {
        ++takes;
        return take(frame);
    }
    bool take(FrameRef &frame) {
        if (frames.empty()) { return false; }
        frame = std::move(frames.front()); frames.pop_front(); return true;
    }
    void configureRecording(bool save, const std::string &directory, const std::string &prefix) override {
        ++recordings;
        require(!running, "录像配置发生在启动之后");
        recording = save; folder = directory; name = prefix;
    }
    bool running{};
    bool recording{};
    std::string folder{}, name{};
    int takes{};
    int waits{};
    int recordings{};
    std::chrono::microseconds waited{};
    std::deque<FrameRef> frames{};
};
void testSourceAndFrames() {
    Source mock{};
    FrameSource &source{mock};
    configureFrameSourceRecording(source, {true, "records", "prefix"});
    require(mock.recordings == 1 && mock.recording && mock.folder == "records" && mock.name == "prefix",
            "录像参数未传递");
    require(source.start() && source.isRunning(), "启动状态错误");
    FrameRef frame{};
    int notifications{};
    require(!waitForLatestFrame(source, frame, std::chrono::microseconds{1234}, [&] { ++notifications; }),
            "空队列没有返回等待失败");
    require(mock.waits == 1 && mock.waited == std::chrono::microseconds{1234} && mock.takes == 0
            && notifications == 0 && !frame, "超时后仍取帧或触发了首帧回调");
    Builder builder{};
    cv::Mat image{80, 100, CV_8UC1, cv::Scalar{42}};
    for (int timestamp{1}; timestamp <= 3; ++timestamp) { mock.frames.push_back(builder.copyFrom(image, timestamp).value); }
    require(waitForLatestFrame(source, frame, std::chrono::seconds{1}, [&] {
        ++notifications;
        require(frame->timestampUs() == 1 && mock.frames.size() == 2 && mock.takes == 0,
                "计时回调没有发生在首帧之后、排空之前");
    }), "等待首帧失败");
    require(frame->timestampUs() == 3 && mock.waits == 2 && mock.waited == std::chrono::seconds{1}
            && mock.takes == 3 && notifications == 1 && mock.frames.empty(), "最新帧消费链错误");
    mock.frames.push_back(builder.copyFrom(image, 4).value);
    require(waitForLatestFrame(source, frame, std::chrono::seconds{1}) && frame->timestampUs() == 4
            && mock.waits == 3 && mock.takes == 4 && mock.frames.empty(), "单帧或无回调路径错误");
    require(source.stop() && source.stop() && !source.isRunning() && source.start(), "重复停止或重新启动失败");
    source.stop();
    configureFrameSourceRecording(source, {false, "other records", "next"});
    require(mock.recordings == 2 && !mock.recording && mock.folder == "other records" && mock.name == "next",
            "关闭录像时丢失配置或沿用旧值");
    int width{}, height{};
    require(source.getImageSize(width, height) && width == 100 && height == 80, "尺寸查询变化");
    for (int type : {CV_8UC1, CV_8UC3}) {
        std::unique_ptr<FrameReadView> view{};
        {
            auto local{std::make_unique<Builder>()};
            cv::Mat parent{20, 30, type, cv::Scalar::all(42)};
            cv::Mat roi{parent(cv::Rect{2, 3, 10, 8})};
            require(!roi.isContinuous(), "测试没有覆盖行跨度");
            auto copied{local->copyFrom(roi, 1000000)};
            require(copied && copied.value->timestampUs() == 1000000, "时间戳未保留");
            view = copied.value->mapRead();
            parent.setTo(0);
        }
        require(view->width() == 10 && view->height() == 8 && cv::mean(view->mat())[0] == 42,
                "模拟帧复制或创建器/视图生命周期契约错误");
    }
    require(!builder.copyFrom({}, 1) && !builder.copyFrom(cv::Mat{2, 2, CV_8UC4}, 1), "非法图像未拒绝");
}
} // namespace
int main() {
    try {
        testMissingAndStartup();
        testCompatibilityAndDestruction();
        testAssembly();
        testSourceAndFrames();
        std::cout << "platform_contract_tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
