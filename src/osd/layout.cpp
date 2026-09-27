#include "osd/layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>

#include "osd/reference_font.h"
#include "osd/short_text.h"
#include "osd/status_glyphs.h"

#include <opencv2/imgproc.hpp>

namespace osd {
namespace {
constexpr int WIDTH{720};
constexpr int HEIGHT{480};
const cv::Scalar WHITE{235, 235, 235, 255};
const cv::Scalar AMBER{60, 200, 255, 255};
const cv::Scalar RED{70, 70, 255, 255};
const cv::Scalar GREEN{90, 230, 90, 255};
const cv::Scalar CYAN{255, 220, 80, 255};
const cv::Scalar BLUE{255, 150, 90, 255};
const cv::Scalar BLACK{0, 0, 0, 255};
constexpr double RADIANS{3.14159265358979323846 / 180.0};

cv::Scalar stateColor(std::int8_t state) {
    switch (state) {
    case 0: return WHITE;
    case 1: return CYAN;
    case 2: return GREEN;
    case 3: return RED;
    default: return WHITE;
    }
}
std::string stateText(std::int8_t state) {
    switch (state) {
    case 0: return "TRK SCANNING";
    case 1: return "TRK FOUND";
    case 2: return "TRK TRACKING";
    case 3: return "TARGET LOST";
    default: return "TRK --";
    }
}
void fill(cv::Mat &image, cv::Rect area, cv::Scalar color) {
    area &= cv::Rect{0, 0, image.cols, image.rows};
    if (!area.empty()) { image(area).setTo(color); }
}
void line(cv::Mat &image, cv::Point from, cv::Point to, cv::Scalar color, int thickness) {
    int dx{std::abs(to.x - from.x)}, sx{from.x < to.x ? 1 : -1};
    int dy{-std::abs(to.y - from.y)}, sy{from.y < to.y ? 1 : -1};
    int error{dx + dy};
    while (true) {
        fill(image, {from.x - thickness / 2, from.y - thickness / 2, thickness, thickness}, color);
        if (from == to) { break; }
        const int twice{2 * error};
        if (twice >= dy) { error += dy; from.x += sx; }
        if (twice <= dx) { error += dx; from.y += sy; }
    }
}
void bitmapText(cv::Mat &image, int x, int y, const std::string &value, cv::Scalar color, bool small = false) {
    x += 2;
    y += 2;
    const int glyph_width{small ? FONT_W_SMALL : FONT_W};
    const int glyph_height{small ? FONT_H_SMALL : FONT_H};
    for (unsigned char source : value) {
        const int index{(source >= FONT_FIRST && source < FONT_FIRST + FONT_COUNT)
            ? source - FONT_FIRST : '?' - FONT_FIRST};
        const unsigned char *atlas{small ? FONT_ATLAS_SMALL[index] : FONT_ATLAS[index]};
        // 与参考项目相同：阈值 48，向四周扩 2 像素作黑描边；彩色核心压成二值 alpha。
        for (int gy{-2}; gy < glyph_height + 2; ++gy) {
            for (int gx{-2}; gx < glyph_width + 2; ++gx) {
                const int px{x + gx}, py{y + gy};
                if (px < 0 || py < 0 || px >= image.cols || py >= image.rows) { continue; }
                bool outline{};
                for (int oy{-2}; oy <= 2 && !outline; ++oy) {
                    for (int ox{-2}; ox <= 2; ++ox) {
                        const int ax{gx - ox}, ay{gy - oy};
                        if (ax >= 0 && ax < glyph_width && ay >= 0 && ay < glyph_height
                            && atlas[ay * glyph_width + ax] >= 48) {
                            outline = true;
                            break;
                        }
                    }
                }
                if (outline) { image.at<cv::Vec4b>(py, px) = {0, 0, 0, 255}; }
            }
        }
        for (int gy{0}; gy < glyph_height; ++gy) {
            for (int gx{0}; gx < glyph_width; ++gx) {
                const int px{x + gx}, py{y + gy};
                if (px < 0 || py < 0 || px >= image.cols || py >= image.rows) { continue; }
                const unsigned int alpha{atlas[gy * glyph_width + gx]};
                if (alpha == 0) { continue; }
                cv::Vec4b &pixel{image.at<cv::Vec4b>(py, px)};
                for (int channel{0}; channel < 3; ++channel) {
                    pixel[channel] = static_cast<unsigned char>((alpha * color[channel]
                        + (255 - alpha) * pixel[channel]) / 255);
                }
                pixel[3] = 255;
            }
        }
        x += glyph_width;
    }
}
void chinese(cv::Mat &image, int y, const std::u32string &message) {
    if (message.empty()) { return; }
    const int width{static_cast<int>(message.size()) * 24};
    int x{(image.cols - width) / 2};
    fill(image, {x - 4, y - 4, width + 8, 32}, BLACK);
    for (char32_t character : message) {
        const auto glyph{std::find_if(std::begin(STATUS_GLYPHS), std::end(STATUS_GLYPHS),
            [character](const StatusGlyph &candidate) { return candidate.codepoint == character; })};
        if (glyph == std::end(STATUS_GLYPHS)) { throw std::logic_error("OSD missing fixed status glyph"); }
        for (int row{0}; row < 24; ++row) {
            for (int column{0}; column < 24; ++column) {
                if ((glyph->rows[row] & (1U << column)) && x + column >= 0 && x + column < image.cols
                    && y + row >= 0 && y + row < image.rows) {
                    image.at<cv::Vec4b>(y + row, x + column) = {60, 200, 255, 255};
                }
            }
        }
        x += 24;
    }
}
void corner(cv::Mat &image, int x, int y, int sx, int sy, int length, int thickness, cv::Scalar color) {
    fill(image, {sx > 0 ? x : x - length + 1, y, length, thickness}, color);
    fill(image, {x, sy > 0 ? y : y - length + 1, thickness, length}, color);
}
struct Element {
    std::string id{}, key{};
    cv::Rect bounds{};
    bool target{};
    std::function<void(cv::Mat &, cv::Point)> paint{};
};
using Elements = std::vector<Element>;
void addText(Elements &elements, std::string id, int x, int y, std::string value, cv::Scalar color,
             bool small = false) {
    const int glyph_width{small ? FONT_W_SMALL : FONT_W};
    const int glyph_height{small ? FONT_H_SMALL : FONT_H};
    const cv::Rect area{x, y, static_cast<int>(value.size()) * glyph_width + 4, glyph_height + 4};
    const std::string key{value + "/" + std::to_string(color[0]) + "/" + std::to_string(color[1])
        + "/" + std::to_string(color[2])};
    elements.push_back({std::move(id), key, area, false,
        [x, y, value = std::move(value), color, small](cv::Mat &image, cv::Point origin) {
            bitmapText(image, x - origin.x, y - origin.y, value, color, small);
        }});
}
void addGraphic(Elements &elements, std::string id, std::string key, cv::Rect bounds,
                std::function<void(cv::Mat &, cv::Point)> paint, bool target = false) {
    elements.push_back({std::move(id), std::move(key), bounds, target, std::move(paint)});
}
std::string optionalInt(std::optional<double> value, const char *pattern, const char *missing) {
    if (!value || !std::isfinite(*value)) { return missing; }
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), pattern, static_cast<int>(std::lround(*value)));
    return buffer;
}
std::string satelliteText(const GpsStatus &gps) {
    const std::string count{gps.satellites ? std::to_string(*gps.satellites) : "--"};
    const std::string fix{gps.fix_type ? (*gps.fix_type >= 3 ? "3D" : *gps.fix_type == 2 ? "2D" : "--") : "--"};
    return "SAT " + count + " " + fix;
}
std::string warningText(const TelemetrySnapshot &telemetry) {
    if (telemetry.warning == "FAILSAFE") { return "FAILSAFE"; }
    if (telemetry.warning == "BATTERY CRITICAL") { return "CRITICAL"; }
    if (telemetry.warning == "BATTERY LOW") { return "LOW BATT"; }
    if (telemetry.warning == "CONNECTION LOST" || telemetry.warning == "CONNECTION UNKNOWN") {
        return "NO LINK";
    }
    return "";
}
void addTarget(Elements &elements, const PresentationState &state, const VideoGeometry &geometry,
               int width, int height) {
    if (state.status != DisplayStatus::Live || state.boxes.empty()) { return; }
    const auto mapped{geometry.mapBox(state.boxes.front())};
    if (!mapped) { return; }
    const cv::Rect box{mapped->x, mapped->y, mapped->width, mapped->height};
    const cv::Rect video{geometry.destination.x, geometry.destination.y,
        geometry.destination.width, geometry.destination.height};
    const cv::Rect canvas{0, 0, width, height};
    if ((box & video & canvas).empty()) { return; }
    const std::string label{targetLabelText(state)};
    const int label_width{static_cast<int>(label.size()) * FONT_W + 12};
    int plate_x{box.x};
    if (plate_x + label_width > width) { plate_x = box.x + box.width - label_width; }
    plate_x = std::max(0, plate_x);
    const int plate_y{box.y >= FONT_H + 8 ? box.y - FONT_H - 8 : box.y + box.height + 2};
    const cv::Rect plate{plate_x, plate_y, label_width, FONT_H + 6};
    const bool locked{state.tracker_state == 2};
    const cv::Rect footprint{(box | plate) + cv::Size{16, 16} + cv::Point{-8, -8}};
    const cv::Rect clipped{footprint & video & canvas};
    const cv::Scalar color{stateColor(state.tracker_state)};
    const std::string key{std::to_string(box.x) + "/" + std::to_string(box.y) + "/"
        + std::to_string(box.width) + "/" + std::to_string(box.height) + "/"
        + label + "/" + std::to_string(state.tracker_state)};
    addGraphic(elements, "target", key, clipped,
        [box, plate, label, color, locked](cv::Mat &image, cv::Point origin) {
            const cv::Rect b{box - origin};
            const cv::Rect p{plate - origin};
            const int thickness{locked ? 3 : 2};
            const int length{std::clamp(std::min(box.width, box.height) / 3, 10, 36)};
            corner(image, b.x, b.y, 1, 1, length, thickness, color);
            corner(image, b.x + b.width - thickness, b.y, -1, 1, length, thickness, color);
            corner(image, b.x, b.y + b.height - thickness, 1, -1, length, thickness, color);
            corner(image, b.x + b.width - thickness, b.y + b.height - thickness,
                -1, -1, length, thickness, color);
            if (locked) {
                const cv::Rect outer{b.x - 6, b.y - 6, b.width + 12, b.height + 12};
                corner(image, outer.x, outer.y, 1, 1, 12, 2, color);
                corner(image, outer.x + outer.width - 2, outer.y, -1, 1, 12, 2, color);
                corner(image, outer.x, outer.y + outer.height - 2, 1, -1, 12, 2, color);
                corner(image, outer.x + outer.width - 2, outer.y + outer.height - 2, -1, -1, 12, 2, color);
                const cv::Point center{b.x + b.width / 2, b.y + b.height / 2};
                line(image, center + cv::Point{-8, 0}, center + cv::Point{0, -8}, color, 2);
                line(image, center + cv::Point{0, -8}, center + cv::Point{8, 0}, color, 2);
                line(image, center + cv::Point{8, 0}, center + cv::Point{0, 8}, color, 2);
                line(image, center + cv::Point{0, 8}, center + cv::Point{-8, 0}, color, 2);
            }
            fill(image, p, {color[0] / 2, color[1] / 2, color[2] / 2, 255});
            fill(image, {p.x, p.y, p.width, 2}, color);
            fill(image, {p.x, p.y + p.height - 2, p.width, 2}, color);
            fill(image, {p.x, p.y, 2, p.height}, color);
            fill(image, {p.x + p.width - 2, p.y, 2, p.height}, color);
            bitmapText(image, p.x + 6, p.y + 3, label, WHITE);
        }, true);
}
Elements scene(const PresentationState &state, const VideoGeometry &geometry, int width, int height,
               std::chrono::system_clock::time_point wall_time, const std::string &craft) {
    Elements elements{};
    addGraphic(elements, "corners", "static", {0, 0, WIDTH, HEIGHT}, [](cv::Mat &image, cv::Point origin) {
        corner(image, 16 - origin.x, 16 - origin.y, 1, 1, 30, 2, WHITE);
        corner(image, 703 - origin.x, 16 - origin.y, -1, 1, 30, 2, WHITE);
        corner(image, 16 - origin.x, 463 - origin.y, 1, -1, 30, 2, WHITE);
        corner(image, 703 - origin.x, 463 - origin.y, -1, -1, 30, 2, WHITE);
    });
    addGraphic(elements, "reticle", "static", {325, 205, 70, 70}, [](cv::Mat &image, cv::Point origin) {
        const int cx{360 - origin.x}, cy{240 - origin.y};
        fill(image, {cx - 44, cy - 1, 33, 2}, RED);
        fill(image, {cx + 11, cy - 1, 33, 2}, RED);
        fill(image, {cx - 1, cy - 44, 2, 33}, RED);
        fill(image, {cx - 1, cy + 11, 2, 33}, RED);
        for (int angle{0}; angle < 360; angle += 3) {
            const double radians{angle * RADIANS};
            fill(image, {cx + static_cast<int>(22 * std::cos(radians)),
                cy + static_cast<int>(22 * std::sin(radians)), 1, 1}, RED);
        }
        fill(image, {cx - 2, cy - 2, 4, 4}, RED);
    });
    addText(elements, "craft", 22, 10, craft.substr(0, 24), WHITE);
    addText(elements, "subtitle", 22, 48, "RK3576 OSD NTSC", CYAN, true);
    const auto &telemetry{state.telemetry};
    const Battery battery{telemetry.battery.value.value_or(Battery{})};
    const GpsStatus gps{telemetry.gps.value.value_or(GpsStatus{})};
    std::string mode{telemetry.flight.value ? telemetry.flight.value->mode.substr(0, 9) : "--"};
    mode.insert(0, 9 - mode.size(), ' ');
    addText(elements, "mode", 554, 10, mode,
        telemetry.flight.value && telemetry.flight.value->mode == "OFFBOARD" ? CYAN : AMBER);
    addText(elements, "tracker", 506, 48, statusBarText(state),
        state.mission_active ? CYAN : stateColor(state.tracker_state));
    addText(elements, "timer", 570, 86, short_text::armedTime(telemetry.armed_seconds), CYAN);
    const cv::Scalar battery_color{telemetry.battery_warning.value && *telemetry.battery_warning.value == 3 ? RED
        : telemetry.battery_warning.value && *telemetry.battery_warning.value == 2 ? AMBER : WHITE};
    addText(elements, "battery", 554, 162, short_text::voltage(battery.voltage), battery_color);
    const std::optional<double> cell{battery.cell_voltage};
    char cell_buffer[32]{};
    if (cell) { std::snprintf(cell_buffer, sizeof(cell_buffer), "CELL %4.2f", *cell); }
    addText(elements, "cell", 554, 200, cell ? cell_buffer : "CELL --", battery_color);
    addText(elements, "current", 554, 238, short_text::current(battery.current_amp), WHITE);
    addText(elements, "used", 554, 276, short_text::milliampHours(battery.consumed_mah), WHITE);
    addText(elements, "altitude", 22, 118, optionalInt(telemetry.altitude_m, "ALT %04dM", "ALT --M"), WHITE);
    addText(elements, "speed", 22, 156, short_text::speedKmh(gps.speed_mps), WHITE);
    addText(elements, "heading", 22, 194,
        optionalInt(telemetry.attitude.value ? std::optional<double>{telemetry.attitude.value->heading_deg}
            : std::nullopt, "HDG %03d", "HDG --"), WHITE);
    addText(elements, "satellites", 22, 232, satelliteText(gps),
        gps.fix_type && *gps.fix_type >= 2 ? WHITE : AMBER);
    addText(elements, "temperature", 22, 270,
        optionalInt(telemetry.temperature_c.value, "TMP %+03dC", "TMP --C"), WHITE, true);
    addText(elements, "home-distance", 312, 86,
        telemetry.home_distance_m && telemetry.home_relative_deg
            ? optionalInt(telemetry.home_distance_m, "%3dM", " --") : " --", AMBER);
    if (telemetry.home_distance_m && telemetry.home_relative_deg) {
        const double angle{*telemetry.home_relative_deg * RADIANS};
        addGraphic(elements, "home-arrow", std::to_string(*telemetry.home_relative_deg), {328, 32, 64, 52},
            [angle](cv::Mat &image, cv::Point origin) {
                const auto rotate{[&](double x, double y) {
                    return cv::Point{360 - origin.x + static_cast<int>(x * std::cos(angle) - y * std::sin(angle)),
                        58 - origin.y + static_cast<int>(x * std::sin(angle) + y * std::cos(angle))};
                }};
                const cv::Point tip{rotate(0, -22)}, left{rotate(-11, 11)}, right{rotate(11, 11)};
                const cv::Point tail{rotate(0, 5)};
                line(image, tip, left, AMBER, 2); line(image, tip, right, AMBER, 2);
                line(image, left, tail, AMBER, 2); line(image, right, tail, AMBER, 2);
            });
    }
    if (telemetry.attitude.value) {
        const Attitude attitude{*telemetry.attitude.value};
        addGraphic(elements, "horizon", std::to_string(attitude.pitch_deg) + "/"
            + std::to_string(attitude.roll_deg), {232, 140, 256, 200},
            [attitude](cv::Mat &image, cv::Point origin) {
                const float angle{static_cast<float>(-attitude.roll_deg * 3.14159265f / 180.0f)};
                const float cosine{std::cos(angle)}, sine{std::sin(angle)};
                for (int rung{-20}; rung <= 20; rung += 5) {
                    if (rung == 0) { continue; }
                    const float dy{static_cast<float>((rung - attitude.pitch_deg) * 5)};
                    if (std::abs(dy) > 90) { continue; }
                    const int length{rung % 10 == 0 ? 80 : 40};
                    const cv::Point first{360 - origin.x + static_cast<int>(-length * cosine - dy * sine),
                        240 - origin.y + static_cast<int>(-length * sine + dy * cosine)};
                    const cv::Point second{360 - origin.x + static_cast<int>(length * cosine - dy * sine),
                        240 - origin.y + static_cast<int>(length * sine + dy * cosine)};
                    line(image, first, second, WHITE, 2);
                }
            });
    }
    if (battery.percentage && *battery.percentage >= 0 && *battery.percentage <= 1) {
        const double remaining{*battery.percentage};
        addGraphic(elements, "battery-bar", std::to_string(remaining), {16, 310, 260, 54},
            [remaining](cv::Mat &image, cv::Point origin) {
                fill(image, {20 - origin.x, 330 - origin.y, 150, 12}, BLACK);
                fill(image, {21 - origin.x, 331 - origin.y,
                    static_cast<int>(148 * remaining), 10}, remaining < 0.25 ? RED : AMBER);
                char value[16]{};
                std::snprintf(value, sizeof(value), "%3d%%", static_cast<int>(remaining * 100));
                bitmapText(image, 178 - origin.x, 318 - origin.y, value, WHITE);
            });
    }
    addText(elements, "clock", 22, 390, short_text::localClock(wall_time), AMBER);
    addText(elements, "frame", 22, 430,
        short_text::frameNumber(state.has_video ? std::optional<std::uint64_t>{state.sequence} : std::nullopt),
        WHITE, true);
    const auto status{OsdLayout::statusText(state.status, state.has_video)};
    if (!status.empty()) {
        addGraphic(elements, "status", std::to_string(static_cast<int>(state.status))
            + (state.has_video ? "v" : "n"), {0, 386, WIDTH, 36},
            [status](cv::Mat &image, cv::Point origin) { chinese(image, 4 - origin.y + 386, status); });
    } else {
        addText(elements, "message", 200, 390, telemetry.warning.substr(0, 20),
            telemetry.warning == "FAILSAFE" || telemetry.warning == "BATTERY CRITICAL" ? RED : BLUE);
    }
    const auto milliseconds{std::chrono::duration_cast<std::chrono::milliseconds>(wall_time.time_since_epoch()).count()};
    if (!state.trigger.empty()) { addText(elements, "trigger", 296, 430, state.trigger, CYAN); }
    else if (telemetry.flight.value && !telemetry.flight.value->armed) {
        addText(elements, "arm", 296, 430, "DISARMED", WHITE);
    } else if (telemetry.flight.value && telemetry.flight.value->armed && telemetry.armed_seconds
        && *telemetry.armed_seconds < 3 && ((milliseconds / 300) % 2 == 0)) {
        addText(elements, "arm", 296, 430, "ARMED", GREEN);
    }
    const std::string warning{warningText(telemetry)};
    if (!warning.empty() && ((milliseconds / 300) % 2 == 0)) {
        addText(elements, "warning", 570, 430, warning,
            warning == "NO LINK" ? AMBER : RED);
    }
    addTarget(elements, state, geometry, width, height);
    return elements;
}
void overlay(cv::Mat &destination, const cv::Mat &source, cv::Rect bounds, cv::Rect area = {}) {
    if (area.empty()) { area = bounds; }
    const cv::Rect clipped{bounds & area & cv::Rect{0, 0, destination.cols, destination.rows}};
    for (int y{clipped.y}; y < clipped.y + clipped.height; ++y) {
        for (int x{clipped.x}; x < clipped.x + clipped.width; ++x) {
            const auto pixel{source.at<cv::Vec4b>(y - bounds.y, x - bounds.x)};
            if (pixel[3]) { destination.at<cv::Vec4b>(y, x) = pixel; }
        }
    }
}
cv::Rect scaledBounds(cv::Rect area, int width, int height) {
    const auto scale{[](int value, int target, int source) {
        return static_cast<int>(std::ceil(static_cast<double>(value) * target / source));
    }};
    const int x{scale(area.x, width, WIDTH)}, y{scale(area.y, height, HEIGHT)};
    return {x, y, scale(area.x + area.width, width, WIDTH) - x,
        scale(area.y + area.height, height, HEIGHT) - y};
}
cv::Mat elementPixels(const Element &element, int width, int height, cv::Rect &bounds) {
    cv::Mat source{element.bounds.height, element.bounds.width, CV_8UC4, cv::Scalar{}};
    element.paint(source, element.bounds.tl());
    if (element.target) { bounds = element.bounds; return source; }
    bounds = scaledBounds(element.bounds, width, height);
    cv::Mat output{bounds.height, bounds.width, CV_8UC4, cv::Scalar{}};
    for (int y{0}; y < output.rows; ++y) {
        const int sy{std::clamp((y + bounds.y) * HEIGHT / height - element.bounds.y, 0, source.rows - 1)};
        for (int x{0}; x < output.cols; ++x) {
            const int sx{std::clamp((x + bounds.x) * WIDTH / width - element.bounds.x, 0, source.cols - 1)};
            output.at<cv::Vec4b>(y, x) = source.at<cv::Vec4b>(sy, sx);
        }
    }
    return output;
}
} // namespace

