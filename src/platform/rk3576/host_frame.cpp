#include "host_frame.h"

#include <stdexcept>

namespace rk3576 {
namespace {
class View final : public FrameReadView {
public:
    explicit View(cv::Mat image) : image_{std::move(image)} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return image_.channels() == 1 ? FramePixelFormat::Gray8 : FramePixelFormat::Bgr8; }
    std::size_t rowStrideBytes() const noexcept override { return image_.step[0]; }
    const cv::Mat &mat() const noexcept override { return image_; }

private:
    cv::Mat image_;
};
class HostFrame final : public Frame {
public:
    struct RetainPixels {};
    HostFrame(const cv::Mat &gray, std::uint64_t timestamp, diagnostics::FrameIdentity identity = {})
        : image_{gray.clone()}, timestamp_{timestamp}, identity_{identity} {}
    HostFrame(RetainPixels, const cv::Mat &gray, std::uint64_t timestamp, diagnostics::FrameIdentity identity)
        : image_{gray}, timestamp_{timestamp}, identity_{identity} {}
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return image_.channels() == 1 ? FramePixelFormat::Gray8 : FramePixelFormat::Bgr8; }
    std::uint64_t timestampUs() const noexcept override { return timestamp_; }
    diagnostics::FrameIdentity diagnosticIdentity() const noexcept override { return identity_; }
    std::unique_ptr<FrameReadView> mapRead() const override { return std::make_unique<View>(image_); }

private:
    cv::Mat image_;
    std::uint64_t timestamp_;
    diagnostics::FrameIdentity identity_{};
};
} // namespace
FrameRef copyHostFrame(const cv::Mat &image, std::uint64_t timestamp_us, diagnostics::FrameIdentity identity) {
    if (image.empty() || (image.type() != CV_8UC1 && image.type() != CV_8UC3)) {
        throw std::invalid_argument("copyHostFrame requires nonempty Gray8 or BGR8");
    }
    return std::make_shared<HostFrame>(image, timestamp_us, identity);
}
bool isHostGrayFrame(const FrameRef &frame) noexcept {
    const auto host{std::dynamic_pointer_cast<const HostFrame>(frame)};
    return host && host->pixelFormat() == FramePixelFormat::Gray8;
}
FrameRef copyGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us, diagnostics::FrameIdentity identity) {
    if (gray.empty() || gray.type() != CV_8UC1) {
        throw std::invalid_argument("copyGrayFrame requires nonempty Gray8");
    }
    return std::make_shared<HostFrame>(gray, timestamp_us, identity);
}
FrameRef retainGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us, diagnostics::FrameIdentity identity) {
    if (gray.empty() || gray.type() != CV_8UC1 || !gray.u || (gray.u->flags & cv::UMatData::USER_ALLOCATED)) {
        throw std::invalid_argument("retainGrayFrame requires reference-counted Gray8 storage");
    }
    return std::make_shared<HostFrame>(HostFrame::RetainPixels{}, gray, timestamp_us, identity);
}
} // namespace rk3576
