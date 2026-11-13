#include "recording/policy.h"

#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char *message) { if (!value) { throw std::runtime_error(message); } }
void catalog() {
    const auto entries{recording::topicCatalog()};
    require(entries.size() == 31 && recording::topicCatalog(true).size() == 32, "profile size");
    const std::vector<std::string> expected{
        "body_imu", "gimbal", "odom", "rc_in", "flight_state", "extended_state", "battery", "pressure",
        "temperature", "low_level_feedback", "gimbal_mode", "trigger", "altitude", "gps_raw", "gps_fix",
        "failsafe", "motor_rpm", "control", "control_raw", "arm", "offboard", "tracker_state", "radar_state",
        "mission_state", "mission_start", "target_bbox", "target_los", "tracker_image", "status_text",
        "radar_target", "radar_interceptor"};
    std::set<std::string> names{};
    for (std::size_t index{}; index < entries.size(); ++index) {
        const auto &entry{entries[index]};
        require(entry.id == expected[index], "stable catalog id");
        require(names.insert(entry.name).second, "duplicate default name");
        require(entry.type.find("/msg/") != std::string::npos, "generated type name");
        require(!entry.protected_event || entry.max_hz == 0, "sampled protected default");
        require((entry.source != recording::SourceKind::Subscription)
            == (entry.id == "trigger" || entry.id == "target_bbox" || entry.id == "target_los"), "direct source");
    }
#define CHECK_GENERATED(id, package, type, file, name, time, source, hz, protect, qos, depth) \
    { package::msg::type message{}; (void)recording::retainedPayload(message); \
      (void)recording::messageStamp(message, recording::TimestampRule::time, 99); }
    DRONE_RECORDING_TOPICS(CHECK_GENERATED)
#undef CHECK_GENERATED
    quadrotor_msgs::msg::ControlCommand command{};
    command.header.stamp.sec = 10;
    command.expected_execution_time.sec = 11;
    command.rotor_thrusts.reserve(200);
    const auto bytes{recording::retainedPayload(command)};
    require(bytes >= sizeof(command) + 200 * sizeof(double), "command retained vector capacity");
    require(recording::messageStamp(command, recording::TimestampRule::Header, 12).nanoseconds == 10000000000LL,
        "execution time replaced source header");
    quadrotor_msgs::msg::LowLevelFeedback feedback{};
    feedback.motor_speeds.reserve(99); feedback.thrust_mapping_coeffs.reserve(53);
    require(recording::retainedPayload(feedback) >= sizeof(feedback) + 99 * sizeof(std::int16_t)
        + 53 * sizeof(double), "feedback vector capacity");
}
void policy() {
    using recording::StampFallback;
    builtin_interfaces::msg::Time time{};
    require(recording::selectStamp(time, 30).fallback == StampFallback::Zero, "zero fallback");
    time.sec = -1;
    require(recording::selectStamp(time, 30).fallback == StampFallback::NegativeSeconds, "negative fallback");
    time.sec = 1; time.nanosec = 1000000000;
    require(recording::selectStamp(time, 30).fallback == StampFallback::InvalidNanoseconds, "malformed fallback");
    time.nanosec = 7;
    require(recording::selectStamp(time, 900000000000LL).nanoseconds == 1000000007, "old valid stamp rejected");
    recording::Sampler sampler{};
    require(sampler.select(100, 0), "first sample delayed");
    require(!sampler.select(100, 5000000), "200Hz sample not omitted");
    require(sampler.select(100, 10000000), "100Hz boundary");
    require(!sampler.select(100, 10000001), "burst accepted");
    require(!sampler.select(100, 0), "elapsed-clock rollback burst");
    require(sampler.select(100, 10000001, true), "OFF pair sampled");
    require(sampler.select(0, 10000001), "zero Hz disabled entry");
    recording::Sampler sparse{};
    require(sparse.select(25, 5000000000LL) && !sparse.select(25, 5000000001LL), "sparse burst");
    recording::Sampler slow{};
    require(slow.select(1e-300, 0) && !slow.select(1e-300, std::numeric_limits<std::int64_t>::max()),
        "very small valid frequency overflowed the clock interval");
    for (const auto &entry : recording::topicCatalog()) {
        if (!entry.protected_event) { continue; }
        recording::Sampler events{};
        require(events.select(entry.max_hz, 0) && events.select(entry.max_hz, 1000000)
            && events.select(entry.max_hz, 2000000), "brief protected transition sampled");
    }
    recording::TokenBucket bucket{100, 100};
    require(bucket.available(100, 0), "initial image burst"); bucket.consume(100);
    require(!bucket.available(1, 0), "consumed image tokens");
    require(bucket.available(50, 500000000), "image replenishment"); bucket.consume(50);
    require(!bucket.available(51, 1000000000), "image byte limit");
    require(bucket.available(100, 2000000000), "image token cap");
}
} // namespace
int main() {
    try { catalog(); policy(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
