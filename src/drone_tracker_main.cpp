#include "drone_tracker_node.h"
#include "platform/cpu_affinity.h"
#include "diagnostics/measurement.h"

#include <iostream>

int main(int argc, char *argv[]) {
#ifdef DRONE_TRACKER_RK3576
    if (argc == 2 && std::string{argv[1]} == "--diagnostic-protocol-version") {
        std::cout << diagnostics::FORMAT_VERSION << '\n'; return 0;
    }
#endif
    int result{0};
    const char *measurement_directory{std::getenv("DRONE_TRACKER_ACCEPTANCE_DIRECTORY")};
    std::unique_ptr<diagnostics::Measurement> measurements{};
    if (measurement_directory) {
        measurements = std::make_unique<diagnostics::Measurement>();
        diagnostics::measurement = measurements.get();
    }
    try {
        cpu_affinity::apply(cpu_affinity::Role::RosTransport);
        rclcpp::init(argc, argv);
        auto node{std::make_shared<DroneTrackerNode>()};
        cpu_affinity::initializeControl();
        node->startService();
        rclcpp::executors::SingleThreadedExecutor executor{};
        executor.add_node(node);
        while (rclcpp::ok()) {
            executor.spin_once(std::chrono::milliseconds{100});
            if (node->serviceFailed()) { throw std::runtime_error(node->serviceFailure()); }
        }
        node->shutdown();
        if (node->serviceFailed()) { throw std::runtime_error(node->serviceFailure()); }
    } catch (const std::exception &error) {
        std::cerr << "drone_tracker initialization/execution failed: " << error.what() << '\n';
        result = 1;
    }
    rclcpp::shutdown();
    if (measurement_directory) {
        try { diagnostics::exportMeasurement(measurement_directory); }
        catch (const std::exception &error) { std::cerr << error.what() << '\n'; result = 1; }
        diagnostics::measurement = nullptr;
    }
    return result;
}
