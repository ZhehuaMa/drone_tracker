#ifndef REPLAY_CLOCK_H
#define REPLAY_CLOCK_H
#include <cstdint>
#include <functional>
namespace replay {
/** @brief 单调墙钟可注入，算法时间只由帧号决定。所有墙钟值以纳秒计。 */
struct ClockSource {
    std::function<std::int64_t()> now{};
    std::function<void(std::int64_t)> sleep_until{};
};
ClockSource steadyClock();
std::int64_t frameTimestamp(std::uint64_t index, double fps);
class ReplayClock {
public:
    ReplayClock(double fps, ClockSource source);
    std::int64_t wait(const std::function<bool()> &interrupted = {});
    void finished();
    std::int64_t now() const { return source_.now(); }
    std::int64_t origin() const noexcept { return origin_; }
    double periodNs() const noexcept { return static_cast<double>(period_); }

private:
    ClockSource source_;
    std::int64_t period_{};
    std::int64_t origin_{};
    std::int64_t planned_{};
};
} // namespace replay
#endif // REPLAY_CLOCK_H
