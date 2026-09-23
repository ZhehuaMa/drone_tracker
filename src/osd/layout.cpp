#include "osd/layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "osd/status_glyphs.h"

#include <opencv2/imgproc.hpp>

namespace osd {
namespace {
const cv::Scalar WHITE{255, 255, 255, 255};
const cv::Scalar AMBER{20, 200, 255, 255};
std::string number(std::optional<double> value, int precision = 1) {
    if (!value || !std::isfinite(*value)) {
        return "--";
    }
    std::ostringstream text{};
    text << std::fixed << std::setprecision(precision) << *value;
    return text.str();
}
void text(cv::Mat &canvas, int x, int y, const std::string &value, const cv::Scalar &color = WHITE) {
    // 二值 alpha 避免 RGA 混合部分透明文字时产生白边。
    std::string ascii{value};
    for (char &c : ascii) {
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) {
            c = '?';
        }
    }
    cv::putText(canvas, ascii, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.48, {0, 0, 0, 255}, 3, cv::LINE_8);
    cv::putText(canvas, ascii, {x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.48, color, 1, cv::LINE_8);
}
void chinese(cv::Mat &canvas, int y, const std::u32string &message) {
    if (message.empty()) {
        return;
    }
    const int width{static_cast<int>(message.size()) * 24};
    int x{(canvas.cols - width) / 2};
    cv::rectangle(canvas, {x - 4, y - 4, width + 8, 32}, {0, 0, 0, 255}, cv::FILLED);
    for (char32_t c : message) {
        const auto glyph{std::find_if(std::begin(STATUS_GLYPHS), std::end(STATUS_GLYPHS),
                                      [c](const StatusGlyph &g) { return g.codepoint == c; })};
        if (glyph == std::end(STATUS_GLYPHS)) {
            throw std::logic_error("OSD missing fixed status glyph");
        }
        for (int row{0}; row < 24; ++row) {
            for (int column{0}; column < 24; ++column) {
                if ((glyph->rows[row] & (1U << column)) && x + column >= 0 && x + column < canvas.cols
                    && y + row >= 0 && y + row < canvas.rows) {
                    canvas.at<cv::Vec4b>(y + row, x + column) = {20, 200, 255, 255};
                }
            }
        }
        x += 24;
    }
}
cv::Scalar stateColor(std::int8_t state) {
    switch (state) {
    case 0: return WHITE;
    case 1: return {255, 255, 0, 255};
    case 2: return {0, 255, 0, 255};
    case 3: return {0, 0, 255, 255};
    default: return WHITE;
    }
}
} // namespace
OsdLayout::OsdLayout(std::string craft_name) : craft_name_{std::move(craft_name)} {}
std::u32string OsdLayout::statusText(DisplayStatus status, bool has_video) {
    const std::u32string prefix{has_video ? U"画面已冻结：" : U""};
    switch (status) {
    case DisplayStatus::WaitingInput: return U"等待输入";
    case DisplayStatus::WaitingFirst: return prefix + U"等待新画面";
    case DisplayStatus::Live: return {};
    case DisplayStatus::Stale: return prefix + U"等待新帧";
    case DisplayStatus::Stopped: return prefix + U"输入已停止";
    case DisplayStatus::Eof: return prefix + U"播放结束";
    case DisplayStatus::InputFailure: return prefix + U"输入/处理故障";
    }
    throw std::invalid_argument("OSD unknown presentation status");
}
DisplayLayers OsdLayout::draw(const PresentationState &state, const VideoGeometry &geometry,
                              int width, int height, std::chrono::system_clock::time_point wall_time) const {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("OSD invalid canvas");
    }
    cv::Mat hud{480, 720, CV_8UC4, cv::Scalar{0, 0, 0, 0}};
    DisplayLayers result{cv::Mat{}, cv::Mat{height, width, CV_8UC4, cv::Scalar{0, 0, 0, 0}}};
    text(hud, 16, 24, craft_name_.substr(0, 24));
    const std::array<const char *, 4> states{{"DETECTING", "TARGET FOUND", "TRACKING", "TARGET LOST"}};
    text(hud, 520, 24, state.tracker_state >= 0 && state.tracker_state < 4
        ? states[state.tracker_state] : "UNKNOWN", stateColor(state.tracker_state));
    const auto &t{state.telemetry};
    text(hud, 16, 54, "MODE " + (t.flight.value ? t.flight.value->mode.substr(0, 20) : "--"));
    text(hud, 16, 78, t.flight.value ? (t.flight.value->armed ? "ARMED" : "DISARMED") : "ARM --");
    text(hud, 16, 102, "T+ " + number(t.armed_seconds, 0) + " s");
    const Battery battery{t.battery.value.value_or(Battery{})};
    text(hud, 16, 140, "BAT " + number(battery.voltage) + " V");
    text(hud, 16, 164, "CELL " + number(battery.cell_voltage, 2) + " V");
    text(hud, 16, 188, "CUR " + number(battery.current_amp) + " A");
    text(hud, 16, 212, "USED " + number(battery.consumed_mah, 0) + " mAh");
    text(hud, 16, 236, "LEFT " + number(battery.percentage ? std::optional<double>{*battery.percentage * 100}
                                                         : std::nullopt, 0) + " %");
    text(hud, 16, 274, "ALT " + number(t.altitude_m) + " m");
    text(hud, 16, 298, "TEMP " + number(t.temperature_c.value) + " C");
    const GpsStatus gps{t.gps.value.value_or(GpsStatus{})};
    text(hud, 520, 54, "SPD " + number(gps.speed_mps) + " m/s");
    text(hud, 520, 78, "SAT " + (gps.satellites ? std::to_string(*gps.satellites) : "--"));
    text(hud, 520, 102, "HDG " + number(t.attitude.value
        ? std::optional<double>{t.attitude.value->heading_deg} : std::nullopt, 0));
    text(hud, 520, 126, "RADAR " + (t.radar_state.value ? std::to_string(*t.radar_state.value) : "--"));
    text(hud, 520, 150, "HOME " + number(t.home_distance_m, 0) + " m");
    text(hud, 470, 290, "LAT " + number(t.position.value
        ? std::optional<double>{t.position.value->latitude_deg} : std::nullopt, 6));
    text(hud, 470, 314, "LON " + number(t.position.value
        ? std::optional<double>{t.position.value->longitude_deg} : std::nullopt, 6));
    constexpr double radians{3.14159265358979323846 / 180.0};
    if (t.attitude.value) {
        const double angle{t.attitude.value->roll_deg * radians};
        const int cy{240 + static_cast<int>(std::clamp(t.attitude.value->pitch_deg, -20.0, 20.0) * 3)};
        const int dx{static_cast<int>(70 * std::cos(angle))};
        const int dy{static_cast<int>(70 * std::sin(angle))};
        cv::line(hud, {360 - dx, cy - dy}, {360 + dx, cy + dy}, WHITE, 2);
    } else {
        text(hud, 300, 270, "ATTITUDE --");
    }
    if (t.home_relative_deg) {
        const double angle{*t.home_relative_deg * radians};
        cv::arrowedLine(hud, {600, 220}, {600 + static_cast<int>(25 * std::sin(angle)),
            220 - static_cast<int>(25 * std::cos(angle))}, AMBER, 2);
    }
    cv::line(hud, {345, 240}, {354, 240}, WHITE, 1);
    cv::line(hud, {366, 240}, {375, 240}, WHITE, 1);
    cv::line(hud, {360, 225}, {360, 234}, WHITE, 1);
    cv::line(hud, {360, 246}, {360, 255}, WHITE, 1);
    text(hud, 200, 362, t.warning.substr(0, 35), AMBER);
    chinese(hud, 390, statusText(state.status, state.has_video));
    text(hud, 16, 454, "FRAME " + (state.has_video ? std::to_string(state.frame_time_us) : "--"));
    const auto time{std::chrono::system_clock::to_time_t(wall_time)};
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream clock{};
    clock << std::put_time(&utc, "%H:%M:%S UTC");
    text(hud, 535, 454, clock.str());
    cv::resize(hud, result.hud, {width, height}, 0, 0, cv::INTER_NEAREST);
    if (state.status == DisplayStatus::Live) {
        for (const auto &box : state.boxes) {
            const auto rectangle{geometry.mapBox(box)};
            if (!rectangle) {
                continue;
            }
            const auto &r{*rectangle};
            const auto color{stateColor(state.tracker_state)};
            const cv::Rect clipped{cv::Rect{r.x, r.y, r.width, r.height} & cv::Rect{0, 0, width, height}};
            if (clipped.empty()) {
                continue;
            }
            cv::rectangle(result.target, clipped, color, state.tracker_state == 2 ? 3 : 1);
            if (!box.label.empty()) {
                text(result.target, clipped.x, std::clamp(clipped.y + 18, 18, std::max(18, height - 2)),
                     box.label.substr(0, 24), color);
            }
            if (state.tracker_state == 2) {
                const cv::Point center{clipped.x + clipped.width / 2, clipped.y + clipped.height / 2};
                cv::drawMarker(result.target, center, color, cv::MARKER_DIAMOND, 12, 2);
            }
        }
    }
    const auto &video{geometry.destination};
    const cv::Rect bounds{cv::Rect{video.x, video.y, video.width, video.height} & cv::Rect{0, 0, width, height}};
    cv::Mat clipped_target{height, width, CV_8UC4, cv::Scalar{0, 0, 0, 0}};
    if (!bounds.empty()) {
        result.target(bounds).copyTo(clipped_target(bounds));
    }
    result.target = std::move(clipped_target);
    return result;
}
} // namespace osd
