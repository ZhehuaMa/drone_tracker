#include "../nanotrack_preprocessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "../host_frame.h"

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
    TestFrame(std::shared_ptr<Trace> trace, bool fallback = false, bool bad = false)
        : trace_{std::move(trace)}, fallback_{fallback}, bad_{bad} {}
    int width() const noexcept override { return 7; }
    int height() const noexcept override { return 5; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return 0; }
    std::unique_ptr<FrameReadView> mapRead() const override {
        ++trace_->calls;
        if (fallback_ && trace_->calls == 1) {
            return {};
        }
        cv::Mat allocation(5, 11, CV_8UC1, cv::Scalar{20});
        return std::make_unique<View>(allocation(cv::Rect{0, 0, 7, 5}), trace_, bad_);
    }

private:
    std::shared_ptr<Trace> trace_;
    bool fallback_{};
    bool bad_{};
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
void numeric(int crop, cv::Point2f center, int left, int top) {
    cv::Mat source(5, 7, CV_8UC1);
    for (int y{0}; y < source.rows; ++y) {
        for (int x{0}; x < source.cols; ++x) {
            source.at<std::uint8_t>(y, x) = static_cast<std::uint8_t>(x * 25 + y * 7);
        }
    }
    // 此固定样例的均值恰为 89。
    rk3576::NanoTrackPreprocessor preprocessor;
    const auto frame{rk3576::copyGrayFrame(source)};
    for (int output : {127, 255}) {
        const ModelInputContract contract{{1, 3, output, output}};
        auto result{output == 127 ? preprocessor.prepareTemplate(frame, center, crop, contract)
                                  : preprocessor.prepareSearch(frame, center, crop, contract)};
        require(static_cast<bool>(result), "固定裁剪样例失败");
        const auto input{std::dynamic_pointer_cast<const rk3576::FloatInput>(result.value)};
        require(input && input->values()->size() == static_cast<std::size_t>(3 * output * output), "Float32 存储错误");
        bool fractional{false};
        for (int y{0}; y < output; ++y) {
            for (int x{0}; x < output; ++x) {
                const auto offset{static_cast<std::size_t>(y * output + x)};
                const float value{input->values()->at(offset)};
                require(std::abs(value - reference(source, left, top, crop, output, x, y, 89)) < 5e-4,
                        "浮点裁剪偏离独立参考");
                require(value >= 0 && value <= 255 && value == input->values()->at(offset + output * output) &&
                            value == input->values()->at(offset + 2 * output * output),
                        "范围/三平面错误");
                fractional = fractional || std::abs(value - std::round(value)) > 0.01f;
            }
        }
        require(fractional, "缩放结果被量化回 uint8");
    }
}
} // namespace
int main() {
    numeric(5, {2, 1}, -1, -2);
    numeric(6, {2, 1}, -1, -2);
    numeric(4, {4, 3}, 2, 1);
    numeric(9, {6, 4}, 1, -1);
    numeric(257, {3, 2}, -126, -127);
    const ModelInputContract z{{1, 3, 127, 127}};
    const ModelInputContract x{{1, 3, 255, 255}};
    PreparedInputRef survivor;
    for (bool fallback : {false, true}) {
        auto trace{std::make_shared<Trace>()};
        FrameRef frame{std::make_shared<TestFrame>(trace, fallback)};
        std::weak_ptr<const Frame> weak{frame};
        auto preprocessor{std::make_unique<rk3576::NanoTrackPreprocessor>()};
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
        auto search{preprocessor->prepareSearch(frame, {0, 0}, 5, x)};
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
