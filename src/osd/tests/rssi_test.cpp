#include "osd/layout.h"

#include <iostream>
#include <stdexcept>

#include <opencv2/imgcodecs.hpp>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { throw std::runtime_error(message); }
}
cv::Mat combined(const osd::DisplayLayers &layers) {
    cv::Mat image{layers.hud.clone()};
    for (int y{0}; y < image.rows; ++y) {
        for (int x{0}; x < image.cols; ++x) {
            const auto pixel{layers.target.at<cv::Vec4b>(y, x)};
            if (pixel[3]) { image.at<cv::Vec4b>(y, x) = pixel; }
        }
    }
    return image;
}
} // namespace
int main(int argc, char **argv) {
    try {
        const osd::OsdLayout legacy{};
        const std::chrono::system_clock::time_point time{};
        osd::PresentationState state{};
        state.status = osd::DisplayStatus::Live;
        state.has_video = true;
        state.tracker_state = 2;
        state.boxes = {{1224, 680, 330, 248, "TRACKING"}};
        state.telemetry.battery.value = osd::Battery{22.2, 4.3, 0.96, 410, 3.7};
        const auto geometry{osd::fitVideo(1920, 1200, 720, 480)};
        state.telemetry.rssi_percent.value = 85;
        const auto baseline{legacy.draw(state, geometry, 720, 480, time)};
        for (const auto temperature : {std::optional<double>{}, {31}, {-25}}) {
            state.telemetry.temperature_c.value = temperature;
            require(cv::norm(baseline.hud, legacy.draw(state, geometry, 720, 480, time).hud,
                cv::NORM_INF) == 0, "temperature still changes display");
        }
        const cv::Rect meter{16, 310, 260, 54};
        for (int warning : {1, 2, 3}) {
            state.telemetry.battery_warning.value = warning;
            state.telemetry.battery.value = osd::Battery{20.1, 2.1, 0.02, 900, 3.35};
            const auto changed{legacy.draw(state, geometry, 720, 480, time)};
            require(cv::norm(baseline.hud(meter), changed.hud(meter), cv::NORM_INF) == 0,
                "battery fields or warning change RSSI meter");
            require(cv::norm(baseline.hud(cv::Rect{550, 160, 150, 150}),
                changed.hud(cv::Rect{550, 160, 150, 150}), cv::NORM_INF) > 0,
                "battery fields no longer update");
        }
        for (int value : {0, 10, 85, 100, -1, 101}) {
            state.telemetry.rssi_percent.value = value;
            const auto layers{legacy.draw(state, geometry, 720, 480, time)};
            const int width{value >= 0 && value <= 100 ? 148 * value / 100 : 0};
            for (int x{0}; x < 148; ++x) {
                require(layers.hud.at<cv::Vec4b>(335, 21 + x)
                    == (x < width ? cv::Vec4b{60, 200, 255, 255} : cv::Vec4b{0, 0, 0, 255}),
                    "RSSI bar width or yellow color incorrect");
            }
        }
        state.telemetry.rssi_percent.value.reset();
        const auto unknown{legacy.draw(state, geometry, 720, 480, time)};
        state.telemetry.rssi_percent.value = 0;
        require(cv::norm(unknown.hud(meter), legacy.draw(state, geometry, 720, 480, time).hud(meter),
            cv::NORM_INF) > 0, "unknown RSSI displayed as zero");
        for (const cv::Size size : {cv::Size{720, 480}, {1280, 720}}) {
            const auto geom{osd::fitVideo(1920, 1200, size.width, size.height)};
            for (int phase{0}; phase < 3; ++phase) {
                osd::CachedOsdLayout cached{};
                state.status = phase == 2 ? osd::DisplayStatus::Stopped : osd::DisplayStatus::Live;
                state.has_tracking_result = phase == 0;
                cv::Mat previous{};
                cv::Mat previous_target{};
                for (const auto value : {std::optional<int>{100}, {}, {9}, {85}, {40}}) {
                    state.telemetry.rssi_percent.value = value;
                    const auto layers{legacy.draw(state, geom, size.width, size.height, time)};
                    const cv::Mat actual{cached.draw(state, geom, size.width, size.height, time).clone()};
                    require(cv::norm(actual, combined(layers), cv::NORM_INF) == 0,
                        "RSSI cached update differs from full rendering");
                    if (!previous.empty()) {
                        const cv::Rect region{static_cast<int>(16.0 * size.width / 720),
                            static_cast<int>(310.0 * size.height / 480),
                            static_cast<int>(260.0 * size.width / 720) + 2,
                            static_cast<int>(54.0 * size.height / 480) + 2};
                        cv::Mat before{previous.clone()}, after{actual.clone()};
                        before(region).setTo(cv::Scalar{}); after(region).setTo(cv::Scalar{});
                        require(cv::norm(before, after, cv::NORM_INF) == 0, "RSSI changed unrelated HUD");
                        require(cv::norm(previous_target, layers.target, cv::NORM_INF) == 0,
                            "RSSI changed target layer");
                    }
                    const auto work{cached.work()};
                    cached.draw(state, geom, size.width, size.height, time);
                    require(cached.work().fields == work.fields && cached.work().glyphs == work.glyphs,
                        "stable RSSI redraws cached fields");
                    if (argc > 1 && phase == 0 && value == 85) {
                        require(cv::imwrite(std::string{argv[1]} + "/rssi-85-" + std::to_string(size.width)
                            + ".png", actual), "RSSI snapshot failed");
                    }
                    previous = actual; previous_target = layers.target;
                }
            }
        }
        std::cout << "RSSI rendering tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
