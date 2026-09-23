#include "osd/low_level_feedback.h"

#include <utility>
#include <quadrotor_msgs/msg/low_level_feedback.hpp>

namespace osd {
static_assert(quadrotor_msgs::msg::LowLevelFeedback::BAT_INVALID == 0
    && quadrotor_msgs::msg::LowLevelFeedback::BAT_GOOD == 1
    && quadrotor_msgs::msg::LowLevelFeedback::BAT_LOW == 2
    && quadrotor_msgs::msg::LowLevelFeedback::BAT_CRITICAL == 3,
    "OSD requires the documented LowLevelFeedback battery-state contract");
rclcpp::SubscriptionBase::SharedPtr subscribeLowLevelFeedback(rclcpp::Node &node, std::function<void(int)> receive) {
    return node.create_subscription<quadrotor_msgs::msg::LowLevelFeedback>("/fpv/low_level_feedback", rclcpp::QoS{10},
        [receive = std::move(receive)](quadrotor_msgs::msg::LowLevelFeedback::ConstSharedPtr message) {
            receive(static_cast<int>(message->battery_state));
        });
}
} // namespace osd
