#ifndef OSD_ROS_TELEMETRY_H
#define OSD_ROS_TELEMETRY_H

#include <mutex>

#include "osd/presentation.h"

#include <rclcpp/rclcpp.hpp>
#include <mavros_msgs/msg/state.hpp>
#include <mavros_msgs/msg/extended_state.hpp>
#include <mavros_msgs/msg/gpsraw.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <sensor_msgs/msg/fluid_pressure.hpp>
#include <sensor_msgs/msg/temperature.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/int8.hpp>

namespace osd {
/** @brief 校验遥测并按单调时钟生成独立快照；不接触显示硬件。 */
class TelemetryStore {
public:
    explicit TelemetryStore(std::chrono::milliseconds timeout = std::chrono::milliseconds{2000},
                            int battery_cells = 0,
                            std::function<SteadyClock::time_point()> now = SteadyClock::now);
    void receive(const sensor_msgs::msg::Imu &message);
    void receive(const mavros_msgs::msg::State &message);
    void receive(const mavros_msgs::msg::ExtendedState &message);
    void receive(const sensor_msgs::msg::BatteryState &message);
    void receive(const mavros_msgs::msg::GPSRAW &message);
    void receive(const sensor_msgs::msg::NavSatFix &message);
    void receive(const sensor_msgs::msg::FluidPressure &message);
    void receive(const sensor_msgs::msg::Temperature &message);
    void receive(const std_msgs::msg::Bool &message);
    void receive(const std_msgs::msg::Int8 &message);
    /** @brief 可选低层适配传入电池等级；0 未知，1 正常，2 低，3 严重。 */
    void receiveBatteryWarning(int level);
    TelemetrySnapshot snapshot() const;
private:
    template <typename T> bool fresh(const Measurement<T> &value, SteadyClock::time_point now) const {
        return value.value && now >= value.received && now - value.received <= timeout_;
    }
    const std::chrono::milliseconds timeout_;
    const int cells_;
    const std::function<SteadyClock::time_point()> now_;
    mutable std::mutex mutex_{};
    TelemetrySnapshot data_{};
    bool armed_{};
    SteadyClock::time_point armed_at_{};
    std::optional<Position> home_{};
    std::optional<double> ground_pressure_{};
};
/** @brief 复用节点执行器订阅外部遥测，以 10 Hz 向显示侧提交快照。 */
class OsdTelemetryAdapter {
public:
    OsdTelemetryAdapter(rclcpp::Node &node, std::function<void(TelemetrySnapshot)> sink,
                        std::chrono::milliseconds timeout, int battery_cells);
    TelemetryStore &store() { return *store_; }
private:
    std::shared_ptr<TelemetryStore> store_;
    std::vector<rclcpp::SubscriptionBase::SharedPtr> subscriptions_{};
    rclcpp::TimerBase::SharedPtr timer_{};
};
} // namespace osd
#endif // OSD_ROS_TELEMETRY_H
