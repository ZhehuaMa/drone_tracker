#ifndef DRONE_TRACKER_RECORDING_POLICY_H
#define DRONE_TRACKER_RECORDING_POLICY_H

#include "topic_catalog.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace recording {
enum class StampFallback { None, Zero, NegativeSeconds, InvalidNanoseconds };
struct SelectedStamp { std::int64_t nanoseconds{}; StampFallback fallback{}; };

/** @brief Select the original source time or a diagnosed receipt-time fallback. */
inline SelectedStamp selectStamp(const builtin_interfaces::msg::Time &stamp, std::int64_t receipt) {
    if (stamp.sec < 0) { return {receipt, StampFallback::NegativeSeconds}; }
    if (stamp.nanosec >= 1000000000U) { return {receipt, StampFallback::InvalidNanoseconds}; }
    if (stamp.sec == 0 && stamp.nanosec == 0) { return {receipt, StampFallback::Zero}; }
    return {static_cast<std::int64_t>(stamp.sec) * 1000000000LL + stamp.nanosec, StampFallback::None};
}

template<typename T, typename = void> struct HasHeader : std::false_type {};
template<typename T> struct HasHeader<T, std::void_t<decltype(std::declval<T>().header)>> : std::true_type {};

template<typename T>
SelectedStamp messageStamp(const T &message, TimestampRule rule, std::int64_t receipt) {
    if (rule == TimestampRule::Receipt) { return {receipt, StampFallback::None}; }
    if constexpr (HasHeader<T>::value) { return selectStamp(message.header.stamp, receipt); }
    else if constexpr (std::is_same_v<T, std_msgs::msg::Header>) { return selectStamp(message.stamp, receipt); }
    else { return {receipt, StampFallback::None}; }
}

/** @brief Minimum-interval selection using caller-supplied monotonic nanoseconds. */
class Sampler {
public:
    bool select(double hz, std::int64_t now, bool bypass = false) {
        if (bypass || hz == 0) { return true; }
        // Very small positive rates may exceed the integer clock range; infinity safely selects no second sample.
        const double interval{std::ceil(1e9 / hz)};
        if (last_ && (now < *last_ || static_cast<double>(now - *last_) < interval)) { return false; }
        last_ = now;
        return true;
    }
private:
    std::optional<std::int64_t> last_{};
};

/** @brief Bounded byte allowance. Only successfully admitted images consume tokens. */
class TokenBucket {
public:
    TokenBucket(double rate, double capacity) : rate_{rate}, capacity_{capacity}, tokens_{capacity} {}
    bool available(std::size_t bytes, std::int64_t now) {
        if (last_ && now > *last_) {
            tokens_ = std::min(capacity_, tokens_ + static_cast<double>(now - *last_) * rate_ / 1e9);
        }
        last_ = now;
        return static_cast<double>(bytes) <= tokens_;
    }
    void consume(std::size_t bytes) { tokens_ -= static_cast<double>(bytes); }
private:
    double rate_{}, capacity_{}, tokens_{};
    std::optional<std::int64_t> last_{};
};

inline std::size_t stringStorage(const std::string &value) { return value.capacity() + 1; }
template<typename T> std::size_t vectorStorage(const std::vector<T> &value) {
    return value.capacity() * sizeof(T);
}

/** @brief Conservative retained message storage, including dynamic capacities. */
template<typename T> std::size_t retainedPayload(const T &message) {
    std::size_t bytes{sizeof(T)};
    if constexpr (HasHeader<T>::value) { bytes += stringStorage(message.header.frame_id); }
    return bytes;
}
inline std::size_t retainedPayload(const std_msgs::msg::Header &m) {
    return sizeof(m) + stringStorage(m.frame_id);
}
inline std::size_t retainedPayload(const std_msgs::msg::String &m) {
    return sizeof(m) + stringStorage(m.data);
}
inline std::size_t retainedPayload(const sensor_msgs::msg::Image &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + stringStorage(m.encoding) + vectorStorage(m.data);
}
inline std::size_t retainedPayload(const nav_msgs::msg::Odometry &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + stringStorage(m.child_frame_id);
}
inline std::size_t retainedPayload(const mavros_msgs::msg::State &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + stringStorage(m.mode);
}
inline std::size_t retainedPayload(const mavros_msgs::msg::RCIn &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + vectorStorage(m.channels);
}
inline std::size_t retainedPayload(const mavros_msgs::msg::StatusText &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + stringStorage(m.text);
}
inline std::size_t retainedPayload(const sensor_msgs::msg::BatteryState &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + stringStorage(m.location)
        + stringStorage(m.serial_number) + vectorStorage(m.cell_voltage) + vectorStorage(m.cell_temperature);
}
inline std::size_t retainedPayload(const quadrotor_msgs::msg::ControlCommand &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + vectorStorage(m.rotor_thrusts);
}
inline std::size_t retainedPayload(const quadrotor_msgs::msg::LowLevelFeedback &m) {
    return sizeof(m) + stringStorage(m.header.frame_id) + vectorStorage(m.motor_speeds)
        + vectorStorage(m.thrust_mapping_coeffs);
}
inline std::size_t retainedPayload(const std_msgs::msg::Float32MultiArray &m) {
    std::size_t bytes{sizeof(m) + vectorStorage(m.data) + vectorStorage(m.layout.dim)};
    for (const auto &dimension : m.layout.dim) { bytes += stringStorage(dimension.label); }
    return bytes;
}
inline std::size_t retainedPayload(const vision_msgs::msg::Detection2D &m) {
    std::size_t bytes{sizeof(m) + stringStorage(m.header.frame_id) + stringStorage(m.id)
        + vectorStorage(m.results)};
    for (const auto &result : m.results) { bytes += stringStorage(result.hypothesis.class_id); }
    return bytes;
}
} // namespace recording
#endif // DRONE_TRACKER_RECORDING_POLICY_H
