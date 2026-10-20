#ifndef TRACKING_REGRESSION_CPU_OBSERVER_H
#define TRACKING_REGRESSION_CPU_OBSERVER_H

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "diagnostics/recorder.h"

namespace tracking_regression {
/** @brief Test-only single-writer arrays. Masks are read once at role entry, never at work points. */
class CpuObserver final {
public:
    struct Record {
        std::uint64_t mono_ns{}, round{}, update{}, request{}, tracker_frame{}, detail{}, cost_ns{}, frame_time_us{}, duration_ns{};
        int cpu{-1};
        std::uint32_t point{};
    };
    struct Writer {
        std::vector<Record> records{};
        std::size_t size{}, overflow{};
        long tid{}, pid{};
        cpu_set_t mask{};
        int policy{-1}, priority{-1};
        std::uint64_t registered_ns{};
        bool valid{};
    };
    explicit CpuObserver(std::size_t capacity) : capacity_{capacity} {
        if (!capacity || capacity > 4194304) { throw std::invalid_argument{"CPU observation capacity out of range"}; }
        for (auto &writer : writers_) { writer.records.resize(capacity); }
        writer_index_ = -1;
        active_ = this;
        diagnostics::position_receiver = receive;
    }
    ~CpuObserver() {
        diagnostics::position_receiver = nullptr;
        active_ = nullptr;
    }
    CpuObserver(const CpuObserver &) = delete;
    CpuObserver &operator=(const CpuObserver &) = delete;
    /** @brief Called after all business writers stop; all filesystem output is outside the hot path. */
    void exportRecords(const std::filesystem::path &output) const {
        std::ofstream csv{output / "cpu-points.csv"};
        csv << "writer,pid,tid,mono_ns,round,update_id,request_id,tracker_frame_id,point,detail,cpu,cost_ns,frame_time_us,duration_ns\n";
        std::ofstream meta{output / "cpu-observation.json"};
        meta << "{\"schema_version\":1,\"sampling\":\"all_managed_points\",\"capacity_per_writer\":" << capacity_
             << ",\"writers\":[";
        for (std::size_t index{}; index < writers_.size(); ++index) {
            const auto &writer{writers_[index]};
            meta << (index ? "," : "") << "{\"writer\":" << index << ",\"pid\":" << writer.pid
                 << ",\"tid\":" << writer.tid << ",\"registered_ns\":" << writer.registered_ns
                 << ",\"policy\":" << writer.policy << ",\"priority\":" << writer.priority
                 << ",\"valid\":" << (writer.valid ? "true" : "false") << ",\"count\":" << writer.size
                 << ",\"overflow\":" << writer.overflow << ",\"allowed_cpus\":[";
            bool first{true};
            for (int cpu{}; cpu < CPU_SETSIZE; ++cpu) {
                if (CPU_ISSET(cpu, &writer.mask)) { meta << (first ? "" : ",") << cpu; first = false; }
            }
            meta << "]}";
            for (std::size_t item{}; item < writer.size; ++item) {
                const auto &record{writer.records[item]};
                csv << index << ',' << writer.pid << ',' << writer.tid << ',' << record.mono_ns << ','
                    << record.round << ',' << record.update << ',' << record.request << ',' << record.tracker_frame
                    << ',' << record.point << ',' << record.detail << ',' << record.cpu << ',' << record.cost_ns
                    << ',' << record.frame_time_us << ',' << record.duration_ns << '\n';
            }
        }
        meta << "]}\n";
        if (!csv || !meta) { throw std::runtime_error{"Cannot export CPU observations"}; }
    }

private:
    static void receive(diagnostics::Producer producer, const diagnostics::Event &event, std::uint32_t point) noexcept {
        if (!active_) { return; }
        // Diagnostic producer labels may temporarily switch during stop(). Keep the physical writer slot stable.
        if (writer_index_ < 0) { writer_index_ = static_cast<int>(producer); }
        const auto index{static_cast<std::size_t>(writer_index_)};
        if (index >= active_->writers_.size()) { return; }
        auto &writer{active_->writers_[index]};
        // Registration occurs at controlled role entrance before the clock, and is measured separately.
        if (!writer.tid) {
            writer.tid = ::syscall(SYS_gettid); writer.pid = ::getpid();
            struct sched_param scheduling{};
            writer.valid = ::sched_getaffinity(0, sizeof(writer.mask), &writer.mask) == 0
                && ::sched_getparam(0, &scheduling) == 0;
            writer.policy = ::sched_getscheduler(0); writer.priority = scheduling.sched_priority;
            writer.registered_ns = diagnostics::monotonicNs();
        }
        const auto begin{diagnostics::monotonicNs()};
        if (writer.size == writer.records.size()) { ++writer.overflow; return; }
        auto &record{writer.records[writer.size++]};
        record = {begin, event.round_id, event.update_id, event.request_id, event.tracker_frame_id,
                  event.type == diagnostics::Type::DetectStage && point == 15 ? event.values[1] : event.values[0],
                  0, event.frame_time_us, event.duration_ns, ::sched_getcpu(), point};
        record.cost_ns = diagnostics::monotonicNs() - begin;
    }
    inline static CpuObserver *active_{};
    inline static thread_local int writer_index_{-1};
    std::array<Writer, diagnostics::PRODUCERS> writers_{};
    std::size_t capacity_{};
};
} // namespace tracking_regression
#endif // TRACKING_REGRESSION_CPU_OBSERVER_H
