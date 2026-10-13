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
        osd::CachedOsdLayout cached{};
        state = {}; state.round = 1; state.status = osd::DisplayStatus::Live; state.has_video = true;
        int comparison{};
        const auto compare{[&](int width, int height) {
            ++comparison;
            const auto geom{osd::fitVideo(1920, 1200, width, height)};
            osd::CachedOsdLayout reference{};
            const auto &actual{cached.draw(state, geom, width, height, wall)};
            const auto &expected{reference.draw(state, geom, width, height, wall)};
            const auto layers{layout.draw(state, geom, width, height, wall)};
            cv::Mat combined{layers.hud.clone()};
            for (int y{0}; y < height; ++y) {
                for (int x{0}; x < width; ++x) {
                    const auto pixel{layers.target.at<cv::Vec4b>(y, x)};
                    if (pixel[3]) { combined.at<cv::Vec4b>(y, x) = pixel; }
                }
            }
            require(cv::norm(actual, combined, cv::NORM_INF) == 0,
                "dirty update differs from full legacy redraw");
            if (cv::norm(actual, expected, cv::NORM_INF) != 0) {
                std::cerr << "cached comparison=" << comparison << "\n";
                if (argc > 1) {
                    cv::imwrite(std::string{argv[1]} + "/cached-actual.png", actual);
                    cv::imwrite(std::string{argv[1]} + "/cached-expected.png", expected);
                }
                throw std::runtime_error("dirty update differs from clean reconstruction");
            }
            for (int y{0}; y < actual.rows; ++y) {
                for (int x{0}; x < actual.cols; ++x) {
                    const auto alpha{actual.at<cv::Vec4b>(y, x)[3]};
                    require(alpha == 0 || alpha == 255, "unexpected partial alpha");
                }
            }
        }};
        compare(720, 480);
        const auto original_hud{layout.draw(state, geometry, 720, 480, wall)};
        const auto &cached_hud{cached.draw(state, geometry, 720, 480, wall)};
        require(cv::norm(original_hud.hud, cached_hud, cv::NORM_INF) == 0,
                "cached glyph positions differ from legacy HUD");
        for (int index{0}; index < 30; ++index) {
            state.frame_time_us = 1790233400123000ULL + index * 17813;
            state.telemetry.warning = "WARN " + std::to_string(index) + " SENSOR A/B";
            state.telemetry.armed_seconds = index * 17.;
            state.telemetry.flight.value = osd::FlightState{true, index % 2 != 0, "MODE " + std::to_string(index)};
            const auto full{layout.draw(state, geometry, 720, 480, wall)};
            require(cv::norm(full.hud, cached.draw(state, geometry, 720, 480, wall), cv::NORM_INF) == 0,
                    "dynamic glyph run differs from legacy HUD");
        }
        const auto *pixels{cached.draw(state, geometry, 720, 480, wall).data};
        const auto stable{cached.work()};
        for (int n{0}; n < 20; ++n) { require(cached.draw(state, geometry, 720, 480, wall).data == pixels,
                                                     "stable canvas reallocated"); }
        require(cached.work().glyphs == stable.glyphs && cached.work().fields == stable.fields
            && cached.work().dirty_pixels == stable.dirty_pixels, "unchanged content repainted");
        state.telemetry.warning = "LONG WARNING TEXT"; compare(720, 480);
        state.telemetry.warning = "X"; compare(720, 480);
        state.boxes = {{100, 200, 220, 160, "TARGET"}}; state.tracker_state = 2; compare(720, 480);
        state.boxes = {{400, 350, 110, 80, "A"}}; compare(720, 480);
        state.boxes = {{100, 200, 2, 2, "WWWWWWWWWWWWWWWWWWWWWWWW"}}; compare(720, 480);
        for (const cv::Size size : {cv::Size{720, 480}, cv::Size{1280, 720}}) {
            const auto geom{osd::fitVideo(1920, 1200, size.width, size.height)};
            for (const osd::DisplayBox box : {osd::DisplayBox{100, 200, 220, 160, {}},
                     {0, 0, 2, 2, {}}, {1918, 1198, 2, 2, {}}}) {
                state.boxes = {box};
                for (int tracker{0}; tracker < 4; ++tracker) {
                    state.tracker_state = static_cast<std::int8_t>(tracker);
                    state.boxes.front().label.clear();
                    compare(size.width, size.height);
                    const auto unlabeled{layout.draw(state, geom, size.width, size.height, wall)};
                    const cv::Mat cached_unlabeled{cached.draw(state, geom, size.width, size.height, wall).clone()};
                    const auto unchanged{cached.work()};
                    for (const std::string label : {std::string{}, std::string{"A"},
                             std::string(80, 'W')}) {
                        state.boxes.front().label = label;
                        const auto layers{layout.draw(state, geom, size.width, size.height, wall)};
                        require(cv::norm(layers.target, unlabeled.target, cv::NORM_INF) == 0,
                            "input label changed target pixels");
                        require(cv::norm(cached.draw(state, geom, size.width, size.height, wall),
                            cached_unlabeled, cv::NORM_INF) == 0, "input label changed cached pixels");
                        require(cached.work().fields == unchanged.fields
                            && cached.work().dirty_pixels == unchanged.dirty_pixels,
                            "input label repainted target");
                        require(osd::targetLabelText(state).empty(), "invisible label reported");
                        compare(size.width, size.height);
                    }
                }
            }
        }
        state.boxes.clear(); compare(720, 480);
        state.telemetry.attitude.value = osd::Attitude{45, 15, 180}; compare(720, 480);
        state.telemetry.attitude.value.reset(); compare(720, 480);
        for (const auto status : {osd::DisplayStatus::WaitingInput, osd::DisplayStatus::WaitingFirst,
                 osd::DisplayStatus::Stale, osd::DisplayStatus::Stopped, osd::DisplayStatus::Eof,
                 osd::DisplayStatus::InputFailure}) {
            state.boxes = {{100, 200, 220, 160, "TARGET"}};
            state.status = status;
            compare(720, 480);
            require(osd::targetLabelText(state).empty(), "non-live label reported");
        }
        state.status = osd::DisplayStatus::Live;
        state.has_tracking_result = false;
        state.mission_active = true;
        state.telemetry.warning = "FAILSAFE";
        for (auto phase : {osd::TrackingPhase::Idle, osd::TrackingPhase::Starting, osd::TrackingPhase::Failed}) {
            state.tracking_phase = phase;
            compare(720, 480);
            const auto preview{layout.draw(state, geometry, 720, 480, wall)};
            require(cv::countNonZero(preview.target.reshape(1)) == 0, "preview retained target decorations");
            require(osd::statusBarText(state) == "ON MISSION", "phase removed mission text");
            require(cv::countNonZero(preview.hud(cv::Rect{250, 360, 200, 25}).reshape(1)) > 0,
                "independent tracking phase hint missing");
        }
        state.has_tracking_result = true;
        state.mission_active = false;
        state.status = osd::DisplayStatus::Stopped;
        const auto legacy_status{layout.draw(state, geometry, 720, 480, wall)};
        const auto &cached_status{cached.draw(state, geometry, 720, 480, wall)};
        require(cv::norm(cached_status(cv::Rect{0, 386, 720, 32}),
                         legacy_status.hud(cv::Rect{0, 386, 720, 32}), cv::NORM_INF) == 0,
                "fixed status differs from legacy layout");
        require(cv::norm(cached_status(cv::Rect{0, 0, 200, 32}),
                         legacy_status.hud(cv::Rect{0, 0, 200, 32}), cv::NORM_INF) == 0,
                "static theme differs from legacy layout");
        state.status = osd::DisplayStatus::Live; compare(720, 480);
        ++state.round; compare(720, 480);
        compare(1280, 720); compare(720, 480);
        cached.setCraftName("NEW THEME");
        osd::CachedOsdLayout theme{"NEW THEME"};
        require(cv::norm(cached.draw(state, geometry, 720, 480, wall),
                         theme.draw(state, geometry, 720, 480, wall), cv::NORM_INF) == 0, "theme not rebuilt");
        std::cout << "OSD layout tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
