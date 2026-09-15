#include "drone_tracker_node.h"

#include <iostream>

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    int result{0};
    try {
        auto node{std::make_shared<DroneTrackerNode>()};
        rclcpp::spin(node);
    } catch (const std::exception &error) {
        std::cerr << "drone_tracker initialization/execution failed: " << error.what() << '\n';
        result = 1;
    }
    rclcpp::shutdown();
    return result;
}
