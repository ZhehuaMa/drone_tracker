#include "hdmi_backend.h"

#include <iostream>
#include <thread>

#include "osd/presenter.h"
#include <opencv2/imgproc.hpp>

namespace {
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
class ProbeFrame final : public Frame {
public:
    ProbeFrame(bool color, std::uint64_t sequence)
        : image_{1200, 1936, color ? CV_8UC3 : CV_8UC1, cv::Scalar{80, 80, 80}}, sequence_{sequence} {
        image_ = image_(cv::Rect{0, 0, 1920, 1200});
        if (color) {
            image_(cv::Rect{0, 0, 960, 1200}).setTo(cv::Scalar{0, 0, 255});
            image_(cv::Rect{960, 0, 960, 1200}).setTo(cv::Scalar{255, 0, 0});
        } else {
            for (int x{0}; x < image_.cols; ++x) {
                image_.col(x).setTo(x * 255 / image_.cols);
            }
        }
        cv::putText(image_, "FRAME " + std::to_string(sequence), {600, 650}, cv::FONT_HERSHEY_SIMPLEX,
                    3.0, cv::Scalar{255, 255, 255}, 6);
    }
    int width() const noexcept override { return image_.cols; }
    int height() const noexcept override { return image_.rows; }
    FramePixelFormat pixelFormat() const noexcept override {
        return image_.channels() == 1 ? FramePixelFormat::Gray8 : FramePixelFormat::Bgr8;
    }
    std::uint64_t timestampUs() const noexcept override { return 123456 + sequence_; }
    std::unique_ptr<FrameReadView> mapRead() const override { return std::make_unique<View>(image_); }
private:
    cv::Mat image_;
    std::uint64_t sequence_{};
};
}
int main(int argc, char **argv) {
    try {
        // 可选参数指定每次打开后的持续秒数；默认保留原有短探针。
        const int seconds{argc > 1 ? std::stoi(argv[1]) : 3};
        const int openings{argc > 2 ? std::stoi(argv[2]) : 2};
        if (argc > 3 || seconds < 1 || seconds > 3600 || openings < 1 || openings > 20) {
            throw std::invalid_argument("usage: osd_hdmi_probe [seconds_per_open: 1..3600] [openings: 1..20]");
        }
        for (int reopening{0}; reopening < openings; ++reopening) {
            osd::OsdPresenter presenter{rk3576::createHdmiBackend({})};
            presenter.start();
            const auto round{presenter.beginRound()};
            const auto deadline{osd::SteadyClock::now() + std::chrono::seconds{seconds}};
            for (std::uint64_t i{1}; osd::SteadyClock::now() < deadline; ++i) {
                const bool color{((i - 1) / 30) % 2 != 0};
                presenter.submit({std::make_shared<ProbeFrame>(color, i), round, i, 123456 + i, 2,
                    {{static_cast<double>((i * 20) % 1800), 300, 100, 100, std::to_string(i)}}});
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
                if (!presenter.failure().empty()) { throw std::runtime_error(presenter.failure()); }
            }
            presenter.endRound(round, osd::DisplayStatus::Stopped);
            // 留出人工核对冻结提示、清框和帧号不变的时间。
            std::this_thread::sleep_for(std::chrono::seconds{5});
            const auto counts{presenter.counters()};
            if (!presenter.failure().empty() || counts.displayed == 0) {
                throw std::runtime_error("OSD probe did not display input");
            }
            presenter.shutdown();
            std::cout << "reopen=" << reopening << " submitted=" << counts.submitted
                      << " replaced=" << counts.replaced << " displayed=" << counts.displayed
                      << " refreshes=" << counts.refreshes << '\n';
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
