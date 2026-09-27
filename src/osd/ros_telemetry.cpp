#include "osd/ros_telemetry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#ifdef DRONE_OSD_LOW_LEVEL_FEEDBACK
#include "osd/low_level_feedback.h"
#endif

namespace osd {
namespace {
constexpr double PI{3.14159265358979323846};
constexpr double RAD{PI / 180.0};
double normalizedAngle(double degrees) {
    return std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
}
std::optional<double> finite(double value) {
    return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
}
std::optional<double> nonnegative(double value) {
    return value >= 0 ? finite(value) : std::nullopt;
}
template <typename T> void assign(Measurement<T> &target, std::optional<T> value, SteadyClock::time_point now) {
    target.value = std::move(value);
    if (target.value) {
        target.received = now;
    }
}
} // namespace
TelemetryStore::TelemetryStore(std::chrono::milliseconds timeout, int battery_cells,
                               std::function<SteadyClock::time_point()> now)
    : timeout_{timeout}, cells_{battery_cells}, now_{std::move(now)} {
    if (timeout_.count() <= 0 || cells_ < 0 || cells_ > 32 || !now_) {
        throw std::invalid_argument("OSD invalid telemetry timeout or battery cells");
    }
}
void TelemetryStore::receive(const sensor_msgs::msg::Imu &message) {
    const auto &q{message.orientation};
    std::optional<Attitude> result{};
    const double norm{std::hypot(std::hypot(q.x, q.y), std::hypot(q.z, q.w))};
    if (message.orientation_covariance[0] != -1 && std::isfinite(norm) && norm > 1e-12) {
        const double x{q.x / norm}, y{q.y / norm}, z{q.z / norm}, w{q.w / norm};
        const double yaw{std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) / RAD};
        result = Attitude{std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) / RAD,
            std::asin(std::clamp(2 * (w * y - z * x), -1.0, 1.0)) / RAD, normalizedAngle(90 - yaw)};
    }
    std::lock_guard<std::mutex> lock{mutex_};
    assign(data_.attitude, result, now_());
}
void TelemetryStore::receive(const mavros_msgs::msg::State &message) {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto now{now_()};
    if (message.armed && !armed_) {
        armed_at_ = now;
        home_.reset();
        if (message.connected && fresh(data_.position, now)) {
            home_ = data_.position.value;
        }
    }
    if (!message.armed) {
        home_.reset();
    }
    armed_ = message.armed;
    data_.flight = {FlightState{message.connected, message.armed, message.mode}, now};
}
void TelemetryStore::receive(const mavros_msgs::msg::ExtendedState &message) {
    std::lock_guard<std::mutex> lock{mutex_};
    std::optional<bool> landed{};
    if (message.landed_state >= 1 && message.landed_state <= 4) {
        landed = message.landed_state == mavros_msgs::msg::ExtendedState::LANDED_STATE_ON_GROUND;
    }
    assign(data_.landed, landed, now_());
}
void TelemetryStore::receive(const sensor_msgs::msg::BatteryState &message) {
    Battery battery{};
    if (message.present) {
        battery.voltage = nonnegative(message.voltage);
        battery.current_amp = finite(message.current);
        if (message.percentage >= 0 && message.percentage <= 1) {
            battery.percentage = message.percentage;
        }
        if (std::isfinite(message.capacity) && message.capacity > 0 && std::isfinite(message.charge)
            && message.charge >= 0 && message.charge <= message.capacity) {
            battery.consumed_mah = (message.capacity - message.charge) * 1000.0;
        }
        if (cells_ > 0 && battery.voltage) {
            battery.cell_voltage = *battery.voltage / cells_;
        } else if (!message.cell_voltage.empty()
            && std::all_of(message.cell_voltage.begin(), message.cell_voltage.end(),
                           [](double v) { return std::isfinite(v) && v > 0; })) {
            double sum{0};
            for (double v : message.cell_voltage) { sum += v; }
            battery.cell_voltage = sum / message.cell_voltage.size();
        }
    }
    std::lock_guard<std::mutex> lock{mutex_};
    const bool valid{battery.voltage || battery.current_amp || battery.percentage
        || battery.consumed_mah || battery.cell_voltage};
    assign(data_.battery, valid ? std::optional<Battery>{battery} : std::nullopt, now_());
}
void TelemetryStore::receive(const mavros_msgs::msg::GPSRAW &message) {
    GpsStatus gps{};
    if (message.satellites_visible != 255) { gps.satellites = message.satellites_visible; }
    if (message.fix_type <= 8) { gps.fix_type = message.fix_type; }
    if (message.fix_type >= 2 && message.fix_type <= 8) {
        if (message.vel != 65535) { gps.speed_mps = message.vel * 0.01; }
        if (message.cog < 36000) { gps.course_deg = message.cog * 0.01; }
    }
    std::lock_guard<std::mutex> lock{mutex_};
    assign(data_.gps, gps.satellites || gps.fix_type || gps.speed_mps || gps.course_deg
        ? std::optional<GpsStatus>{gps} : std::nullopt, now_());
}
void TelemetryStore::receive(const sensor_msgs::msg::NavSatFix &message) {
    std::optional<Position> position{};
    if (message.status.status >= sensor_msgs::msg::NavSatStatus::STATUS_FIX
        && std::isfinite(message.latitude) && std::isfinite(message.longitude)
        && std::abs(message.latitude) <= 90 && std::abs(message.longitude) <= 180) {
        position = Position{message.latitude, message.longitude};
    }
    std::lock_guard<std::mutex> lock{mutex_};
    assign(data_.position, position, now_());
}
void TelemetryStore::receive(const sensor_msgs::msg::FluidPressure &message) {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto now{now_()};
    const auto pressure{message.fluid_pressure > 0 ? finite(message.fluid_pressure) : std::nullopt};
    assign(data_.pressure_pa, pressure, now);
    if (pressure && fresh(data_.flight, now) && data_.flight.value->connected && !data_.flight.value->armed
        && fresh(data_.landed, now) && *data_.landed.value) {
        ground_pressure_ = ground_pressure_ ? *ground_pressure_ * 0.95 + *pressure * 0.05 : *pressure;
    }
}
void TelemetryStore::receive(const sensor_msgs::msg::Temperature &message) {
    std::lock_guard<std::mutex> lock{mutex_};
    assign(data_.temperature_c, message.temperature >= -273.15 ? finite(message.temperature) : std::nullopt, now_());
}
void TelemetryStore::receive(const std_msgs::msg::Bool &message) {
    std::lock_guard<std::mutex> lock{mutex_};
    data_.failsafe = {message.data, now_()};
}
void TelemetryStore::receive(const std_msgs::msg::Int8 &message) {
    std::lock_guard<std::mutex> lock{mutex_};
    assign(data_.radar_state, message.data >= 0 && message.data <= 3
        ? std::optional<int>{message.data} : std::nullopt, now_());
}
void TelemetryStore::receiveBatteryWarning(int level) {
    std::lock_guard<std::mutex> lock{mutex_};
    assign(data_.battery_warning, level >= 1 && level <= 3 ? std::optional<int>{level} : std::nullopt, now_());
}
TelemetrySnapshot TelemetryStore::snapshot() const {
    std::lock_guard<std::mutex> lock{mutex_};
    const auto now{now_()};
    TelemetrySnapshot result{data_};
    const auto expire{[&](auto &m) { if (!fresh(m, now)) { m.value.reset(); } }};
    expire(result.attitude); expire(result.flight); expire(result.position); expire(result.battery);
    expire(result.gps); expire(result.landed); expire(result.failsafe); expire(result.pressure_pa);
    expire(result.temperature_c); expire(result.radar_state); expire(result.battery_warning);
    if (result.flight.value && result.flight.value->connected && result.flight.value->armed) {
        result.armed_seconds = std::chrono::duration<double>(now - armed_at_).count();
        if (home_ && result.position.value) {
            const double lat1{result.position.value->latitude_deg * RAD}, lat2{home_->latitude_deg * RAD};
            const double dl{(home_->longitude_deg - result.position.value->longitude_deg) * RAD};
            const double a{std::pow(std::sin((lat2 - lat1) / 2), 2)
                + std::cos(lat1) * std::cos(lat2) * std::pow(std::sin(dl / 2), 2)};
            result.home_distance_m = 6371000.0 * 2 * std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
            if (result.attitude.value) {
                const double bearing{std::atan2(std::sin(dl) * std::cos(lat2),
                    std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dl)) / RAD};
                result.home_relative_deg = normalizedAngle(bearing - result.attitude.value->heading_deg);
            }
        }
    }
    if (ground_pressure_ && result.pressure_pa.value) {
        result.altitude_m = 44330.0 * (1 - std::pow(*result.pressure_pa.value / *ground_pressure_, 0.190294957));
    }
    if (result.flight.value && !result.flight.value->connected) {
        result.warning = "CONNECTION LOST";
    } else if (result.failsafe.value && *result.failsafe.value) {
        result.warning = "FAILSAFE";
    } else if (result.battery_warning.value && *result.battery_warning.value == 3) {
        result.warning = "BATTERY CRITICAL";
    } else if (result.battery_warning.value && *result.battery_warning.value == 2) {
        result.warning = "BATTERY LOW";
    } else if (!result.flight.value) {
        result.warning = "CONNECTION UNKNOWN";
    } else if (!result.failsafe.value) {
        result.warning = "FAILSAFE UNKNOWN";
    }
    return result;
}
OsdTelemetryAdapter::OsdTelemetryAdapter(rclcpp::Node &node, std::function<void(TelemetrySnapshot)> sink,
                                         std::chrono::milliseconds timeout, int battery_cells)
    : store_{std::make_shared<TelemetryStore>(timeout, battery_cells)} {
    const auto subscribe{[&](auto message, const char *topic, bool best_effort) {
        using Message = decltype(message);
        rclcpp::QoS qos{10};
        if (best_effort) { qos.best_effort(); }
        const auto store{store_};
        subscriptions_.push_back(node.create_subscription<Message>(topic, qos,
            [store](typename Message::ConstSharedPtr input) { store->receive(*input); }));
    }};
    subscribe(sensor_msgs::msg::Imu{}, "/mavros/imu/data", true);
    subscribe(mavros_msgs::msg::State{}, "/mavros/state", false);
    subscribe(mavros_msgs::msg::ExtendedState{}, "/mavros/extended_state", false);
    subscribe(sensor_msgs::msg::BatteryState{}, "/mavros/battery", false);
    subscribe(mavros_msgs::msg::GPSRAW{}, "/mavros/gpsstatus/gpsraw", true);
    subscribe(sensor_msgs::msg::NavSatFix{}, "/mavros/global_position/global", true);
    subscribe(sensor_msgs::msg::FluidPressure{}, "/fpv/static_pressure", true);
    subscribe(sensor_msgs::msg::Temperature{}, "/fpv/temperature_baro", true);
    subscribe(std_msgs::msg::Bool{}, "/fpv/failsafe", false);
    subscribe(std_msgs::msg::Int8{}, "/fpv/radar_state", false);
    const auto store{store_};
#ifdef DRONE_OSD_LOW_LEVEL_FEEDBACK
    subscriptions_.push_back(subscribeLowLevelFeedback(node, [store](int level) { store->receiveBatteryWarning(level); }));
#endif
    timer_ = node.create_wall_timer(std::chrono::milliseconds{100},
        [store, sink = std::move(sink)] { sink(store->snapshot()); });
}
} // namespace osd
