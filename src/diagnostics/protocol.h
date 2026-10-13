#ifndef DRONE_DIAGNOSTICS_PROTOCOL_H
#define DRONE_DIAGNOSTICS_PROTOCOL_H

#include "event.h"
#include <atomic>
#include <cstddef>

namespace diagnostics {
inline constexpr std::uint32_t MAGIC{0x44544731};
inline constexpr std::uint32_t ENDIAN_MARKER{0x01020304};
inline constexpr std::size_t PRODUCERS{static_cast<std::size_t>(Producer::Count)};
inline constexpr std::size_t NORMAL_CAPACITY{1024};
inline constexpr std::size_t CRITICAL_CAPACITY{512};
inline constexpr std::size_t GAP_RANGES{32};
struct GapRange {
    std::atomic<std::uint64_t> first{0};
    std::atomic<std::uint64_t> last{0};
};
struct alignas(64) Positions {
    std::atomic<std::uint64_t> generated{0}, written{0}, synced{0};
    std::atomic<std::uint64_t> dropped{0}, first_dropped{0}, last_dropped{0};
    std::atomic<std::uint64_t> range_count{0}, gap_epoch{0};
    std::array<GapRange, GAP_RANGES> ranges{};
};
template <std::size_t Capacity> struct alignas(64) Ring {
    alignas(64) std::atomic<std::uint64_t> write{0};
    alignas(64) std::atomic<std::uint64_t> read{0};
    std::array<Event, Capacity> events{};
};
struct ProducerBuffer {
    Positions position{};
    Ring<NORMAL_CAPACITY> normal{};
    Ring<CRITICAL_CAPACITY> critical{};
};
struct alignas(64) SharedRegion {
    std::uint32_t magic{MAGIC};
    std::uint32_t endian_marker{ENDIAN_MARKER};
    std::uint16_t version{FORMAT_VERSION};
    std::uint16_t event_size{sizeof(Event)};
    std::uint64_t region_size{sizeof(SharedRegion)};
    std::uint64_t run_id{};
    std::atomic<std::uint64_t> round_id{0};
    std::atomic<std::uint32_t> consumer_pid{0};
    std::atomic<std::uint32_t> closing{0};
    std::atomic<std::uint32_t> drained{0};
    std::atomic<std::uint32_t> error{0};
    std::atomic<std::uint64_t> sync_started_ns{0}, sync_finished_ns{0}, oldest_unsynced_ns{0};
    std::array<ProducerBuffer, PRODUCERS> producers{};
};
struct Control {
    std::uint32_t magic{MAGIC};
    std::uint32_t endian_marker{ENDIAN_MARKER};
    std::uint16_t version{FORMAT_VERSION};
    std::uint16_t length{sizeof(Control)};
    std::uint64_t region_size{sizeof(SharedRegion)};
    std::uint64_t run_id{};
    std::uint32_t pid{};
    std::uint32_t result{};
    std::array<char, 16384> identity{};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(CRITICAL_CAPACITY * 4 >= NORMAL_CAPACITY + CRITICAL_CAPACITY);
static_assert(sizeof(SharedRegion) + sizeof(Event) * PRODUCERS < 8 * 1024 * 1024);
inline bool valid(const Control &c) noexcept {
    return c.magic == MAGIC && c.endian_marker == ENDIAN_MARKER && c.version == FORMAT_VERSION &&
           c.length == sizeof(Control) && c.region_size == sizeof(SharedRegion) && c.run_id != 0;
}
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_PROTOCOL_H
