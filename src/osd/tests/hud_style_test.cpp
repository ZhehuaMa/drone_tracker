#include "osd/layout.h"

#include <iostream>
#include <stdexcept>

#include <opencv2/imgcodecs.hpp>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { throw std::runtime_error(message); }
}
int opaque(const cv::Mat &image, cv::Rect area) {
    area &= cv::Rect{0, 0, image.cols, image.rows};
    if (area.empty()) { return 0; }
    std::vector<cv::Mat> channels{};
    cv::split(image(area), channels);
    return cv::countNonZero(channels[3]);
}
void equivalent(osd::OsdLayout &legacy, osd::CachedOsdLayout &cached, const osd::PresentationState &state,
                const osd::VideoGeometry &geometry, int width, int height,
                std::chrono::system_clock::time_point time) {
    const auto layers{legacy.draw(state, geometry, width, height, time)};
    cv::Mat combined{layers.hud.clone()};
    for (int y{0}; y < height; ++y) {
        for (int x{0}; x < width; ++x) {
            const auto pixel{layers.target.at<cv::Vec4b>(y, x)};
            if (pixel[3]) { combined.at<cv::Vec4b>(y, x) = pixel; }
        }
    }
    require(cv::norm(combined, cached.draw(state, geometry, width, height, time), cv::NORM_INF) == 0,
        "legacy and cached HUD differ");
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto time{std::chrono::system_clock::time_point{std::chrono::seconds{1760000000}}};
        const auto geometry{osd::fitVideo(1920, 1200, 720, 480)};
        osd::PresentationState state{};
        state.round = 1; state.sequence = 100; state.has_video = true;
        state.status = osd::DisplayStatus::Live; state.tracker_state = 2;
        state.boxes = {{1224, 680, 330, 248, "TRACKING"}};
        auto &telemetry{state.telemetry};
        telemetry.flight.value = osd::FlightState{true, true, "OFFBOARD"};
        telemetry.attitude.value = osd::Attitude{-2, 3, 214};
        telemetry.gps.value = osd::GpsStatus{12, 3, 8.3, 210};
        telemetry.battery.value = osd::Battery{22.2, 4.3, 1.0 - 410.0 / 13000.0, 410, 3.7};
        telemetry.battery_warning.value = 1;
        telemetry.armed_seconds = 75;
        telemetry.altitude_m = 102;
        telemetry.temperature_c.value = 31;
        telemetry.home_distance_m = 240;
        telemetry.home_relative_deg = 266;
        osd::OsdLayout legacy{"FPV"};
        osd::CachedOsdLayout cached{"FPV"};
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        const auto normal{legacy.draw(state, geometry, 720, 480, time)};
        require(opaque(normal.hud, {16, 8, 220, 62}) > 500, "title or subtitle missing");
        require(opaque(normal.hud, {0, 0, 50, 50}) > 40, "corner bracket missing");
        require(opaque(normal.hud, {330, 210, 60, 60}) > 50, "ring reticle missing");
        require(opaque(normal.hud, {320, 32, 80, 55}) > 20, "home arrow missing");
        require(opaque(normal.hud, {16, 320, 230, 45}) > 50, "battery bar missing");
        require(opaque(normal.target, {445, 240, 170, 140}) > 60, "tracking corners missing");
        const cv::Vec4b tracking{normal.target.at<cv::Vec4b>(269, 459)};
        require(tracking[0] == 90 && tracking[1] == 230 && tracking[2] == 90,
            "tracking green corner color");
        for (const auto [tracker, expected] : {
                 std::pair<std::int8_t, cv::Vec4b>{0, {235, 235, 235, 255}},
                 {1, {255, 220, 80, 255}}, {3, {70, 70, 255, 255}}}) {
            state.tracker_state = tracker;
            equivalent(legacy, cached, state, geometry, 720, 480, time);
            const auto layer{legacy.draw(state, geometry, 720, 480, time)};
            require(layer.target.at<cv::Vec4b>(269, 459) == expected, "tracker corner palette");
        }
        state.tracker_state = 2;
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        if (argc > 1) {
            require(cv::imwrite(std::string{argv[1]} + "/hud-normal.png", normal.hud), "normal HUD snapshot");
            require(cv::imwrite(std::string{argv[1]} + "/target-normal.png", normal.target), "normal target snapshot");
        }
        osd::PresentationState alert{state};
        alert.sequence = 150;
        alert.telemetry.battery.value = osd::Battery{20.1, 4.3, 1.f - 10400.f / 13000.f, 10400, 3.35};
        alert.telemetry.battery_warning.value = 3;
        alert.telemetry.altitude_m = 95;
        alert.telemetry.attitude.value->heading_deg = 220;
        alert.telemetry.home_distance_m.reset();
        alert.telemetry.home_relative_deg.reset();
        alert.telemetry.warning = "BATTERY CRITICAL";
        alert.trigger = "TRIG ON";
        alert.boxes = {{1366, 480, 340, 242, "TRACKING"}};
        equivalent(legacy, cached, alert, geometry, 720, 480, time);
        if (argc > 1) {
            const auto alert_layers{legacy.draw(alert, geometry, 720, 480, time)};
            require(cv::imwrite(std::string{argv[1]} + "/hud-alert.png", alert_layers.hud), "alert HUD snapshot");
            require(cv::imwrite(std::string{argv[1]} + "/target-moved.png", alert_layers.target), "moved target snapshot");
        }
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        const auto work{cached.work()};
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        require(cached.work().fields == work.fields && cached.work().dirty_pixels == work.dirty_pixels,
            "stable HUD was repainted");
        state.telemetry.attitude.value = osd::Attitude{0, 8, 214};
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        state.telemetry.attitude.value.reset();
        state.telemetry.home_relative_deg.reset();
        state.telemetry.home_distance_m.reset();
        state.telemetry.battery.value->percentage.reset();
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        const auto missing{legacy.draw(state, geometry, 720, 480, time)};
        require(opaque(missing.hud, {320, 32, 80, 55}) == 0, "expired home arrow remains");
        require(opaque(missing.hud, {16, 320, 230, 45}) == 0, "unknown battery bar remains");
        require(opaque(normal.hud, {232, 140, 72, 200}) > opaque(missing.hud, {232, 140, 72, 200}),
            "expired pitch ladder remains");
        state.telemetry.warning = "FAILSAFE";
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        const auto warning_on{legacy.draw(state, geometry, 720, 480, time)};
        equivalent(legacy, cached, state, geometry, 720, 480, time + std::chrono::milliseconds{300});
        const auto warning_off{legacy.draw(state, geometry, 720, 480, time + std::chrono::milliseconds{300})};
        require(opaque(warning_on.hud, {570, 425, 128, 38}) > opaque(warning_off.hud, {570, 425, 128, 38}),
            "serious warning did not blink");
        state.boxes = {{1400, 750, 240, 170, "A"}};
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        state.status = osd::DisplayStatus::Stopped;
        state.trigger = "TRIG OFF";
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        const auto frozen{legacy.draw(state, geometry, 720, 480, time)};
        require(opaque(frozen.target, {0, 0, 720, 480}) == 0, "frozen target decoration remains");
        require(opaque(frozen.hud, {190, 385, 350, 40}) > 100, "Chinese freeze prompt missing");
        require(opaque(frozen.hud, {570, 425, 128, 38}) > 30, "warning hidden by freeze prompt");
        if (argc > 1) {
            require(cv::imwrite(std::string{argv[1]} + "/hud-frozen.png", frozen.hud), "frozen snapshot");
        }
        const auto wide{osd::fitVideo(1920, 1200, 1280, 720)};
        equivalent(legacy, cached, state, wide, 1280, 720, time);
        equivalent(legacy, cached, state, geometry, 720, 480, time);
        std::cout << "OSD reference HUD style contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
