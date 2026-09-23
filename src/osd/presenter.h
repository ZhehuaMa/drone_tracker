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
                          std::function<SteadyClock::time_point()> now = SteadyClock::now);
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

private:
    void render() noexcept;
    std::unique_ptr<DisplayBackend> backend_;
    const std::chrono::milliseconds stale_;
    const std::function<SteadyClock::time_point()> now_;
    mutable std::mutex mutex_{};
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
