#ifndef DRONE_DIAGNOSTICS_MEASUREMENT_H
#define DRONE_DIAGNOSTICS_MEASUREMENT_H

#include "recorder.h"
#include <fstream>
#include <stdexcept>
#include <string>

namespace diagnostics {
/** @brief 验收专用独立数组；启动时预触及，停完业务线程后才导出，不递归记录。 */
struct Measurement {
    struct Submission {
        std::uint64_t start_ns{}, duration_ns{};
        std::uint32_t round{}, sequence{};
    };
    struct Call {
        std::uint64_t start_ns{}, duration_ns{}, round{};
        std::int64_t frame_time_us{};
        bool consumed{};
    };
    std::array<std::array<Submission, 65536>, PRODUCERS> submissions{};
    std::array<std::size_t, PRODUCERS> sizes{}, overflow{};
    std::array<Call, 8192> calls{};
    std::size_t call_size{}, call_overflow{};
};
static_assert(sizeof(Measurement) + sizeof(SharedRegion) + sizeof(Event) * PRODUCERS < 8 * 1024 * 1024);
inline Measurement *measurement{};

/** @brief 测量整个 submit，包括通知系统调用；每个生产者只有一个写者。 */
class SubmitMeasurement final {
public:
    explicit SubmitMeasurement(Producer producer) noexcept
        : data_{measurement}, producer_{static_cast<std::size_t>(producer)},
          start_{data_ ? monotonicNs() : 0} {}
    ~SubmitMeasurement() noexcept {
        if (!data_ || producer_ >= PRODUCERS) { return; }
        const auto elapsed{monotonicNs() - start_};
        auto &size{data_->sizes[producer_]};
        if (size < data_->submissions[producer_].size()) {
            data_->submissions[producer_][size++] = {start_, elapsed,
                static_cast<std::uint32_t>(current_round.load()), static_cast<std::uint32_t>(sequence_)};
        } else {
            ++data_->overflow[producer_];
        }
    }
    void sequence(std::uint64_t value) noexcept { sequence_ = value; }

private:
    Measurement *data_{};
    std::size_t producer_{};
    std::uint64_t start_{}, sequence_{};
};
inline void measureCall(std::uint64_t start, std::uint64_t elapsed, std::int64_t frame, bool consumed) noexcept {
    if (!measurement) { return; }
    if (measurement->call_size < measurement->calls.size()) {
        measurement->calls[measurement->call_size++] = {start, elapsed, current_round.load(), frame, consumed};
    } else {
        ++measurement->call_overflow;
    }
}
/** @brief 仅 main 在节点销毁后调用；目录须由外部控制器新建并保证没有已有输出。 */
inline void exportMeasurement(const std::string &directory) {
    if (!measurement) { return; }
    std::ofstream submits{directory + "/submissions.csv"}, calls{directory + "/calls.csv"};
    submits << "producer,start_ns,duration_ns,round,sequence\n";
    for (std::size_t producer{}; producer < PRODUCERS; ++producer) {
        for (std::size_t index{}; index < measurement->sizes[producer]; ++index) {
            const auto &sample{measurement->submissions[producer][index]};
            submits << producer << ',' << sample.start_ns << ',' << sample.duration_ns << ','
                    << sample.round << ',' << sample.sequence << '\n';
        }
    }
    calls << "start_ns,duration_ns,round,frame_time_us,consumed\n";
    for (std::size_t index{}; index < measurement->call_size; ++index) {
        const auto &sample{measurement->calls[index]};
        calls << sample.start_ns << ',' << sample.duration_ns << ',' << sample.round << ','
              << sample.frame_time_us << ',' << sample.consumed << '\n';
    }
    std::ofstream status{directory + "/sampling-status.txt"};
    status << "call_overflow=" << measurement->call_overflow << '\n';
    for (std::size_t producer{}; producer < PRODUCERS; ++producer) {
        status << "producer_" << producer << "_overflow=" << measurement->overflow[producer] << '\n';
    }
    if (!submits || !calls || !status) { throw std::runtime_error("Cannot export acceptance measurements"); }
}
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_MEASUREMENT_H
