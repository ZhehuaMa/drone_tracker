#ifndef DRONE_DIAGNOSTICS_TIMING_H
#define DRONE_DIAGNOSTICS_TIMING_H
#include "recorder.h"
#include "hybrid_tracker/timing_observer.h"

namespace diagnostics {
/** @brief 定长阶段观察器；每个调用在栈上创建，不分配、不执行文件 I/O。 */
class DetailObserver final : public timing::Observer {
public:
    std::uint32_t begin(timing::Stage stage) noexcept override {
        for (std::size_t index{}; index < starts_.size(); ++index) {
            if (starts_[index].time == 0) {
                starts_[index] = {monotonicNs(), stage};
                return static_cast<std::uint32_t>(index + 1);
            }
        }
        ++failed_[static_cast<std::size_t>(stage)];
        return 0;
    }
    void end(std::uint32_t id, bool complete) noexcept override {
        if (id == 0 || id > starts_.size()) {
            return;
        }
        auto &start{starts_[id - 1]};
        if (start.time == 0) {
            return;
        }
        const auto index{static_cast<std::size_t>(start.stage)};
        duration_[index] += monotonicNs() - start.time;
        ++count_[index];
        if (!complete) {
            ++failed_[index];
        }
        start.time = 0;
    }
    void flush() const noexcept {
        for (std::size_t index{}; index < timing::stageCount; ++index) {
            auto value{event(Type::Timing)};
            value.values[0] = index;
            value.duration_ns = duration_[index];
            value.counts[0] = count_[index];
            value.counts[1] = failed_[index];
            if (count_[index]) {
                value.flags |= Observed;
            }
            if (count_[index] && !failed_[index]) {
                value.flags |= Complete;
            }
            emit(value);
        }
    }

private:
    struct Start {
        std::uint64_t time{};
        timing::Stage stage{};
    };
    std::array<Start, 128> starts_{};
    std::array<std::uint64_t, timing::stageCount> duration_{};
    std::array<std::uint32_t, timing::stageCount> count_{}, failed_{};
};
class DetailScope final {
public:
    DetailScope() noexcept : enabled_{isEnabled()}, observation_{enabled_ ? &observer_ : timing::current} {}
    ~DetailScope() noexcept {
        if (enabled_) {
            observer_.flush();
        }
    }

private:
    static bool isEnabled() noexcept {
        auto *recorder{active.load(std::memory_order_acquire)};
        return recorder && recorder->mode() == Mode::Detail;
    }
    bool enabled_{};
    DetailObserver observer_{};
    timing::Observation observation_;
};
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_TIMING_H
