#include "recorder.h"
#include "measurement.h"
#include <cerrno>
#include <unistd.h>

namespace diagnostics {
namespace {
template <std::size_t Capacity> bool push(Ring<Capacity> &ring, const Event &event, bool &empty) noexcept {
    const std::uint64_t write{ring.write.load(std::memory_order_relaxed)};
    const std::uint64_t read{ring.read.load(std::memory_order_acquire)};
    empty = write == read;
    if (write - read >= Capacity) {
        return false;
    }
    ring.events[write % Capacity] = event;
    ring.write.store(write + 1, std::memory_order_release);
    return true;
}
void drop(Positions &position, std::uint64_t sequence) noexcept {
    position.gap_epoch.fetch_add(1, std::memory_order_seq_cst);
    const std::uint64_t count{position.dropped.load(std::memory_order_relaxed)};
    if (count == 0) {
        position.first_dropped.store(sequence, std::memory_order_relaxed);
    }
    const std::uint64_t previous{position.last_dropped.load(std::memory_order_relaxed)};
    std::uint64_t ranges{position.range_count.load(std::memory_order_relaxed)};
    if (previous + 1 != sequence || count == 0) {
        if (ranges < GAP_RANGES) {
            position.ranges[ranges].first.store(sequence, std::memory_order_relaxed);
            position.ranges[ranges].last.store(sequence, std::memory_order_release);
        }
        position.range_count.store(++ranges, std::memory_order_release);
    } else if (ranges <= GAP_RANGES) {
        position.ranges[ranges - 1].last.store(sequence, std::memory_order_release);
    }
    position.last_dropped.store(sequence, std::memory_order_release);
    position.dropped.store(count + 1, std::memory_order_release);
    position.gap_epoch.fetch_add(1, std::memory_order_seq_cst);
}
} // namespace
Recorder::Recorder(SharedRegion &region, int notification, Mode mode) noexcept
    : region_{region}, notification_{notification}, mode_{mode} {}
bool Recorder::submit(Producer producer, Event value) noexcept {
    SubmitMeasurement measured{producer};
    if (mode_ == Mode::Off) {
        return false;
    }
    const std::size_t index{static_cast<std::size_t>(producer)};
    if (index >= PRODUCERS) {
        return false;
    }
    auto &buffer{region_.producers[index]};
    value.run_id = region_.run_id;
    value.round_id = region_.round_id.load(std::memory_order_acquire);
    value.producer_id = static_cast<std::uint32_t>(producer);
    value.producer_seq = buffer.position.generated.load(std::memory_order_relaxed) + 1;
    measured.sequence(value.producer_seq);
    value.mono_ns = monotonicNs();
    bool empty{};
    const bool critical{(value.flags & Critical) != 0};
    const bool accepted{critical ? push(buffer.critical, value, empty) : push(buffer.normal, value, empty)};
    if (!accepted) {
        drop(buffer.position, value.producer_seq);
    } else {
        std::uint64_t expected{};
        region_.oldest_unsynced_ns.compare_exchange_strong(expected, value.mono_ns, std::memory_order_release);
    }
    buffer.position.generated.store(value.producer_seq, std::memory_order_release);
    if (accepted && (critical || empty) && (critical || value.mono_ns - last_notification_[index] >= 1000000)) {
        const std::uint64_t one{1};
        const auto ignored{::write(notification_, &one, sizeof(one))};
        (void)ignored; // EAGAIN 或遗漏不撤销槽；收集器主动轮询。
        last_notification_[index] = value.mono_ns;
    }
    return accepted;
}
} // namespace diagnostics
