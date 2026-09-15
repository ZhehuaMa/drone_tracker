#ifndef ROS_TEST_SUPPORT_H
#define ROS_TEST_SUPPORT_H
#include "hybrid_tracker/tests/model_test_support.h"
#include "platform/platform_factory.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
namespace ros_test {
struct Trace {
    std::array<std::atomic<int>, 5> destroyed{};
    std::atomic_bool target{false};
    std::atomic_bool nano_ok{true};
    std::atomic<int> assemblies{};
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
        auto output{model_test::tensor({1, 5, 10})};
        float *data{const_cast<float *>(output.read().data())};
        data[0] = 32; data[10] = 32; data[20] = 10; data[30] = 10;
        data[40] = t.target ? 0.95f : 0.0f;
        return {{output, 1, 2}};
    }
private:
    Lifetime lifetime_;
};
class Nano final : public model::NanoTrackBackend {
public:
    explicit Nano(std::shared_ptr<Trace> trace) : lifetime_{std::move(trace), 2} {}
    model::Status setTemplate(const PreparedInputRef &) override { return {true}; }
    model::Result<model::NanoPrediction> search(const PreparedInputRef &) override {
        if (!lifetime_.trace->nano_ok) {
            return model::Result<model::NanoPrediction>::failure(
                {model::ModelError::ExecutionFailure, "test", {}, {}, {}, "injected lost target"});
        }
        auto score{model_test::tensor({1, 2, 15, 15}, -5.0f)};
        float *values{const_cast<float *>(score.read().data())};
        values[225 + 112] = 5.0f;
        return {{score, model_test::tensor({1, 4, 15, 15}, 8.0f)}};
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

} // namespace ros_test
#endif // ROS_TEST_SUPPORT_H