std::string statusBarText(const PresentationState &state) {
    return state.mission_active ? "ON MISSION" : stateText(state.tracker_state);
}

std::string targetLabelText(const PresentationState &state) {
    if (state.status != DisplayStatus::Live || state.boxes.empty()) { return {}; }
    return state.boxes.front().label.empty() ? stateText(state.tracker_state)
        : state.boxes.front().label.substr(0, 24);
}

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
    if (width <= 0 || height <= 0) { throw std::invalid_argument("OSD invalid canvas"); }
    DisplayLayers output{cv::Mat{height, width, CV_8UC4, cv::Scalar{}},
        cv::Mat{height, width, CV_8UC4, cv::Scalar{}}};
    for (const Element &element : scene(state, geometry, width, height, wall_time, craft_name_)) {
        cv::Rect bounds{};
        const cv::Mat pixels{elementPixels(element, width, height, bounds)};
        overlay(element.target ? output.target : output.hud, pixels, bounds);
    }
    return output;
}
struct CachedOsdLayout::Cache {
    struct Field { std::string key{}; cv::Rect bounds{}; cv::Mat pixels{}; bool used{}; };
    std::string craft{};
    cv::Mat canvas{};
    std::map<std::string, Field> fields{};
    std::vector<std::string> order{};
    std::vector<cv::Rect> dirty{};
    LayoutWork counters{};
    std::uint64_t round{~std::uint64_t{0}};
    VideoGeometry geometry{};
    bool theme_changed{true};
};
CachedOsdLayout::CachedOsdLayout(std::string craft_name) : cache_{std::make_unique<Cache>()} {
    cache_->craft = std::move(craft_name);
}
CachedOsdLayout::~CachedOsdLayout() = default;
void CachedOsdLayout::setCraftName(std::string craft_name) {
    if (cache_->craft != craft_name) { cache_->craft = std::move(craft_name); cache_->theme_changed = true; }
}
LayoutWork CachedOsdLayout::work() const { return cache_->counters; }
const cv::Mat &CachedOsdLayout::draw(const PresentationState &state, const VideoGeometry &geometry,
                                    int width, int height, std::chrono::system_clock::time_point wall_time) {
    if (width <= 0 || height <= 0) { throw std::invalid_argument("OSD invalid cached canvas"); }
    Cache &cache{*cache_};
    const auto &old{cache.geometry.destination}, &current{geometry.destination};
    if (cache.theme_changed || cache.canvas.cols != width || cache.canvas.rows != height || cache.round != state.round
        || cache.geometry.source_width != geometry.source_width || cache.geometry.source_height != geometry.source_height
        || old.x != current.x || old.y != current.y || old.width != current.width || old.height != current.height) {
        cache.canvas = cv::Mat{height, width, CV_8UC4, cv::Scalar{}};
        cache.fields.clear();
        cache.geometry = geometry;
        cache.round = state.round;
        cache.theme_changed = false;
        ++cache.counters.rebuilds;
    }
    cache.order.clear();
    cache.dirty.clear();
    if (cache.fields.empty()) { cache.dirty.push_back({0, 0, width, height}); }
    for (auto &field : cache.fields) { field.second.used = false; }
    for (const Element &element : scene(state, geometry, width, height, wall_time, cache.craft)) {
        cache.order.push_back(element.id);
        Cache::Field &field{cache.fields[element.id]};
        field.used = true;
        const cv::Rect bounds{element.target ? element.bounds : scaledBounds(element.bounds, width, height)};
        if (field.key == element.key && field.bounds == bounds && !field.pixels.empty()) { continue; }
        if (!field.bounds.empty()) { cache.dirty.push_back(field.bounds); }
        cache.dirty.push_back(bounds);
        field.key = element.key;
        field.pixels = elementPixels(element, width, height, field.bounds);
        ++cache.counters.fields;
        if (element.id == "craft" || element.id == "subtitle" || element.id == "target"
            || element.id == "message" || element.id == "warning") {
            cache.counters.glyphs += element.key.size();
        }
    }
    for (auto it{cache.fields.begin()}; it != cache.fields.end();) {
        if (it->second.used) { ++it; }
        else { cache.dirty.push_back(it->second.bounds); it = cache.fields.erase(it); }
    }
    for (std::size_t i{0}; i < cache.dirty.size(); ++i) {
        for (std::size_t j{i + 1}; j < cache.dirty.size();) {
            if (!(cache.dirty[i] & cache.dirty[j]).empty()) {
                cache.dirty[i] |= cache.dirty[j];
                cache.dirty.erase(cache.dirty.begin() + j);
                j = i + 1;
            } else { ++j; }
        }
    }
    for (cv::Rect area : cache.dirty) {
        area &= cv::Rect{0, 0, width, height};
        if (area.empty()) { continue; }
        cache.canvas(area).setTo(cv::Scalar{});
        for (const std::string &id : cache.order) {
            const auto &field{cache.fields.at(id)};
            const cv::Rect overlap{area & field.bounds};
            if (overlap.empty()) { continue; }
            overlay(cache.canvas, field.pixels, field.bounds, overlap);
        }
        cache.counters.dirty_pixels += static_cast<std::uint64_t>(area.area());
    }
    return cache.canvas;
}
} // namespace osd
