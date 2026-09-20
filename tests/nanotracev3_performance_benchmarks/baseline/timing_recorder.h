#ifndef NANOTRACK_BASELINE_TIMING_RECORDER_H
#define NANOTRACK_BASELINE_TIMING_RECORDER_H
#include "timing_observer.h"
#include <array>
#include <cstdint>
#include <optional>
namespace baseline {
using Stage = timing::Stage;
struct SpanRecord {
    std::uint32_t id{}, parent{};
    Stage stage{};
    std::int64_t start{}, end{};
    bool complete{};
};
/** @brief 每帧固定容量记录器；满容量或区间错误导致本帧失败，不丢弃后继续测量。 */
class TimingRecorder final : public timing::Observer {
  public:
    using Clock = std::int64_t (*)() noexcept;
    explicit TimingRecorder(bool detailed = true, Clock clock = steadyNow) noexcept;
    static std::int64_t steadyNow() noexcept;
    std::uint32_t begin(Stage stage) noexcept override;
    void end(std::uint32_t id, bool complete) noexcept override;
    void finish(bool complete) noexcept;
    void clear() noexcept;
    void markFailed() noexcept {
        if (size_) {
            spans_[0].complete = false;
        }
    }
    std::optional<std::int64_t> duration(Stage stage) const;
    const auto &spans() const noexcept { return spans_; }
    std::size_t size() const noexcept { return size_; }
    bool valid() const noexcept { return !error_; }
    bool active() const noexcept { return size_ != 0; }
    std::int64_t now() const noexcept { return clock_() - origin_; }

  private:
    std::array<SpanRecord, 256> spans_{};
    std::size_t size_{};
    std::uint32_t top_{};
    bool detailed_{}, error_{};
    Clock clock_{};
    std::int64_t origin_{};
};
const char *stageName(Stage stage);
const char *modelRole(const TimingRecorder &recorder, const SpanRecord &span);
} // namespace baseline
#endif // NANOTRACK_BASELINE_TIMING_RECORDER_H
