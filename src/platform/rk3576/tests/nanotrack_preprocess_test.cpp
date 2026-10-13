#include "../nanotrack_preprocessor.h"
#include "../../../../tests/nanotracev3_performance_benchmarks/window_preprocess/whole_frame_preprocessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

#include "../host_frame.h"
#include "timing_observer.h"

namespace {
void require(bool ok, const char *message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
// 返回受生命周期计数的非连续行视图；首次映射失败可独立验证均值 114 回退。
struct Trace {
    int views{};
    int calls{};
};
class View final : public FrameReadView {
public:
    View(cv::Mat image, std::shared_ptr<Trace> trace, bool bad)
        : image_{std::move(image)}, trace_{std::move(trace)}, bad_{bad} {
        ++trace_->views;
    }
    ~View() override { --trace_->views; }
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::size_t rowStrideBytes() const noexcept override { return bad_ ? 1 : image_.step[0]; }
    const cv::Mat &mat() const noexcept override { return image_; }

private:
    cv::Mat image_;
    std::shared_ptr<Trace> trace_;
    bool bad_{};
};
class TestFrame final : public Frame {
public:
    TestFrame(std::shared_ptr<Trace> trace, bool fallback = false, bool bad = false, bool unmapped = false)
        : trace_{std::move(trace)}, fallback_{fallback}, bad_{bad}, unmapped_{unmapped} {}
    int width() const noexcept override { return 7; }
    int height() const noexcept override { return 5; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return 0; }
    std::unique_ptr<FrameReadView> mapRead() const override {
        ++trace_->calls;
        if (unmapped_ || (fallback_ && trace_->calls == 1)) {
            return {};
        }
        cv::Mat allocation(5, 11, CV_8UC1, cv::Scalar{249});
        allocation(cv::Rect{0, 0, 7, 5}).setTo(20);
        return std::make_unique<View>(allocation(cv::Rect{0, 0, 7, 5}), trace_, bad_);
    }

private:
    std::shared_ptr<Trace> trace_;
    bool fallback_{};
    bool bad_{};
    bool unmapped_{};
};
class ImageFrame final : public Frame {
public:
    explicit ImageFrame(cv::Mat image) : image_{std::move(image)}, trace_{std::make_shared<Trace>()} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return 0; }
    std::unique_ptr<FrameReadView> mapRead() const override { return std::make_unique<View>(image_, trace_, false); }
private:
    cv::Mat image_;
    std::shared_ptr<Trace> trace_;
};
// 双精度四邻点参考，裁剪原点作为固定样例给出，不调用生产几何或 resize。
double pixel(const cv::Mat &source, int x, int y, int fill) {
    return x < 0 || y < 0 || x >= source.cols || y >= source.rows ? fill : source.at<std::uint8_t>(y, x);
}
double reference(const cv::Mat &source, int left, int top, int crop, int output, int x, int y, int fill) {
    const double sx{std::clamp((x + 0.5) * crop / output - 0.5, 0.0, static_cast<double>(crop - 1))};
    const double sy{std::clamp((y + 0.5) * crop / output - 0.5, 0.0, static_cast<double>(crop - 1))};
    const int x0{static_cast<int>(std::floor(sx))};
    const int y0{static_cast<int>(std::floor(sy))};
    const int x1{std::min(x0 + 1, crop - 1)};
    const int y1{std::min(y0 + 1, crop - 1)};
    const double fx{sx - x0};
    const double fy{sy - y0};
    return (pixel(source, left + x0, top + y0, fill) * (1 - fx) + pixel(source, left + x1, top + y0, fill) * fx) *
               (1 - fy) +
           (pixel(source, left + x0, top + y1, fill) * (1 - fx) + pixel(source, left + x1, top + y1, fill) * fx) * fy;
}
void numeric(int crop, cv::Point2f center, int left, int top, int pattern = 0) {
    cv::Mat allocation(5, 11, CV_8UC1, cv::Scalar{251});
    cv::Mat source{allocation(cv::Rect{0, 0, 7, 5})};
    std::mt19937 random{3576};
    for (int y{0}; y < source.rows; ++y) {
        for (int x{0}; x < source.cols; ++x) {
            source.at<std::uint8_t>(y, x) = static_cast<std::uint8_t>(pattern == 0 ? x * 25 + y * 7 :
                pattern == 1 ? random() % 256 : pattern == 2 ? ((x + y) % 2) * 127 : 42);
        }
    }
    const int fill{cv::saturate_cast<unsigned char>(cv::mean(source)[0])};
    rk3576::NanoTrackPreprocessor preprocessor;
    window_preprocess::WholeFramePreprocessor old;
    const FrameRef frame{std::make_shared<ImageFrame>(source)};
    for (int output : {127, 255}) {
        const ModelInputContract contract{{1, 3, output, output}};
        auto result{output == 127 ? preprocessor.prepareTemplate(frame, center, crop, contract)
                                  : preprocessor.prepareSearch(frame, center, crop, contract)};
        require(static_cast<bool>(result), "固定裁剪样例失败");
        auto previous{output == 127 ? old.prepareTemplate(frame, center, crop, contract)
                                   : old.prepareSearch(frame, center, crop, contract)};
        require(previous && *std::dynamic_pointer_cast<const rk3576::FloatInput>(result.value)->values()
                    == *std::dynamic_pointer_cast<const rk3576::FloatInput>(previous.value)->values(),
                "旧整帧路径精确对照失败");
        const auto input{std::dynamic_pointer_cast<const rk3576::FloatInput>(result.value)};
        require(input && input->values()->size() == static_cast<std::size_t>(3 * output * output), "Float32 存储错误");
        bool fractional{false};
        for (int y{0}; y < output; ++y) {
            for (int x{0}; x < output; ++x) {
                const auto offset{static_cast<std::size_t>(y * output + x)};
                const float value{input->values()->at(offset)};
                if (std::abs(value - reference(source, left, top, crop, output, x, y, fill)) >= 5e-4) {
                    std::cerr << "crop=" << crop << " center=" << center << " origin=" << left << "," << top
                              << " pattern=" << pattern << " output=" << output << " pixel=" << x << "," << y
                              << " actual=" << value << " reference="
                              << reference(source, left, top, crop, output, x, y, fill) << std::endl;
                }
                require(std::abs(value - reference(source, left, top, crop, output, x, y, fill)) < 5e-4,
                        "浮点裁剪偏离独立参考");
                // 浮点运算可能在端点产生舍入尾差；输入仍与旧路径逐元素精确对照，不能钳制。
                require(value >= -5e-4 && value <= 255 + 5e-4 && value == input->values()->at(offset + output * output) &&
                            value == input->values()->at(offset + 2 * output * output),
                        "范围/三平面错误");
                fractional = fractional || std::abs(value - std::round(value)) > 0.01f;
            }
        }
        if (pattern == 0 && crop > 1 && crop != output && left < source.cols && top < source.rows
            && left + crop > 0 && top + crop > 0) {
            require(fractional, "缩放结果被量化回 uint8");
        }
    }
}
void matrix() {
    // 独立给定左上角；不从生产几何函数生成预期结果。
    struct Case { int crop; cv::Point2f center; int left, top; };
    const std::vector<Case> cases{
        {5, {2.25f, 1.75f}, -1, -1}, {6, {2.25f, 1.75f}, -1, -2},
        {3, {1.5f, 1.5f}, 0, 0}, {3, {5.5f, 3.5f}, 4, 2},
        {5, {1, 3}, -2, 0}, {5, {7, 3}, 4, 0}, {5, {3, 1}, 0, -2}, {5, {3, 5}, 0, 2},
        {5, {1, 1}, -2, -2}, {5, {7, 1}, 4, -2}, {5, {1, 5}, -2, 2}, {5, {7, 5}, 4, 2},
        {5, {23, 21}, 20, 18}, {5, {-10, -10}, -13, -13},
        {1, {4, 3}, 3, 2}, {127, {64, 64}, 0, 0}, {255, {128, 128}, 0, 0},
        {257, {3, 2}, -126, -127}};
    for (int pattern{0}; pattern < 4; ++pattern) {
        for (const auto &c : cases) { numeric(c.crop, c.center, c.left, c.top, pattern); }
    }
}
void reuseAndFailures() {
    const ModelInputContract z{{1, 3, 127, 127}}, x{{1, 3, 255, 255}};
    rk3576::NanoTrackPreprocessor current;
    window_preprocess::WholeFramePreprocessor previous;
    auto compare{[&](const FrameRef &frame, cv::Point2f center, int size, bool init) {
        auto a{init ? current.prepareTemplate(frame, center, size, z) : current.prepareSearch(frame, center, size, x)};
        auto b{init ? previous.prepareTemplate(frame, center, size, z) : previous.prepareSearch(frame, center, size, x)};
        require(a.error == b.error && static_cast<bool>(a) == static_cast<bool>(b), "失败类别与旧路径不一致");
        if (a) {
            require(*std::dynamic_pointer_cast<const rk3576::FloatInput>(a.value)->values() ==
                    *std::dynamic_pointer_cast<const rk3576::FloatInput>(b.value)->values(), "复用残留旧像素或旧均值");
        }
    }};
    const auto first{rk3576::copyGrayFrame(cv::Mat{5, 7, CV_8UC1, cv::Scalar{17}})};
    const auto second{rk3576::copyGrayFrame(cv::Mat{5, 7, CV_8UC1, cv::Scalar{233}})};
    compare(first, {4, 3}, 3, true);
    compare(first, {4, 3}, 3, false);
    compare(second, {0, 0}, 3, false); // 沿用第一帧全图均值，完整补齐相同尺寸工作区。
    compare(second, {0, 0}, 257, false);
    compare(second, {4, 3}, 1, false);
    compare(second, {0, 0}, 3, true);
    compare(first, {23, 21}, 5, false);
    cv::Mat outside_a{5, 7, CV_8UC1, cv::Scalar{32}};
    cv::Mat outside_b{5, 7, CV_8UC1, cv::Scalar{222}};
    outside_a(cv::Rect{0, 0, 2, 2}).setTo(20);
    outside_b(cv::Rect{0, 0, 2, 2}).setTo(20);
    compare(rk3576::copyGrayFrame(outside_a), {0, 0}, 5, true);
    compare(rk3576::copyGrayFrame(outside_b), {0, 0}, 5, true); // 窗口内不变，窗口外改变全图均值。
    compare(rk3576::copyHostFrame(cv::Mat{5, 7, CV_8UC3, cv::Scalar{20, 20, 20}}), {0, 0}, 5, true);
    for (bool init : {false, true}) {
        auto trace{std::make_shared<Trace>()};
        compare(std::make_shared<TestFrame>(trace, false, true), {23, 21}, 5, init);
        require(trace->views == 0, "失败后映射未释放");
        compare(std::make_shared<TestFrame>(trace, false, false, true), {23, 21}, 5, init);
        require(trace->views == 0, "映射失败后仍保留视图");
        compare(first, {0, 0}, 5, false); // 搜索失败保留均值；模板失败使均值无效。
        compare(second, {0, 0}, 5, true);
    }
    compare({}, {0, 0}, 5, true);
    compare(first, {0, 0}, 5, false);
    compare(first, {0, 0}, 0, true);
    compare(first, {std::numeric_limits<float>::infinity(), 0}, 5, true);
}
void observationPixels() {
    class Observer final : public timing::Observer {
    public:
        int calls{};
        std::uint32_t begin(timing::Stage) noexcept override { ++calls; return 1; }
        void end(std::uint32_t, bool) noexcept override {}
    } observer;
    std::vector<float> expected;
    for (bool enabled : {false, true}) {
        auto trace{std::make_shared<Trace>()};
        auto frame{std::make_shared<TestFrame>(trace)};
        rk3576::NanoTrackPreprocessor preprocessor;
        timing::Observation observation{enabled ? &observer : nullptr};
        auto prepared{preprocessor.prepareTemplate(frame, {0, 0}, 9, ModelInputContract{{1, 3, 127, 127}})};
        require(static_cast<bool>(prepared), "非连续行边缘裁剪失败");
        const auto values{std::dynamic_pointer_cast<const rk3576::FloatInput>(prepared.value)->values()};
        if (enabled) { require(*values == expected && observer.calls == 1, "观测改变非连续行像素"); }
        else { expected = *values; }
    }
}
} // namespace
int main() {
    matrix();
    reuseAndFailures();
    observationPixels();
    numeric(5, {2, 1}, -1, -2);
    numeric(6, {2, 1}, -1, -2);
    numeric(4, {4, 3}, 2, 1);
    numeric(9, {6, 4}, 1, -1);
    numeric(257, {3, 2}, -126, -127);
    const ModelInputContract z{{1, 3, 127, 127}};
    const ModelInputContract x{{1, 3, 255, 255}};
    PreparedInputRef survivor;
    for (bool legacy : {false, true}) {
        for (bool fallback : {false, true}) {
            auto trace{std::make_shared<Trace>()};
            FrameRef frame{std::make_shared<TestFrame>(trace, fallback)};
            std::weak_ptr<const Frame> weak{frame};
            std::unique_ptr<::NanoTrackPreprocessor> preprocessor;
            if (legacy) { preprocessor = std::make_unique<window_preprocess::WholeFramePreprocessor>(); }
            else { preprocessor = std::make_unique<rk3576::NanoTrackPreprocessor>(); }
            auto result{preprocessor->prepareTemplate(frame, {0, 0}, 5, z)};
            require(result && trace->views == 0, "映射租约未释放");
            auto input{std::dynamic_pointer_cast<const rk3576::FloatInput>(result.value)};
            require(input->values()->front() == (fallback ? 114 : 20), "目标均值/114 回退错误");
            require(preprocessor->prepareTemplate(frame, {0, 0}, 5, z).error == PreprocessError::InputBusy,
                    "旧输入被缓存覆盖");
            require(preprocessor->prepareSearch(frame, {0, 0}, 5, x).error == PreprocessError::ContractMismatch,
                    "新模板失败仍复用旧均值");
            result.value.reset();
            input.reset();
            result = preprocessor->prepareTemplate(frame, {0, 0}, 5, z);
            require(static_cast<bool>(result), "释放租约后无法准备模板");
            const auto held_template{std::dynamic_pointer_cast<const rk3576::FloatInput>(result.value)};
            const auto template_values{*held_template->values()};
            auto search{preprocessor->prepareSearch(frame, {0, 0}, 257, x)};
            require(*held_template->values() == template_values, "复写搜索工作区覆盖模板结果");
            require(search && preprocessor->prepareSearch(frame, {0, 0}, 5, x).error == PreprocessError::InputBusy,
                    "搜索租约未生效");
            survivor = search.value;
            frame.reset();
            require(weak.expired() && trace->views == 0, "准备输入仍占用帧");
            preprocessor->reset();
            preprocessor->reset();
            preprocessor.reset();
            const auto kept{std::dynamic_pointer_cast<const rk3576::FloatInput>(survivor)};
            require(kept->values()->front() == 20, "预处理器销毁破坏旧输入");
        }
    }
    rk3576::NanoTrackPreprocessor preprocessor;
    auto trace{std::make_shared<Trace>()};
    auto bad{preprocessor.prepareTemplate(std::make_shared<TestFrame>(trace, false, true), {0, 0}, 5, z)};
    require(!bad && bad.error == PreprocessError::PlatformTypeMismatch && trace->views == 0, "非法映射未拒绝");
    require(preprocessor.prepareTemplate({}, {0, 0}, 5, z).error == PreprocessError::EmptyFrame, "空帧未拒绝");
    auto contract{z};
    contract.scale = 1.0f / 255.0f;
    require(preprocessor.prepareTemplate(std::make_shared<TestFrame>(trace), {0, 0}, 5, contract).error ==
                PreprocessError::ContractMismatch,
            "错误归一化未拒绝");
    std::cout << "NanoTrack 浮点裁剪、奇偶/边缘、均值回退与租约测试通过\n";
}
