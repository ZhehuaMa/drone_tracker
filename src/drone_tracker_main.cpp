#include "drone_tracker_node.h"
#include "platform/cpu_affinity.h"

#include <iostream>

int main(int argc, char *argv[]) {
    int result{0};
    try {
        cpu_affinity::initializeControl();
        rclcpp::init(argc, argv);
        auto node{std::make_shared<DroneTrackerNode>()};
        rclcpp::spin(node);
    } catch (const std::exception &error) {
        std::cerr << "drone_tracker initialization/execution failed: " << error.what() << '\n';
        result = 1;
    }
    rclcpp::shutdown();
    return result;
}
