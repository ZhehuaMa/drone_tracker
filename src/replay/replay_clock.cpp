#include "replay_clock.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
namespace replay {
namespace {
std::int64_t checked(long double value) {
    if (!std::isfinite(value) || value < 0 || value >= 9223372036854775808.0L) {
        throw std::overflow_error("Replay time outside int64 range");
    }
    return static_cast<std::int64_t>(value);
}
void validFps(double fps) {
    if (!std::isfinite(fps) || fps <= 0 || fps > 1000000) {
        throw std::invalid_argument("Invalid fps");
    }
}
} // namespace
ClockSource steadyClock() {
    return {[] {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            },
            [](std::int64_t ns) {
                std::this_thread::sleep_until(std::chrono::steady_clock::time_point{std::chrono::nanoseconds{ns}});
            }};
}
std::int64_t frameTimestamp(std::uint64_t index, double fps) {
    validFps(fps);
    const auto calculate{[fps](std::uint64_t i) {
        return checked(1000000.0L + std::round(static_cast<long double>(i) * 1000000.0L / fps));
    }};
    const auto value{calculate(index)};
    if (index && value <= calculate(index - 1)) {
        throw std::overflow_error("Non-increasing frame time");
    }
    return value;
}
ReplayClock::ReplayClock(double fps, ClockSource source) : source_{std::move(source)} {
    validFps(fps);
    period_ = checked(std::ceil(1000000000.0L / fps));
    origin_ = source_.now();
    planned_ = origin_;
}
std::int64_t ReplayClock::wait(const std::function<bool()> &interrupted) {
    // 极低帧率也应及时响应 SIGINT；短等待不改变最终计划提交时刻。
    do {
        if (interrupted && interrupted()) {
            return source_.now();
        }
        const auto before{source_.now()};
        const auto target{planned_ > before && planned_ - before > 50000000 ? before + 50000000 : planned_};
        source_.sleep_until(target);
        const auto now{source_.now()};
        if (now < target || now < before) {
            throw std::runtime_error("Clock returned before scheduled wait");
        }
        if (now >= planned_) {
            return now;
        }
    } while (true);
}
void ReplayClock::finished() {
    const auto base{std::max(planned_, source_.now())};
    if (base > std::numeric_limits<std::int64_t>::max() - period_) {
        throw std::overflow_error("Wall clock schedule overflow");
    }
    planned_ = base + period_;
}
} // namespace replay
