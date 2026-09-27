#include "osd/short_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

namespace osd::short_text {
namespace {
std::string fixed(std::optional<double> value, const char *pattern, const char *missing) {
    if (!value || !std::isfinite(*value)) { return missing; }
    char output[64]{};
    std::snprintf(output, sizeof(output), pattern, *value);
    return output;
}
} // namespace
std::string speedKmh(std::optional<double> value) {
    if (!value || !std::isfinite(*value) || *value < 0) { return "SPD --KMH"; }
    const int speed{static_cast<int>(std::min(999.0, std::floor(*value * 3.6 + 0.5)))};
    char output[32]{};
    std::snprintf(output, sizeof(output), "SPD %03dKMH", speed);
    return output;
}
std::string voltage(std::optional<double> value) {
    return fixed(value, "BAT %04.1fV", "BAT --V");
}
std::string current(std::optional<double> value) {
    return fixed(value && std::isfinite(*value) ? std::optional<double>{std::fabs(*value)} : std::nullopt,
        "CUR %04.1fA", "CUR --A");
}
std::string milliampHours(std::optional<double> value) {
    if (!value || !std::isfinite(*value) || *value < 0) { return "MAH --"; }
    const auto amount{static_cast<unsigned long long>(std::min(*value + 0.5,
        static_cast<double>(std::numeric_limits<unsigned int>::max())))};
    char output[32]{};
    std::snprintf(output, sizeof(output), "MAH %05llu", amount);
    return output;
}
std::string armedTime(std::optional<double> value) {
    if (!value || !std::isfinite(*value) || *value < 0) { return "T+--:--"; }
    const auto seconds{static_cast<unsigned long long>(std::min(*value, 359999.0))};
    char output[32]{};
    std::snprintf(output, sizeof(output), "T+%02llu:%02llu", seconds / 60, seconds % 60);
    return output;
}
std::string localClock(std::chrono::system_clock::time_point time) {
    const auto epoch{std::chrono::system_clock::to_time_t(time)};
    std::tm local{};
    localtime_r(&epoch, &local);
    char output[32]{};
    const auto nanoseconds{std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count()};
    const int tenth{static_cast<int>((nanoseconds % 1000000000 + 1000000000) % 1000000000 / 100000000)};
    std::snprintf(output, sizeof(output), "%02d:%02d:%02d.%d",
        local.tm_hour, local.tm_min, local.tm_sec, tenth);
    return output;
}
std::string frameNumber(std::optional<std::uint64_t> sequence) {
    if (!sequence) { return "F------"; }
    char output[32]{};
    std::snprintf(output, sizeof(output), "F%06llu", static_cast<unsigned long long>(*sequence));
    return output;
}
} // namespace osd::short_text
