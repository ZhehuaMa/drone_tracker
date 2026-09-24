#include "hdmi_backend.h"
#include "osd/geometry.h"
#include "osd/layout.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <opencv2/core.hpp>

namespace {
void require(bool value, const char *message) { if (!value) { throw std::runtime_error(message); } }
class ProbeFrame final : public Frame {
public:
    ProbeFrame(int channels, int stride, int sequence)
        : channels_{channels}, sequence_{sequence}, bytes_(static_cast<std::size_t>(stride) * 720, 231),
          image_{720, 1280, channels == 1 ? CV_8UC1 : CV_8UC3, bytes_.data(), static_cast<std::size_t>(stride)} {
        for (int y{0}; y < image_.rows; ++y) {
            for (int x{0}; x < image_.cols; ++x) {
                for (int c{0}; c < channels_; ++c) { image_.ptr(y)[x * channels_ + c] = value(x, y, c); }
            }
        }
    }
    unsigned char value(int x, int y, int channel) const {
        return static_cast<unsigned char>((x / 160 * 17 + y / 90 * 7 + sequence_ * 13 + channel * 53) % 256);
    }
    int width() const noexcept override { return 1280; }
    int height() const noexcept override { return 720; }
    FramePixelFormat pixelFormat() const noexcept override {
        return channels_ == 1 ? FramePixelFormat::Gray8 : FramePixelFormat::Bgr8;
    }
    std::uint64_t timestampUs() const noexcept override { return sequence_; }
    class View final : public FrameReadView {
    public:
        explicit View(const ProbeFrame &owner) : owner_{owner} { ++owner_.views_; }
        ~View() override { --owner_.views_; }
        int width() const noexcept override { return owner_.width(); }
        int height() const noexcept override { return owner_.height(); }
        FramePixelFormat pixelFormat() const noexcept override { return owner_.pixelFormat(); }
        std::size_t rowStrideBytes() const noexcept override { return owner_.image_.step; }
        const cv::Mat &mat() const noexcept override { return owner_.image_; }
    private:
        const ProbeFrame &owner_;
    };
    std::unique_ptr<FrameReadView> mapRead() const override { return std::make_unique<View>(*this); }
    void poison() { require(views_ == 0, "source view escaped prepareVideo"); image_.setTo(cv::Scalar{255, 0, 255}); }
private:
    int channels_{}, sequence_{};
    std::vector<unsigned char> bytes_{};
    cv::Mat image_{};
    mutable int views_{};
};
}
int main() {
    try {
        osd::PresentationState state{};
        state.round = 1; state.has_video = true; state.new_video = true; state.status = osd::DisplayStatus::Live;
        state.tracker_state = 2;
        osd::CachedOsdLayout reference{};
        const auto geometry{osd::fitVideo(1280, 720, 720, 480)};
        const ProbeFrame *expected{};
        int channels{};
        cv::Mat frozen{};
        int primary{-1}, overlay{-1};
        std::uint64_t operations{}, repacks{}, previous_generation{};
        bool repeat{};
        auto backend{rk3576::createInspectedHdmiBackend({}, [&](const rk3576::HdmiInspection &snapshot) {
            require(snapshot.generation > previous_generation, "generation did not advance");
            previous_generation = snapshot.generation;
            require(snapshot.background_initializations == (snapshot.generation == 1 ? 4U : 5U),
                    "stable geometry repeated full background initialization");
            require(snapshot.video_operations == operations && snapshot.repacked_frames == repacks,
                    "video operation/copy count changed");
            if (repeat) {
                require(snapshot.primary_index == primary && snapshot.overlay_index != overlay,
                        "repeat did not preserve primary");
                require(cv::norm(snapshot.primary, frozen, cv::NORM_INF) == 0, "frozen pixels changed");
            } else {
                const auto &r{geometry.destination};
                for (int y{0}; y < snapshot.primary.rows; ++y) {
                    for (int x{0}; x < snapshot.primary.cols; ++x) {
                        const auto pixel{snapshot.primary.at<cv::Vec4b>(y, x)};
                        if (x < r.x || x >= r.x + r.width || y < r.y || y >= r.y + r.height) {
                            require(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0, "letterbox not black");
                            continue;
                        }
                        const int sx{(x - r.x) * 1280 / r.width}, sy{(y - r.y) * 720 / r.height};
                        // RGA 插值在色块边缘混合相邻值，边界附近不作为精确色值样本。
                        if (sx % 160 < 4 || sx % 160 > 155 || sy % 90 < 4 || sy % 90 > 85) { continue; }
                        for (int channel{0}; channel < 3; ++channel) {
                            require(std::abs(static_cast<int>(pixel[channel])
                                - expected->value(sx, sy, channels == 1 ? 0 : channel)) <= 2,
                                "source stride/colour/lifetime mismatch");
                        }
                    }
                }
                frozen = snapshot.primary.clone();
            }
            // 时钟字段单独屏蔽，其他覆盖像素必须与同帧元数据重建完全一致。
            cv::Mat actual{snapshot.overlay.clone()};
            cv::Mat wanted{reference.draw(state, geometry, 720, 480, std::chrono::system_clock::now()).clone()};
            actual(cv::Rect{525, 430, 195, 45}).setTo(cv::Scalar{});
            wanted(cv::Rect{525, 430, 195, 45}).setTo(cv::Scalar{});
            require(cv::norm(actual, wanted, cv::NORM_INF) == 0, "overlay metadata pairing mismatch");
            primary = snapshot.primary_index; overlay = snapshot.overlay_index;
        })};
        backend->initialize();
        for (const auto spec : {std::array<int, 2>{1,1280}, {1,1296}, {3,3840}, {3,3888}, {3,4096}}) {
            channels = spec[0];
            for (int i{1}; i <= 60; ++i) {
                ProbeFrame frame{channels, spec[1], i}; expected = &frame;
                backend->prepareVideo(frame);
                frame.poison(); // prepare 返回后复用源像素，不得影响已生成的底图。
                operations += channels == 1 ? 2 : 1;
                if (spec[1] == 4096) { ++repacks; }
                state.new_video = true; state.status = osd::DisplayStatus::Live; state.frame_time_us = i;
                state.boxes = {{static_cast<double>(i * 13), 230, 180, 140, "TARGET"}};
                repeat = false;
                require(backend->present(state, [] { return true; }), "new video rejected");
            }
            std::cout << "PASS source channels=" << channels << " byte_stride=" << spec[1] << " frames=60\n";
        }
        for (int round{0}; round < 20; ++round) {
            repeat = true; state.new_video = false; state.status = osd::DisplayStatus::Stopped; state.boxes.clear();
            state.telemetry.warning = "STOP " + std::to_string(round);
            require(backend->present(state, [] { return true; }), "freeze rejected");
            channels = 3; ProbeFrame frame{3, 3840, round}; expected = &frame;
            backend->prepareVideo(frame); frame.poison(); ++operations;
            state.new_video = true; state.status = osd::DisplayStatus::Live; ++state.round;
            state.telemetry.warning.clear(); repeat = false;
            require(backend->present(state, [] { return true; }), "restart rejected");
        }
        backend->shutdown();
        std::cout << "PASS 20 freeze/resume cycles; video_operations=" << operations << " repacked_frames=" << repacks << '\n';
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
