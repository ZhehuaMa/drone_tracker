#include "osd/layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <map>
#include <functional>
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

namespace osd {
struct CachedOsdLayout::Cache {
    struct Glyph { cv::Mat outline{}, fill{}; int advance{}; };
    struct Field { std::string key{}; cv::Rect bounds{}; cv::Mat pixels{}; bool used{}; };
    std::string craft{};
    cv::Mat canvas{}, base{};
    std::map<std::pair<unsigned char, int>, Glyph> glyphs{};
    std::map<std::string, Field> fields{};
    std::vector<std::string> order{};
    std::vector<cv::Rect> dirty{};
    std::uint64_t round{~std::uint64_t{0}};
    VideoGeometry geometry{};
    LayoutWork counters{};
    bool theme_changed{true};

    const Glyph &glyph(unsigned char c, int prefix_advance = 0) {
        if (c < 32 || c > 126) { c = '?'; }
        // OpenCV Hershey 文本使用 16 位小数累计笔位置。保留这个相位，避免逐字取整造成字距漂移。
        constexpr int scale{31457}; // round(0.48 * 65536)，与 putText 一致。
        const int phase{(prefix_advance * scale) & 65535};
        const auto key{std::make_pair(c, phase)};
        const auto found{glyphs.find(key)};
        if (found != glyphs.end()) { return found->second; }
        int baseline{};
        const int advance{cv::getTextSize(std::string(1, c), cv::FONT_HERSHEY_SIMPLEX, 1, 0, &baseline).width};
        Glyph value{cv::Mat::zeros(28, advance + 10, CV_8UC1),
                    cv::Mat::zeros(28, advance + 10, CV_8UC1), advance};
        // 空格前缀不产生像素，只移动笔位置；选择最接近原相位的整数前缀。
        // 最大相位误差不足 1/4096 像素；固定字形结果由布局基准测试约束。
        const int space{cv::getTextSize(" ", cv::FONT_HERSHEY_SIMPLEX, 1, 0, &baseline).width};
        int prefix{}, distance{65536};
        for (int candidate{0}; candidate < 4096; ++candidate) {
            const int candidate_phase{static_cast<int>((static_cast<std::int64_t>(candidate) * space * scale) & 65535)};
            const int difference{std::abs(candidate_phase - phase)};
            if (difference < distance) { distance = difference; prefix = candidate; }
            if (distance == 0) { break; }
        }
        const std::string run{std::string(prefix, ' ') + static_cast<char>(c)};
        const cv::Point origin{4 - static_cast<int>((static_cast<std::int64_t>(prefix) * space * scale) >> 16), 20};
        cv::putText(value.outline, run, origin, cv::FONT_HERSHEY_SIMPLEX, 0.48, 255, 3, cv::LINE_8);
        cv::putText(value.fill, run, origin, cv::FONT_HERSHEY_SIMPLEX, 0.48, 255, 1, cv::LINE_8);
        ++counters.glyphs;
        return glyphs.emplace(key, std::move(value)).first->second;
    }
    void mark(cv::Rect area) {
        area &= cv::Rect{0, 0, canvas.cols, canvas.rows};
        if (!area.empty()) { dirty.push_back(area); }
    }
    void field(const std::string &id, const std::string &key, cv::Rect bounds,
               const std::function<cv::Mat()> &paint) {
        order.push_back(id);
        auto &entry{fields[id]};
        entry.used = true;
        if (entry.key == key && entry.bounds == bounds && !entry.pixels.empty()) { return; }
        mark(entry.bounds); mark(bounds);
        entry.key = key; entry.bounds = bounds; entry.pixels = paint();
        ++counters.fields;
    }
    // 逻辑 HUD 固定为 720×480；缩放矩形使用全局最近邻采样，避免局部缩放接缝。
    void logical(const std::string &id, const std::string &key, cv::Rect rectangle,
                 const std::function<void(cv::Mat &, cv::Point)> &paint) {
        const auto ceilScale{[](int x, int target, int source) {
            return static_cast<int>(std::ceil(static_cast<double>(x) * target / source));
        }};
        const cv::Rect bounds{ceilScale(rectangle.x, canvas.cols, 720), ceilScale(rectangle.y, canvas.rows, 480),
            ceilScale(rectangle.x + rectangle.width, canvas.cols, 720) - ceilScale(rectangle.x, canvas.cols, 720),
            ceilScale(rectangle.y + rectangle.height, canvas.rows, 480) - ceilScale(rectangle.y, canvas.rows, 480)};
        field(id, key, bounds, [&] {
            cv::Mat source{rectangle.height, rectangle.width, CV_8UC4, cv::Scalar{}};
            paint(source, rectangle.tl());
            cv::Mat scaled{bounds.height, bounds.width, CV_8UC4, cv::Scalar{}};
            for (int y{0}; y < scaled.rows; ++y) {
                const int sy{std::clamp((y + bounds.y) * 480 / canvas.rows - rectangle.y, 0, source.rows - 1)};
                for (int x{0}; x < scaled.cols; ++x) {
                    const int sx{std::clamp((x + bounds.x) * 720 / canvas.cols - rectangle.x, 0, source.cols - 1)};
                    scaled.at<cv::Vec4b>(y, x) = source.at<cv::Vec4b>(sy, sx);
                }
            }
            return scaled;
        });
    }
    void glyphRun(cv::Mat &image, int x, int y, const std::string &value, cv::Scalar color) {
        for (bool outline : {true, false}) {
            int offset{};
            for (unsigned char character : value) {
                const auto &g{glyph(character, offset)};
                const auto &mask{outline ? g.outline : g.fill};
                const cv::Point origin{x - 4 + static_cast<int>((static_cast<std::int64_t>(offset) * 31457) >> 16), y - 20};
                const cv::Rect dst{cv::Rect{origin.x, origin.y, mask.cols, mask.rows}
                                   & cv::Rect{0, 0, image.cols, image.rows}};
                if (!dst.empty()) {
                    image(dst).setTo(outline ? cv::Scalar{0, 0, 0, 255} : color,
                        mask(cv::Rect{dst.x - origin.x, dst.y - origin.y, dst.width, dst.height}));
                }
                offset += g.advance;
            }
        }
    }
    void label(const std::string &id, int x, int y, const std::string &value, cv::Scalar color = WHITE) {
        int advance{};
        for (unsigned char c : value) { advance += glyph(c, advance).advance; }
        const cv::Rect rectangle{x - 4, y - 20, static_cast<int>(std::ceil(advance * 0.48)) + 10, 28};
        const std::string key{value + "/" + std::to_string(color[0]) + "/" + std::to_string(color[1])
                              + "/" + std::to_string(color[2])};
        logical(id, key, rectangle, [&](cv::Mat &image, cv::Point) {
            glyphRun(image, 4, 20, value, color);
        });
    }
    void composite(const Field &field, const cv::Rect &area) {
        const auto region{area & field.bounds};
        for (int y{region.y}; y < region.y + region.height; ++y) {
            auto *dst{canvas.ptr<cv::Vec4b>(y)};
            const auto *src{field.pixels.ptr<cv::Vec4b>(y - field.bounds.y)};
            for (int x{region.x}; x < region.x + region.width; ++x) {
                const auto value{src[x - field.bounds.x]};
                if (value[3]) { dst[x] = value; }
            }
        }
    }
    void finish() {
        for (auto it{fields.begin()}; it != fields.end();) {
            if (!it->second.used) { mark(it->second.bounds); it = fields.erase(it); }
            else { ++it; }
        }
        // 合并相交矩形，避免交叠恢复擦掉刚画好的字段；互不相交的字段仍独立更新。
        for (std::size_t i{0}; i < dirty.size(); ++i) {
            for (std::size_t j{i + 1}; j < dirty.size();) {
                if (!(dirty[i] & dirty[j]).empty()) {
                    dirty[i] |= dirty[j]; dirty.erase(dirty.begin() + j); j = i + 1;
                } else { ++j; }
            }
        }
        for (const auto &area : dirty) {
            base(area).copyTo(canvas(area));
            for (const auto &id : order) { composite(fields.at(id), area); }
            counters.dirty_pixels += static_cast<std::uint64_t>(area.area());
        }
    }
};
CachedOsdLayout::CachedOsdLayout(std::string craft_name) : cache_{std::make_unique<Cache>()} {
    cache_->craft = std::move(craft_name);
}
CachedOsdLayout::~CachedOsdLayout() = default;
void CachedOsdLayout::setCraftName(std::string value) {
    if (cache_->craft != value) { cache_->craft = std::move(value); cache_->theme_changed = true; }
}
LayoutWork CachedOsdLayout::work() const { return cache_->counters; }
const cv::Mat &CachedOsdLayout::draw(const PresentationState &state, const VideoGeometry &geometry,
                                    int width, int height, std::chrono::system_clock::time_point wall_time) {
    if (width <= 0 || height <= 0) { throw std::invalid_argument("OSD invalid cached canvas"); }
    auto &c{*cache_};
    const auto &a{c.geometry.destination}; const auto &b{geometry.destination};
    if (c.theme_changed || c.canvas.cols != width || c.canvas.rows != height || c.round != state.round
        || c.geometry.source_width != geometry.source_width || c.geometry.source_height != geometry.source_height
        || a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height) {
        c.canvas = cv::Mat{height, width, CV_8UC4, cv::Scalar{}};
        cv::Mat logical{480, 720, CV_8UC4, cv::Scalar{}};
        text(logical, 16, 24, c.craft.substr(0, 24));
        cv::line(logical, {345, 240}, {354, 240}, WHITE, 1);
        cv::line(logical, {366, 240}, {375, 240}, WHITE, 1);
        cv::line(logical, {360, 225}, {360, 234}, WHITE, 1);
        cv::line(logical, {360, 246}, {360, 255}, WHITE, 1);
        cv::resize(logical, c.base, {width, height}, 0, 0, cv::INTER_NEAREST);
        c.fields.clear(); c.geometry = geometry; c.round = state.round; c.theme_changed = false;
        ++c.counters.rebuilds;
    }
    c.order.clear(); c.dirty.clear();
    if (c.fields.empty()) { c.mark({0, 0, width, height}); }
    for (auto &entry : c.fields) { entry.second.used = false; }
    const std::array<const char *, 4> states{{"DETECTING", "TARGET FOUND", "TRACKING", "TARGET LOST"}};
    c.label("state", 520, 24, state.tracker_state >= 0 && state.tracker_state < 4
        ? states[state.tracker_state] : "UNKNOWN", stateColor(state.tracker_state));
    const auto &t{state.telemetry};
    c.label("mode", 16, 54, "MODE " + (t.flight.value ? t.flight.value->mode.substr(0, 20) : "--"));
    c.label("armed", 16, 78, t.flight.value ? (t.flight.value->armed ? "ARMED" : "DISARMED") : "ARM --");
    c.label("timer", 16, 102, "T+ " + number(t.armed_seconds, 0) + " s");
    const Battery battery{t.battery.value.value_or(Battery{})};
    c.label("battery", 16, 140, "BAT " + number(battery.voltage) + " V");
    c.label("cell", 16, 164, "CELL " + number(battery.cell_voltage, 2) + " V");
    c.label("current", 16, 188, "CUR " + number(battery.current_amp) + " A");
    c.label("used", 16, 212, "USED " + number(battery.consumed_mah, 0) + " mAh");
    c.label("left", 16, 236, "LEFT " + number(battery.percentage ? std::optional<double>{*battery.percentage * 100}
        : std::nullopt, 0) + " %");
    c.label("altitude", 16, 274, "ALT " + number(t.altitude_m) + " m");
    c.label("temperature", 16, 298, "TEMP " + number(t.temperature_c.value) + " C");
    const GpsStatus gps{t.gps.value.value_or(GpsStatus{})};
    c.label("speed", 520, 54, "SPD " + number(gps.speed_mps) + " m/s");
    c.label("satellites", 520, 78, "SAT " + (gps.satellites ? std::to_string(*gps.satellites) : "--"));
    c.label("heading", 520, 102, "HDG " + number(t.attitude.value
        ? std::optional<double>{t.attitude.value->heading_deg} : std::nullopt, 0));
    c.label("radar", 520, 126, "RADAR " + (t.radar_state.value ? std::to_string(*t.radar_state.value) : "--"));
    c.label("home", 520, 150, "HOME " + number(t.home_distance_m, 0) + " m");
    c.label("latitude", 470, 290, "LAT " + number(t.position.value
        ? std::optional<double>{t.position.value->latitude_deg} : std::nullopt, 6));
    c.label("longitude", 470, 314, "LON " + number(t.position.value
        ? std::optional<double>{t.position.value->longitude_deg} : std::nullopt, 6));
    constexpr double radians{3.14159265358979323846 / 180.0};
    if (t.attitude.value) {
        const double angle{t.attitude.value->roll_deg * radians};
        const int cy{240 + static_cast<int>(std::clamp(t.attitude.value->pitch_deg, -20.0, 20.0) * 3)};
        const int dx{static_cast<int>(70 * std::cos(angle))}, dy{static_cast<int>(70 * std::sin(angle))};
        c.logical("horizon", std::to_string(cy) + "/" + std::to_string(dx) + "/" + std::to_string(dy),
            {286, 106, 148, 268}, [&](cv::Mat &image, cv::Point origin) {
                cv::line(image, cv::Point{360 - dx, cy - dy} - origin,
                         cv::Point{360 + dx, cy + dy} - origin, WHITE, 2);
            });
    } else { c.label("unknown-attitude", 300, 270, "ATTITUDE --"); }
    if (t.home_relative_deg) {
        const double angle{*t.home_relative_deg * radians};
        const cv::Point end{600 + static_cast<int>(25 * std::sin(angle)), 220 - static_cast<int>(25 * std::cos(angle))};
        c.logical("home-arrow", std::to_string(end.x) + "/" + std::to_string(end.y), {570, 190, 60, 60},
            [&](cv::Mat &image, cv::Point origin) { cv::arrowedLine(image, cv::Point{600, 220} - origin, end - origin, AMBER, 2); });
    }
    c.label("warning", 200, 362, t.warning.substr(0, 35), AMBER);
    const auto message{OsdLayout::statusText(state.status, state.has_video)};
    c.logical("status", std::to_string(static_cast<int>(state.status)) + (state.has_video ? "v" : "n"),
        {0, 386, 720, 32}, [&](cv::Mat &image, cv::Point) { chinese(image, 4, message); });
    c.label("frame", 16, 454, "FRAME " + (state.has_video ? std::to_string(state.frame_time_us) : "--"));
    const auto time{std::chrono::system_clock::to_time_t(wall_time)};
    std::tm utc{}; gmtime_r(&time, &utc); std::ostringstream clock{};
    clock << std::put_time(&utc, "%H:%M:%S UTC");
    c.label("clock", 535, 454, clock.str());
    if (state.status == DisplayStatus::Live) {
        std::size_t index{};
        for (const auto &box : state.boxes) {
            const auto mapped{geometry.mapBox(box)};
            if (!mapped) { continue; }
            const auto &r{*mapped};
            const cv::Rect clipped{cv::Rect{r.x, r.y, r.width, r.height} & cv::Rect{0, 0, width, height}};
            if (clipped.empty()) { continue; }
            const cv::Rect video{b.x, b.y, b.width, b.height};
            cv::Rect painted{clipped + cv::Size{20, 20} + cv::Point{-10, -10}};
            if (!box.label.empty()) {
                int baseline{};
                const auto size{cv::getTextSize(box.label.substr(0, 24), cv::FONT_HERSHEY_SIMPLEX, 0.48, 3, &baseline)};
                const int label_y{std::clamp(clipped.y + 18, 18, std::max(18, height - 2))};
                painted |= cv::Rect{clipped.x - 4, label_y - 20, size.width + 10, 28};
            }
            const cv::Rect bounds{painted & video & cv::Rect{0, 0, width, height}};
            const auto color{stateColor(state.tracker_state)};
            const std::string key{std::to_string(clipped.x) + "/" + std::to_string(clipped.y) + "/"
                + std::to_string(clipped.width) + "/" + std::to_string(clipped.height) + "/"
                + std::to_string(state.tracker_state) + "/" + box.label};
            c.field("box-" + std::to_string(index++), key, bounds, [&] {
                cv::Mat image{bounds.height, bounds.width, CV_8UC4, cv::Scalar{}};
                cv::rectangle(image, clipped - bounds.tl(), color, state.tracker_state == 2 ? 3 : 1);
                if (!box.label.empty()) {
                    c.glyphRun(image, clipped.x - bounds.x,
                        std::clamp(clipped.y + 18, 18, std::max(18, height - 2)) - bounds.y, box.label.substr(0, 24), color);
                }
                if (state.tracker_state == 2) {
                    cv::drawMarker(image, cv::Point{clipped.x + clipped.width / 2, clipped.y + clipped.height / 2}
                        - bounds.tl(), color, cv::MARKER_DIAMOND, 12, 2);
                }
                return image;
            });
        }
    }
    c.finish();
    return c.canvas;
}
} // namespace osd
