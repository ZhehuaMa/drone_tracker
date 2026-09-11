#include "model_test_support.h"
#include "platform/rk3576/host_frame.h"
#include "platform/rk3576/yolo_preprocessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
using model_test::require;
struct Trace {
    int views{0};
};
class View final : public FrameReadView {
public:
    View(cv::Mat image, std::shared_ptr<Trace> trace, int bad)
        : image_{std::move(image)}, trace_{std::move(trace)}, bad_{bad} {
        ++trace_->views;
    }
    ~View() override { --trace_->views; }
    int width() const noexcept override { return image_.cols + (bad_ == 2 ? 1 : 0); }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::size_t rowStrideBytes() const noexcept override { return bad_ == 3 ? 1 : image_.step[0]; }
    const cv::Mat &mat() const noexcept override { return image_; }

private:
    cv::Mat image_;
    std::shared_ptr<Trace> trace_;
    int bad_;
};
class PaddedFrame final : public Frame {
public:
    PaddedFrame(int value, std::shared_ptr<Trace> trace, int bad = 0)
        : allocation_(321, 647, CV_8UC1, cv::Scalar{19}), trace_{std::move(trace)}, bad_{bad} {
        allocation_(cv::Rect{0, 0, 640, 321}).setTo(value);
    }
    int width() const noexcept override { return 640; }
    int height() const noexcept override { return 321; }
    FramePixelFormat pixelFormat() const noexcept override {
        return bad_ == 4 ? FramePixelFormat::Bgr8 : FramePixelFormat::Gray8;
    }
    std::uint64_t timestampUs() const noexcept override { return 17; }
    std::unique_ptr<FrameReadView> mapRead() const override {
        if (bad_ == 1) {
            return {};
        }
        return std::make_unique<View>(allocation_(cv::Rect{0, 0, 640, 321}), trace_, bad_);
    }

private:
    cv::Mat allocation_;
    std::shared_ptr<Trace> trace_;
    int bad_;
};
// 独立双精度参考：按像素中心映射，边缘钳制后对四个邻点加权。
// 不调用 cv::resize 或生产几何函数，避免与被测路径共用同一实现。
double bilinearReference(const cv::Mat &source, const cv::Rect &roi, const cv::Size &size, int x, int y) {
    const double sx{std::clamp((x + 0.5) * roi.width / size.width - 0.5, 0.0,
                               static_cast<double>(roi.width - 1))};
    const double sy{std::clamp((y + 0.5) * roi.height / size.height - 0.5, 0.0,
                               static_cast<double>(roi.height - 1))};
    const int x0{static_cast<int>(std::floor(sx))};
    const int y0{static_cast<int>(std::floor(sy))};
    const int x1{std::min(x0 + 1, roi.width - 1)};
    const int y1{std::min(y0 + 1, roi.height - 1)};
    const double fx{sx - x0};
    const double fy{sy - y0};
    const double top{source.at<std::uint8_t>(roi.y + y0, roi.x + x0) * (1 - fx) +
                     source.at<std::uint8_t>(roi.y + y0, roi.x + x1) * fx};
    const double bottom{source.at<std::uint8_t>(roi.y + y1, roi.x + x0) * (1 - fx) +
                        source.at<std::uint8_t>(roi.y + y1, roi.x + x1) * fx};
    return top * (1 - fy) + bottom * fy;
}
void checkNonuniformResize(const cv::Size &source_size, const cv::Rect &roi,
                          const cv::Size &expected_size, int expected_top, int expected_bottom) {
    cv::Mat source(source_size, CV_8UC1);
    for (int y{0}; y < source.rows; ++y) {
        for (int x{0}; x < source.cols; ++x) {
            source.at<std::uint8_t>(y, x) = static_cast<std::uint8_t>((37 * x + 61 * y + 97 * ((x + y) % 2)) % 256);
        }
    }
    rk3576::YoloPreprocessor preprocessor;
    auto session{preprocessor.beginFrame(rk3576::copyGrayFrame(source), rk3576::yoloContract())};
    require(static_cast<bool>(session), "非均匀图像会话失败");
    auto prepared{session.value->prepare(roi)};
    require(static_cast<bool>(prepared), "非均匀区域准备失败");
    const auto &lb{prepared.value.transform};
    require(lb.resized == expected_size && lb.left == 0 && lb.right == 0 &&
                lb.top == expected_top && lb.bottom == expected_bottom,
            "非整数缩放尺寸或边距错误");
    require(lb.scale_x == static_cast<float>(expected_size.width) / roi.width &&
                lb.scale_y == static_cast<float>(expected_size.height) / roi.height,
            "坐标还原没有使用实际每轴比例");
    const auto input{std::dynamic_pointer_cast<const rk3576::ByteInput>(prepared.value.input)};
    require(input && input->bytes().size() == 640 * 640 * 3, "缩放输出存储错误");
    double max_error{0};
    int max_rounded_error{0};
    for (int y{0}; y < 640; ++y) {
        for (int x{0}; x < 640; ++x) {
            const auto offset{static_cast<std::size_t>(y * 640 + x) * 3};
            const int actual{input->bytes()[offset]};
            require(input->bytes()[offset + 1] == actual && input->bytes()[offset + 2] == actual,
                    "缩放后三个灰度通道不一致");
            if (y < expected_top || y >= expected_top + expected_size.height) {
                require(actual == 114, "非均匀区域填充不是 114");
                continue;
            }
            const double reference{bilinearReference(source, roi, expected_size, x, y - expected_top)};
            max_error = std::max(max_error, std::abs(actual - reference));
            const int rounded_error{std::abs(actual - static_cast<int>(std::lround(reference)))};
            max_rounded_error = std::max(max_rounded_error, rounded_error);
            // U8 线性插值使用定点权重及整数舍入；允许与双精度四舍五入相差一个灰度级。
            require(rounded_error <= 1, "非均匀缩放偏离独立双线性参考超过 1 灰度级");
        }
    }
    std::cout << "非均匀缩放 source=" << source_size.width << 'x' << source_size.height
              << " roi=" << roi.x << ',' << roi.y << ',' << roi.width << ',' << roi.height
              << " resized=" << expected_size.width << 'x' << expected_size.height
              << " max_abs=" << max_error << " max_rounded=" << max_rounded_error << '\n';
}
} // namespace
int main() {
    checkNonuniformResize({17, 13}, {2, 1, 11, 7}, {640, 407}, 116, 117);
    checkNonuniformResize({1001, 809}, {2, 3, 997, 797}, {640, 512}, 64, 64);
    rk3576::YoloPreprocessor preprocessor;
    const auto contract{rk3576::yoloContract()};
    for (int value : {0, 114, 255}) {
        auto trace{std::make_shared<Trace>()};
        FrameRef source{std::make_shared<PaddedFrame>(value, trace)};
        std::weak_ptr<const Frame> weak{source};
        auto session{preprocessor.beginFrame(source, contract)};
        require(session && trace->views == 0, "快照仍占用映射");
        source.reset();
        require(weak.expired(), "快照仍持有源帧");
        auto prepared{session.value->prepare()};
        require(prepared && prepared.value.transform.top == 159 && prepared.value.transform.bottom == 160,
                "奇数填充几何错误");
        auto bytes{std::dynamic_pointer_cast<const rk3576::ByteInput>(prepared.value.input)};
        for (std::size_t y{0}; y < 640; ++y) {
            for (std::size_t x{0}; x < 640 * 3; ++x) {
                require(bytes->bytes()[y * 640 * 3 + x] == (y >= 159 && y < 480 ? value : 114),
                        "像素/填充/三通道或行跨度错误");
            }
        }
        require(session.value->prepare().error == PreprocessError::InputBusy, "缓存覆盖旧输入");
        prepared.value.input.reset();
        require(session.value->prepare().error == PreprocessError::InputBusy, "别名租约未生效");
        bytes.reset();
        require(session.value->prepare({639, 0, 2, 1}).error == PreprocessError::InvalidRegion, "区域越界未拒绝");
        require(static_cast<bool>(session.value->prepare({0, 0, 320, 160})), "释放输入后未恢复");
    }
    auto trace{std::make_shared<Trace>()};
    for (int bad : {1, 2, 3, 4}) {
        auto result{preprocessor.beginFrame(std::make_shared<PaddedFrame>(0, trace, bad), contract)};
        require(!result && !result.value && trace->views == 0, "非法映射/格式泄漏会话或租约");
    }
    require(preprocessor.beginFrame({}, contract).error == PreprocessError::EmptyFrame, "空帧未拒绝");
    auto bad{contract};
    bad.scale = 1.0f / 255.0f;
    require(preprocessor.beginFrame(std::make_shared<PaddedFrame>(0, trace), bad).error ==
                PreprocessError::ContractMismatch,
            "重复归一化契约未拒绝");
    auto original{cv::Mat(2, 2, CV_8UC1, cv::Scalar{255})};
    auto owned{rk3576::copyGrayFrame(original, 123)};
    original.setTo(0);
    require(owned->timestampUs() == 123 && owned->mapRead()->mat().at<std::uint8_t>(0, 0) == 255, "主机帧没有自有复制");
    std::cout << "RKNN 快照、像素、几何、映射和租约测试通过\n";
}
