#ifndef RK3576_VIDEO_TIMING_H
#define RK3576_VIDEO_TIMING_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rk3576 {
/** @brief 严格解析历史微秒基准；字段名不区分 ASCII 大小写且必须唯一。 */
std::int64_t historyBase(const std::vector<std::pair<std::string, std::string>> &metadata);
/** @brief 非负 PTS 按最近微秒换算（半微秒向上），检查缺失、重复及溢出。 */
std::int64_t videoPtsUs(std::int64_t pts, bool present, std::pair<int, int> time_base, std::int64_t previous);
/** @brief 检查正 int64 微秒帧时间。 */
std::int64_t videoTimestamp(std::int64_t base, std::int64_t pts_us);
/** @brief 时间与等待注入边界；waitUntil 返回 false 表示主动停止。 */
class VideoClock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    virtual ~VideoClock() = default;
    virtual std::int64_t systemUs();
    virtual TimePoint steadyNow();
    virtual bool waitUntil(TimePoint deadline, const std::atomic<bool> &stop);
    virtual void wake();
private:
    std::mutex mutex_{};
    std::condition_variable condition_{};
};
/** @brief 在 steady_clock 原生刻度上检查截止时间溢出。 */
VideoClock::TimePoint videoDeadline(VideoClock::TimePoint origin, std::int64_t pts_us);
} // namespace rk3576
#endif // RK3576_VIDEO_TIMING_H
