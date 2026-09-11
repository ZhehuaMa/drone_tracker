#include "host_frame.h"

#include <stdexcept>

namespace rk3576 {
namespace {
class View final : public FrameReadView {
public:
    explicit View(cv::Mat image) : image_{std::move(image)} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::size_t rowStrideBytes() const noexcept override { return image_.step[0]; }
    const cv::Mat &mat() const noexcept override { return image_; }

private:
    cv::Mat image_;
};
class HostFrame final : public Frame {
public:
    HostFrame(const cv::Mat &gray, std::uint64_t timestamp) : image_{gray.clone()}, timestamp_{timestamp} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return timestamp_; }
    std::unique_ptr<FrameReadView> mapRead() const override { return std::make_unique<View>(image_); }

private:
    cv::Mat image_;
    std::uint64_t timestamp_;
};
} // namespace
FrameRef copyGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us) {
    if (gray.empty() || gray.type() != CV_8UC1) {
        throw std::invalid_argument("copyGrayFrame requires nonempty Gray8");
    }
    return std::make_shared<HostFrame>(gray, timestamp_us);
}
} // namespace rk3576
