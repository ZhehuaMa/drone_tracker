#include "osd/layout.h"

#include <iostream>
#include <stdexcept>

#include <opencv2/imgcodecs.hpp>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
}
int main(int argc, char **argv) {
    try {
        const osd::OsdLayout layout{};
        const auto geometry{osd::fitVideo(1920, 1200, 720, 480)};
        const std::chrono::system_clock::time_point wall{};
        osd::PresentationState state{};
        state.has_video = true;
        state.status = osd::DisplayStatus::Live;
        state.boxes = {{-20, -20, 100, 100, {}}};
        for (int i{0}; i < 4; ++i) {
            state.tracker_state = static_cast<std::int8_t>(i);
            const auto layers{layout.draw(state, geometry, 720, 480, wall)};
            require(cv::countNonZero(layers.target.reshape(1)) > 0, "valid box missing");
            require(cv::countNonZero(layers.target(cv::Rect{0, 0, 720, 14}).reshape(1)) == 0, "box in margin");
        }
        state.boxes.clear();
        const auto empty{layout.draw(state, geometry, 720, 480, wall)};
        require(cv::countNonZero(empty.target.reshape(1)) == 0, "empty box retained");
        state.status = osd::DisplayStatus::Stopped;
        state.boxes = {{1, 1, 100, 100, {}}};
        const auto frozen{layout.draw(state, geometry, 720, 480, wall)};
        require(cv::countNonZero(frozen.target.reshape(1)) == 0, "frozen box revived");
        state.telemetry.warning = "FAILSAFE";
        const auto warned{layout.draw(state, geometry, 720, 480, wall)};
        require(cv::norm(frozen.hud(cv::Rect{0, 386, 720, 36}),
            warned.hud(cv::Rect{0, 386, 720, 36}), cv::NORM_INF) == 0, "warning covers freeze");
        state.telemetry.attitude.value = osd::Attitude{20, 10, 90};
        const auto fresh{layout.draw(state, geometry, 720, 480, wall)};
        require(cv::norm(fresh.hud, warned.hud, cv::NORM_INF) > 0, "new telemetry not drawn while frozen");
        state.telemetry.attitude.value.reset();
        const auto expired{layout.draw(state, geometry, 720, 480, wall)};
        require(cv::norm(expired.hud, warned.hud, cv::NORM_INF) == 0, "unknown attitude retains horizon");
        for (const auto status : {osd::DisplayStatus::WaitingInput, osd::DisplayStatus::WaitingFirst,
             osd::DisplayStatus::Stale, osd::DisplayStatus::Stopped, osd::DisplayStatus::Eof,
             osd::DisplayStatus::InputFailure}) {
            state.status = status;
            const auto layers{layout.draw(state, geometry, 720, 480, wall)};
            require(cv::countNonZero(layers.hud(cv::Rect{0, 390, 720, 24}).reshape(1)) > 0, "Chinese missing");
            if (argc > 1) {
                require(cv::imwrite(std::string{argv[1]} + "/osd-status-"
                    + std::to_string(static_cast<int>(status)) + ".png", layers.hud), "snapshot failed");
            }
        }
        std::cout << "OSD layout tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
