#ifndef OSD_LOW_LEVEL_FEEDBACK_H
#define OSD_LOW_LEVEL_FEEDBACK_H

#include <functional>
#include <rclcpp/rclcpp.hpp>

namespace osd {
/** @brief 可选自定义消息边界；公共显示代码不包含 quadrotor_msgs。 */
rclcpp::SubscriptionBase::SharedPtr subscribeLowLevelFeedback(rclcpp::Node &node, std::function<void(int)> receive);
} // namespace osd
#endif // OSD_LOW_LEVEL_FEEDBACK_H
