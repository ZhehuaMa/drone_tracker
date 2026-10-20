#ifndef DRONE_DIAGNOSTICS_RECORDER_H
#define DRONE_DIAGNOSTICS_RECORDER_H

#include "protocol.h"
#include <chrono>

namespace diagnostics {
/** @brief 热路径只填数值、发布原子位置和合并通知；每个 Producer 必须只有一个线程写入。 */
class Recorder {
public:
    Recorder(SharedRegion &region, int notification, Mode mode) noexcept;
    bool submit(Producer producer, Event event) noexcept;
    Mode mode() const noexcept { return mode_; }
    void round(std::uint64_t value) noexcept { region_.round_id.store(value, std::memory_order_release); }

private:
    SharedRegion &region_;
    int notification_{};
    Mode mode_{};
    std::array<std::uint64_t, PRODUCERS> last_notification_{};
};
inline std::uint64_t monotonicNs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
// 仅在启动/所有业务线程停止的边界替换，调用不取得锁。
inline std::atomic<Recorder *> active{nullptr};
inline std::atomic<std::uint64_t> current_round{0};
inline thread_local Producer current_producer{Producer::Node};
inline thread_local std::uint64_t current_update{0};
inline thread_local std::uint64_t current_tracker_frame{0};
inline thread_local std::uint64_t current_request{0};
// Installed only by a regression entry point before any business threads exist.
using PositionReceiver = void (*)(Producer, const Event &, std::uint32_t) noexcept;
inline PositionReceiver position_receiver{};
inline void positionPoint(std::uint32_t point, std::uint64_t detail = 0) noexcept {
    if (!position_receiver) { return; }
    Event sample{};
    sample.round_id = current_round.load(std::memory_order_relaxed);
    sample.update_id = current_update;
    sample.tracker_frame_id = current_tracker_frame;
    sample.request_id = current_request;
    sample.values[0] = detail;
    position_receiver(current_producer, sample, point);
}
inline void emit(Event event) noexcept {
#ifdef DRONE_TRACKER_ENABLE_DIAGNOSTICS
    Recorder *recorder{active.load(std::memory_order_acquire)};
    if (recorder) {
        if (event.update_id == 0) {
            event.update_id = current_update;
        }
        if (event.tracker_frame_id == 0) {
            event.tracker_frame_id = current_tracker_frame;
        }
        if (event.request_id == 0) {
            event.request_id = current_request;
        }
        if (position_receiver) {
            Event sample{event};
            sample.round_id = current_round.load(std::memory_order_relaxed);
            position_receiver(current_producer, sample, static_cast<std::uint32_t>(event.type));
        }
        recorder->submit(current_producer, event);
    }
#else
    (void)event;
#endif
}
inline Event event(Type type, Reason reason = Reason::None, bool critical = false) noexcept {
    Event result{};
    result.type = type;
    result.reason = reason;
    if (critical) {
        result.flags |= Critical;
    }
    return result;
}
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_RECORDER_H
