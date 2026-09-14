#include "video_timing.h"

#include <limits>
#include <stdexcept>

namespace rk3576 {
std::int64_t historyBase(const std::vector<std::pair<std::string, std::string>> &metadata) {
    std::int64_t result{};
    bool found{};
    for (const auto &tag : metadata) {
        std::string key{tag.first};
        for (char &letter : key) {
            if (letter >= 'A' && letter <= 'Z') {
                letter = static_cast<char>(letter + ('a' - 'A'));
            }
        }
        if (key != "artist") {
            continue;
        }
        if (found || tag.second.empty()) {
            throw std::runtime_error("Missing or ambiguous Artist timestamp");
        }
        found = true;
        for (char digit : tag.second) {
            if (digit < '0' || digit > '9'
                || result > (std::numeric_limits<std::int64_t>::max() - (digit - '0')) / 10) {
                throw std::runtime_error("Artist must be positive decimal int64 microseconds");
            }
            result = result * 10 + (digit - '0');
        }
    }
    if (!found || result <= 0) {
        throw std::runtime_error("Missing or nonpositive Artist timestamp");
    }
    return result;
}
std::int64_t videoPtsUs(std::int64_t pts, bool present, std::pair<int, int> time_base, std::int64_t previous) {
    if (!present || pts < 0 || time_base.first <= 0 || time_base.second <= 0) {
        throw std::runtime_error("Missing/negative PTS or invalid time base");
    }
    // int64 × int32 × 10^6 在 int128 内精确，避免浮点舍入和饱和换算掩盖溢出。
    const __int128 scaled{static_cast<__int128>(pts) * time_base.first * 1000000};
    const __int128 rounded{(scaled + time_base.second / 2) / time_base.second};
    if (rounded > std::numeric_limits<std::int64_t>::max() || rounded <= previous) {
        throw std::runtime_error("PTS microseconds overflow or are not strictly increasing");
    }
    return static_cast<std::int64_t>(rounded);
}
std::int64_t videoTimestamp(std::int64_t base, std::int64_t pts_us) {
    if (base <= 0 || pts_us < 0 || base > std::numeric_limits<std::int64_t>::max() - pts_us) {
        throw std::runtime_error("Frame timestamp outside positive int64 microseconds");
    }
    return base + pts_us;
}
std::int64_t VideoClock::systemUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
VideoClock::TimePoint VideoClock::steadyNow() {
    return std::chrono::steady_clock::now();
}
bool VideoClock::waitUntil(TimePoint deadline, const std::atomic<bool> &stop) {
    std::unique_lock<std::mutex> lock{mutex_};
    return !condition_.wait_until(lock, deadline, [&stop] { return stop.load(); });
}
void VideoClock::wake() {
    // 与 waitUntil 的检查/入睡交接同步，避免停止通知丢失。
    std::lock_guard<std::mutex> lock{mutex_};
    condition_.notify_all();
}
VideoClock::TimePoint videoDeadline(VideoClock::TimePoint origin, std::int64_t pts_us) {
    using Duration = VideoClock::TimePoint::duration;
    using Period = Duration::period;
    const __int128 ticks{static_cast<__int128>(pts_us) * Period::den
        / (static_cast<__int128>(1000000) * Period::num)};
    const __int128 deadline{static_cast<__int128>(origin.time_since_epoch().count()) + ticks};
    if (pts_us < 0 || deadline > std::numeric_limits<Duration::rep>::max()
        || deadline < std::numeric_limits<Duration::rep>::min()) {
        throw std::runtime_error("Playback deadline overflow");
    }
    return VideoClock::TimePoint{Duration{static_cast<Duration::rep>(deadline)}};
}
} // namespace rk3576
