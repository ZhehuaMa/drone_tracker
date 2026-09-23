#ifndef RK3576_CAMERA_TIMING_H
#define RK3576_CAMERA_TIMING_H

#include <cstdint>
#include <string>

namespace rk3576 {
/** @brief 本轮固定时钟偏移；夹取样本的跨度用于评估采样误差，单位微秒。 */
struct CameraClockPair {
    std::int64_t offset_us{};
    std::int64_t span_us{};
};
CameraClockPair cameraClockPair(std::int64_t before, std::int64_t realtime, std::int64_t after);
/** @brief 检查原始 timeval，转换到有符号微秒；非法值或溢出抛异常。 */
std::int64_t cameraTimevalUs(std::int64_t seconds, std::int64_t microseconds);
/** @brief 首帧固定策略，之后严格递增；未知时钟只代表出队时刻。 */
class CameraTiming {
public:
    explicit CameraTiming(CameraClockPair pair);
    std::int64_t map(std::int64_t seconds, std::int64_t microseconds,
                     std::uint32_t flags, std::int64_t dequeue_us);
    std::string strategy() const;
    /** @brief 观察期允许相等但仍拒绝倒退；与正式输出使用不同实例。 */
    std::int64_t observe(std::int64_t seconds, std::int64_t microseconds,
                         std::uint32_t flags, std::int64_t dequeue_us);
private:
    std::int64_t convert(std::int64_t seconds, std::int64_t microseconds,
                         std::uint32_t flags, std::int64_t dequeue_us, bool allow_equal);
    CameraClockPair pair_{};
    int strategy_{-1};
    std::int64_t previous_{};
};
/** @brief 单轮有界观察；调用者负责归还丢弃帧，检查量耗尽的帧也必须先归还。 */
class CameraWarmup {
public:
    static constexpr std::int64_t DURATION_US{2000000};
    static constexpr unsigned MAX_SAMPLES{120};
    static constexpr unsigned REQUIRED_STREAK{8};
    CameraWarmup(CameraClockPair pair, std::int64_t started_us);
    bool observe(std::int64_t seconds, std::int64_t microseconds, std::uint32_t flags,
                 std::int64_t dequeue_us, std::int64_t now_us);
    void checkDeadline(std::int64_t now_us) const;
    std::int64_t remainingUs(std::int64_t now_us) const;
    bool ready() const { return streak_ >= REQUIRED_STREAK; }
    bool exhausted() const { return !ready() && examined_ >= MAX_SAMPLES; }
    unsigned examined() const { return examined_; }
    unsigned streak() const { return streak_; }
    unsigned repeats() const { return repeats_; }
    std::string strategy() const { return timing_.strategy(); }
private:
    CameraTiming timing_;
    std::int64_t started_us_{};
    std::int64_t previous_{};
    unsigned examined_{};
    unsigned streak_{};
    unsigned repeats_{};
};
} // namespace rk3576
#endif // RK3576_CAMERA_TIMING_H
