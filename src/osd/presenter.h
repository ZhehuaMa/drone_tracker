#ifndef OSD_PRESENTER_H
#define OSD_PRESENTER_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "osd/presentation.h"

namespace osd {
struct PresentationCounters {
    std::uint64_t submitted{}, replaced{}, displayed{}, refreshes{}, rejected{};
};
/** @brief 管理完整包交接与独立显示线程。停轮不终止显示，遥测仍可更新。 */
class OsdPresenter {
public:
    explicit OsdPresenter(std::unique_ptr<DisplayBackend> backend,
                          std::chrono::milliseconds stale = std::chrono::milliseconds{1000},
                          std::function<SteadyClock::time_point()> now = SteadyClock::now,
                          std::function<void(const TimingWindow &)> timing_sink = {});
    ~OsdPresenter();
    OsdPresenter(const OsdPresenter &) = delete;
    OsdPresenter &operator=(const OsdPresenter &) = delete;
    /** @brief 同步确认显示初始化，失败时抛异常并回收线程。 */
    void start();
    void shutdown() noexcept;
    /** @brief 重复 ON 返回同一轮次；新轮返回递增编号。 */
    std::uint64_t beginRound();
    /** @brief 只停止指定轮，防止旧工作线程的 EOF 关闭新轮。 */
    void endRound(std::uint64_t round, DisplayStatus reason);
    /** @brief 仅交接所有权，不映射或复制像素，也不等待设备。 */
    bool submit(PresentationPacket packet);
    void updateTelemetry(TelemetrySnapshot snapshot);
    PresentationCounters counters() const;
    std::string failure() const;
    /** @brief 独立诊断回调及退出调用；快照后释放提交锁再输出。 */
    void reportTiming();

private:
    void render() noexcept;
    TimingWindow &timingWindow(std::uint64_t round);
    const std::function<void(const TimingWindow &)> timing_sink_;
    // 每次新轮前冲刷；至多当前轮、一个在途旧轮及初始等待轮。
    std::array<std::optional<TimingWindow>, 3> timing_windows_{};
    std::uint64_t timing_window_id_{};
    SteadyClock::time_point timing_window_start_{};
    std::optional<SteadyClock::time_point> last_display_{};
    std::uint64_t interval_generation_{}, first_video_round_{~std::uint64_t{0}};
    std::unique_ptr<DisplayBackend> backend_;
    const std::chrono::milliseconds stale_;
    const std::function<SteadyClock::time_point()> now_;
    mutable std::mutex mutex_{};
    std::mutex round_mutex_{}; // 串行化轮次切换及其锁外冲刷，保证窗口容量上界。
    std::condition_variable wake_{};
    std::thread thread_{};
    std::optional<PresentationPacket> pending_{};
    TelemetrySnapshot telemetry_{};
    PresentationCounters counters_{};
    std::uint64_t round_{}, version_{}, last_sequence_{};
    bool active_{}, stopping_{}, initialized_{}, finished_{}, started_{};
    DisplayStatus status_{DisplayStatus::WaitingInput};
    SteadyClock::time_point last_received_{};
    std::string failure_{};
};
} // namespace osd
#endif // OSD_PRESENTER_H
