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
private:
    CameraClockPair pair_{};
    int strategy_{-1};
    std::int64_t previous_{};
};
} // namespace rk3576
#endif // RK3576_CAMERA_TIMING_H
