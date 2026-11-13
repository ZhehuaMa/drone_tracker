#ifndef DRONE_TRACKER_RECORDING_TOPIC_CATALOG_H
#define DRONE_TRACKER_RECORDING_TOPIC_CATALOG_H

#include <cstddef>
#include <string>
#include <vector>

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <mavros_msgs/msg/extended_state.hpp>
#include <mavros_msgs/msg/gpsraw.hpp>
#include <mavros_msgs/msg/rc_in.hpp>
#include <mavros_msgs/msg/state.hpp>
#include <mavros_msgs/msg/status_text.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <quadrotor_msgs/msg/control_command.hpp>
#include <quadrotor_msgs/msg/low_level_feedback.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/fluid_pressure.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <sensor_msgs/msg/temperature.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/int8.hpp>
#include <std_msgs/msg/string.hpp>
#include <vision_msgs/msg/detection2_d.hpp>

// One list owns both generated C++ types and their stable catalog descriptors.
#define DRONE_RECORDING_TOPICS(X) \
    X(body_imu, sensor_msgs, Imu, imu, "/mavros/imu/data", Header, Subscription, 100, false, BestEffort, 100) \
    X(gimbal, sensor_msgs, Imu, imu, "/fpv/gimbal", Header, Subscription, 100, false, BestEffort, 10) \
    X(odom, nav_msgs, Odometry, odometry, "/fpv/odom", Header, Subscription, 50, false, BestEffort, 10) \
    X(rc_in, mavros_msgs, RCIn, rc_in, "/mavros/rc/in", Header, Subscription, 25, false, BestEffort, 10) \
    X(flight_state, mavros_msgs, State, state, "/mavros/state", Header, Subscription, 0, false, Reliable, 10) \
    X(extended_state, mavros_msgs, ExtendedState, extended_state, "/mavros/extended_state", Header, \
        Subscription, 0, false, Reliable, 10) \
    X(battery, sensor_msgs, BatteryState, battery_state, "/mavros/battery", Header, Subscription, 0, false, \
        Reliable, 10) \
    X(pressure, sensor_msgs, FluidPressure, fluid_pressure, "/fpv/static_pressure", Header, Subscription, \
        25, false, BestEffort, 10) \
    X(temperature, sensor_msgs, Temperature, temperature, "/fpv/temperature_baro", Header, Subscription, \
        25, false, BestEffort, 10) \
    X(low_level_feedback, quadrotor_msgs, LowLevelFeedback, low_level_feedback, "/fpv/low_level_feedback", \
        Header, Subscription, 0, false, Reliable, 10) \
    X(gimbal_mode, std_msgs, String, string, "/fpv/gimbal_mode", Receipt, Subscription, 0, true, Retained, 1) \
    X(trigger, std_msgs, Header, header, "/fpv/tracker_trigger", Stamp, Direct, 0, true, Direct, 1) \
    X(altitude, std_msgs, Float64, float64, "/fpv/altitude", Receipt, Subscription, 25, false, BestEffort, 10) \
    X(gps_raw, mavros_msgs, GPSRAW, gpsraw, "/mavros/gpsstatus/gpsraw", Header, Subscription, 0, false, \
        BestEffort, 10) \
    X(gps_fix, sensor_msgs, NavSatFix, nav_sat_fix, "/mavros/global_position/global", Header, Subscription, \
        0, false, BestEffort, 10) \
    X(failsafe, std_msgs, Bool, bool, "/fpv/failsafe", Receipt, Subscription, 0, true, Reliable, 10) \
    X(motor_rpm, std_msgs, Float32MultiArray, float32_multi_array, "/fpv/motor_rpm", Receipt, Subscription, \
        25, false, BestEffort, 10) \
    X(control, quadrotor_msgs, ControlCommand, control_command, "/fpv/control_command", Header, \
        Subscription, 0, false, BestEffort, 10) \
    X(control_raw, quadrotor_msgs, ControlCommand, control_command, "/fpv/control_command_raw", Header, \
        Subscription, 0, false, BestEffort, 10) \
    X(arm, std_msgs, Bool, bool, "/fpv/arm", Receipt, Subscription, 0, true, Reliable, 10) \
    X(offboard, std_msgs, Bool, bool, "/fpv/offboard", Receipt, Subscription, 0, true, Reliable, 10) \
    X(tracker_state, std_msgs, Int8, int8, "/fpv/tracker_state", Receipt, Subscription, 0, true, Reliable, 10) \
    X(radar_state, std_msgs, Int8, int8, "/fpv/radar_state", Receipt, Subscription, 0, true, Reliable, 10) \
    X(mission_state, std_msgs, Bool, bool, "/fpv/mission_state", Receipt, Subscription, 0, true, Reliable, 10) \
    X(mission_start, std_msgs, Bool, bool, "/fpv/mission_start", Receipt, Subscription, 0, true, Reliable, 10) \
    X(target_bbox, vision_msgs, Detection2D, detection2_d, "/fpv/target_bbox", Header, Pair, 0, false, Direct, 10) \
    X(target_los, geometry_msgs, TwistStamped, twist_stamped, "/fpv/target_los", Header, Pair, 0, false, Direct, 10) \
    X(tracker_image, sensor_msgs, Image, image, "/fpv/tracker_image", Header, Subscription, 2, false, BestEffort, 1) \
    X(status_text, mavros_msgs, StatusText, status_text, "/mavros/statustext/send", Header, Subscription, \
        0, true, Reliable, 10) \
    X(radar_target, nav_msgs, Odometry, odometry, "/fpv/radar/target", Header, Subscription, 0, false, Reliable, 10) \
    X(radar_interceptor, nav_msgs, Odometry, odometry, "/fpv/radar/interceptor", Header, Subscription, 0, \
        false, Reliable, 10) \
    X(bench_status, std_msgs, String, string, "/bench_pid_tuner/status", Receipt, Subscription, 0, false, Reliable, 10)

namespace recording {
enum class TimestampRule { Header, Stamp, Receipt };
enum class SourceKind { Subscription, Direct, Pair };
enum class Delivery { BestEffort, Reliable, Retained, Direct };

/** @brief A fixed message contract; configuration may change names and ordinary policy only. */
struct TopicDescriptor {
    std::string id{}, name{}, type{};
    TimestampRule timestamp{};
    SourceKind source{};
    double max_hz{};
    bool protected_event{};
    Delivery delivery{};
    std::size_t depth{}, max_messages{32}, max_bytes{8U * 1024U * 1024U};
};

/** @brief Return the 31 application entries, plus the optional known bench entry. */
std::vector<TopicDescriptor> topicCatalog(bool bench = false);
} // namespace recording
#endif // DRONE_TRACKER_RECORDING_TOPIC_CATALOG_H
