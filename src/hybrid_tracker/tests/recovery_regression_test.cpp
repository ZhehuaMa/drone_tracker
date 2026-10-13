#include "hybrid_tracker.h"

#include <condition_variable>
#include <iostream>
#include <mutex>

#include "model_test_support.h"
#include "platform/rk3576/host_frame.h"
#include "platform/rk3576/nanotrack_preprocessor.h"
#include "../../../tests/nanotracev3_performance_benchmarks/window_preprocess/whole_frame_preprocessor.h"

namespace {
using model_test::require;
struct Gate {
    std::mutex mutex{};
    std::condition_variable cv{};
    int entered{}, published{};
    bool first{}, all{};
};
class Storage final : public model::TensorStorage {
public:
    const float *hostData() const noexcept override { return values_.data(); }
    std::size_t byteSize() const noexcept override { return sizeof(float) * values_.size(); }
private:
    std::array<float, 5> values_{50, 40, 16, 14, 0.95f};
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
    Session(ModelInputContract contract, std::shared_ptr<Gate> gate)
        : contract_{std::move(contract)}, gate_{std::move(gate)} {}
    ~Session() override {
        std::lock_guard<std::mutex> lock{gate_->mutex};
        ++gate_->published; // 真实 worker 先提交检测结果，随后销毁会话。
        gate_->cv.notify_all();
    }
    cv::Size sourceSize() const noexcept override { return {100, 80}; }
    PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region) override {
        const auto size{region.empty() ? sourceSize() : region.size()};
        return {YoloPreparedRegion{std::make_shared<Input>(contract_),
                                   calculateLetterboxTransform(size, {100, 80}).value}};
    }
private:
    ModelInputContract contract_;
    std::shared_ptr<Gate> gate_;
};
class YoloPre final : public YoloPreprocessor {
public:
    explicit YoloPre(std::shared_ptr<Gate> gate) : gate_{std::move(gate)} {}
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(
        const FrameRef &, const ModelInputContract &c) override {
        return {std::make_unique<Session>(c, gate_)};
    }
private:
    std::shared_ptr<Gate> gate_;
};
class Yolo final : public model::YoloBackend {
public:
    explicit Yolo(std::shared_ptr<Gate> gate) : gate_{std::move(gate)} {}
    model::Result<int> classCount() const override { return {1}; }
    ModelInputContract inputContract() const override { return {{1, 3, 80, 100}, TensorElementType::Float32,
                                                               TensorLayout::Nchw, 1.0f / 255.0f}; }
    model::Result<model::YoloPrediction> infer(const PreparedInputRef &) override {
        std::unique_lock<std::mutex> lock{gate_->mutex};
        const int call{++gate_->entered};
        gate_->cv.notify_all();
        gate_->cv.wait(lock, [&] { return gate_->all || (call == 1 && gate_->first); });
        model::Tensor tensor{{{1, 5, 1}, TensorElementType::Float32, TensorLayout::Linear,
                               5 * sizeof(float), {}}, std::make_shared<Storage>()};
        return {{std::move(tensor), 1, 2}};
    }
private:
    std::shared_ptr<Gate> gate_;
};
class Compatibility final : public FrameCompatibility {
public:
    PlatformDiagnostic validate(const FrameRef &frame) const override {
        return rk3576::isHostGrayFrame(frame) ? PlatformDiagnostic{} : PlatformDiagnostic{"mock", "frame", {}, "wrong"};
    }
};
struct Trace { std::vector<std::uint64_t> calls{}; bool fail_init{}; std::uint64_t fail_search{}; };
class NanoPre final : public NanoTrackPreprocessor {
public:
    NanoPre(std::unique_ptr<NanoTrackPreprocessor> inner, std::shared_ptr<Trace> trace)
        : inner_{std::move(inner)}, trace_{std::move(trace)} {}
    PreparedInputResult prepareTemplate(const FrameRef &frame, const cv::Point2f &p, int size,
                                        const ModelInputContract &c) override {
        trace_->calls.push_back(frame->timestampUs());
        if (trace_->fail_init) {
            return PreparedInputResult::failure(PreprocessError::PlatformFailure, "controlled init failure");
        }
        return inner_->prepareTemplate(frame, p, size, c);
    }
    PreparedInputResult prepareSearch(const FrameRef &frame, const cv::Point2f &p, int size,
                                        const ModelInputContract &c) override {
        trace_->calls.push_back(frame->timestampUs());
        if (trace_->fail_search == frame->timestampUs()) {
            return PreparedInputResult::failure(PreprocessError::PlatformFailure, "controlled catchup failure");
        }
        return inner_->prepareSearch(frame, p, size, c);
    }
    void reset() noexcept override { inner_->reset(); }
private:
    std::unique_ptr<NanoTrackPreprocessor> inner_;
    std::shared_ptr<Trace> trace_;
};
class Nano final : public model::NanoTrackBackend {
public:
    model::Status setTemplate(const PreparedInputRef &) override { return {true}; }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &) override {
        return {{model_test::tensor({1, 2, 15, 15}), model_test::tensor({1, 4, 15, 15}, 32.0f)}};
    }
    void reset() noexcept override {}
};
void wait(const std::shared_ptr<Gate> &gate, bool publication) {
    std::unique_lock<std::mutex> lock{gate->mutex};
    require(gate->cv.wait_for(lock, std::chrono::seconds{5}, [&] {
        return publication ? gate->published >= 1 : gate->entered >= 1;
    }), "受控检测事件未发生");
}
TrackerOutput scenario(bool legacy, bool fail_init, bool fail_search) {
    auto gate{std::make_shared<Gate>()};
    auto trace{std::make_shared<Trace>()};
    trace->fail_init = fail_init;
    trace->fail_search = fail_search ? 1033334 : 0;
    TrackerComponents components{};
    components.yolo = std::make_unique<Yolo>(gate);
    components.yolo_preprocessor = std::make_unique<YoloPre>(gate);
    components.nano = std::make_unique<Nano>();
    std::unique_ptr<NanoTrackPreprocessor> inner;
    if (legacy) { inner = std::make_unique<window_preprocess::WholeFramePreprocessor>(); }
    else { inner = std::make_unique<rk3576::NanoTrackPreprocessor>(); }
    components.nano_preprocessor = std::make_unique<NanoPre>(std::move(inner), trace);
    components.compatibility = std::make_unique<Compatibility>();
    TrackerSettings settings{};
    settings.enable_dcf = false;
    settings.kf_fx = settings.kf_fy = 100;
    auto tracker{HybridTracker::CreateInstance(settings, std::move(components))};
    // 先于 tracker 析构解除阻塞；断言失败时也不会让 worker 遗留。
    struct Release {
        std::shared_ptr<Gate> gate;
        ~Release() { std::lock_guard<std::mutex> lock{gate->mutex}; gate->all = true; gate->cv.notify_all(); }
    } release{gate};
    require(tracker->init(), "真实 HybridTracker 未初始化");
    auto update{[&](int i) {
        FrameContext context{};
        context.now_us = 1000000 + i * 16667;
        context.frame = rk3576::copyGrayFrame(cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, context.now_us);
        return tracker->update(context);
    }};
    require(update(0).state == TrackerState::Detecting, "无检测结果时状态错误");
    wait(gate, false);
    update(1); update(2); // 检测仍阻塞，积压两张历史帧。
    {
        std::lock_guard<std::mutex> lock{gate->mutex};
        gate->first = true;
        gate->cv.notify_all();
    }
    wait(gate, true);
    const auto output{update(3)};
    if (fail_init) {
        require(trace->calls == std::vector<std::uint64_t>{1000000}, "初始化失败后仍执行补帧");
        require(output.state == TrackerState::Detecting && !output.tracking && !output.display_box.is_valid,
                "初始化失败仍发布有效目标");
    } else {
        require(trace->calls == std::vector<std::uint64_t>{1000000, 1016667, 1033334, 1050001},
                "补帧顺序变化或中间失败后未继续");
        require(output.state == TrackerState::TargetFound && output.tracking && output.display_box.is_valid,
                "恢复成功状态或有效框条件改变");
    }
    return output;
}
} // namespace
int main() {
    for (const auto &failures : {std::pair{true, false}, std::pair{false, true}, std::pair{false, false}}) {
        const auto old{scenario(true, failures.first, failures.second)};
        const auto current{scenario(false, failures.first, failures.second)};
        require(old.state == current.state && old.tracking == current.tracking
            && old.display_box.cx == current.display_box.cx && old.display_box.cy == current.display_box.cy
            && old.display_box.width == current.display_box.width
            && old.display_box.height == current.display_box.height,
            "新旧受控恢复输出不同");
    }
    std::cout << "真实 HybridTracker 初始化失败、补帧失败后继续和恢复成功检查通过\n";
}
