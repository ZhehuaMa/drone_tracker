#include "platform/cpu_affinity.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sched.h>
#include <unistd.h>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int64_multi_array.hpp>

namespace {
using Clock = std::chrono::steady_clock;
using Message = std_msgs::msg::UInt64MultiArray;
std::uint64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::string mask() {
    cpu_set_t actual{};
    require(::sched_getaffinity(0, sizeof(actual), &actual) == 0, "cannot read affinity");
    std::string result{};
    for (int i{}; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &actual)) { result += (result.empty() ? "" : ",") + std::to_string(i); }
    }
    return result;
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) { return 0; }
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>((values.size() - 1) * fraction)];
}
void statistics(const char *label, const std::vector<double> &values) {
    std::cout << '"' << label << "\":{\"samples\":" << values.size()
              << ",\"p50_ms\":" << percentile(values, .5) << ",\"p95_ms\":" << percentile(values, .95)
              << ",\"max_ms\":" << percentile(values, 1) << '}';
}
} // namespace

int main(int argc, char **argv) {
    bool publisher{};
    int samples{100}, period_ms{10}, skip{-1};
    double maximum_ms{1000};
    for (int i{1}; i < argc; ++i) {
        const std::string argument{argv[i]};
        if (argument == "--publisher") { publisher = true; }
        else if (argument == "--samples" && i + 1 < argc) { samples = std::stoi(argv[++i]); }
        else if (argument == "--period-ms" && i + 1 < argc) { period_ms = std::stoi(argv[++i]); }
        else if (argument == "--skip-sequence" && i + 1 < argc) { skip = std::stoi(argv[++i]); }
        else if (argument == "--max-latency-ms" && i + 1 < argc) { maximum_ms = std::stod(argv[++i]); }
        else { std::cerr << "Unknown/incomplete argument: " << argument << '\n'; return 2; }
    }
    int result{};
    try {
        require(samples > 1 && period_ms > 0 && maximum_ms >= 0, "invalid probe parameters");
        cpu_affinity::apply(cpu_affinity::Role::RosTransport);
        int ros_argc{1};
        rclcpp::init(ros_argc, argv);
        auto node{std::make_shared<rclcpp::Node>(publisher ? "affinity_probe_publisher" : "affinity_probe_receiver")};
        const auto creation_mask{mask()};
        if (!publisher) { cpu_affinity::initializeControl(); }
        rclcpp::executors::SingleThreadedExecutor executor{};
        executor.add_node(node);
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "{\"event\":\"ready\",\"pid\":" << ::getpid() << ",\"publisher\":"
                  << publisher << ",\"creation_mask\":\"" << creation_mask << "\",\"execution_mask\":\""
                  << mask() << "\"}" << std::endl;
        for (int round{}; round < 2; ++round) {
            const std::string topic{"/affinity_probe/round_" + std::to_string(round)};
            if (publisher) {
                rclcpp::Publisher<Message>::SharedPtr output{};
                cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [&] {
                    output = node->create_publisher<Message>(topic, rclcpp::QoS{100}.reliable());
                });
                const auto deadline{Clock::now() + std::chrono::seconds{10}};
                while (output->get_subscription_count() == 0 && Clock::now() < deadline) {
                    executor.spin_once(std::chrono::milliseconds{20});
                }
                require(output->get_subscription_count() > 0, "receiver discovery timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
                auto next{Clock::now()};
                for (int sequence{}; sequence < samples; ++sequence) {
                    if (sequence != skip) {
                        Message message{};
                        message.data = {static_cast<std::uint64_t>(round), static_cast<std::uint64_t>(sequence),
                            monotonicNs()};
                        output->publish(message);
                    }
                    next += std::chrono::milliseconds{period_ms};
                    std::this_thread::sleep_until(next);
                }
                std::cout << "{\"event\":\"published\",\"round\":" << round << ",\"samples\":"
                          << samples - (skip >= 0 && skip < samples ? 1 : 0) << "}" << std::endl;
            } else {
                std::set<std::uint64_t> sequences{};
                std::vector<double> callback{}, middleware{};
                bool last{}, wrong_core{};
                rclcpp::Subscription<Message>::SharedPtr input{};
                cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [&] {
                    input = node->create_subscription<Message>(topic, rclcpp::QoS{100}.reliable(),
                        [&](const Message::SharedPtr message, const rclcpp::MessageInfo &info) {
                            const auto now{monotonicNs()};
                            if (message->data.size() != 3 || message->data[0] != static_cast<std::uint64_t>(round)) {
                                return;
                            }
                            const auto sequence{message->data[1]};
                            require(sequence < static_cast<std::uint64_t>(samples), "invalid sequence");
                            require(now >= message->data[2], "invalid monotonic timestamp");
                            sequences.insert(sequence);
                            callback.push_back((now - message->data[2]) / 1e6);
                            const auto &metadata{info.get_rmw_message_info()};
                            if (metadata.source_timestamp > 0 && metadata.received_timestamp >= metadata.source_timestamp) {
                                middleware.push_back((metadata.received_timestamp - metadata.source_timestamp) / 1e6);
                            }
                            const auto &policy{cpu_affinity::current()};
                            if (policy.configured && policy.mode == cpu_affinity::Mode::RolePinned) {
                                wrong_core = wrong_core || mask() != std::to_string(policy.control);
                            }
                            last = sequence + 1 == static_cast<std::uint64_t>(samples);
                        });
                });
                const auto deadline{Clock::now() + std::chrono::milliseconds{samples * period_ms + 12000}};
                while (!last && rclcpp::ok() && Clock::now() < deadline) {
                    executor.spin_once(std::chrono::milliseconds{20});
                }
                const auto lost{samples - sequences.size()};
                std::cout << "{\"event\":\"received\",\"round\":" << round << ",\"samples\":"
                          << sequences.size() << ",\"lost\":" << lost << ",\"callback_mask\":\"" << mask()
                          << "\",\"callback_clock\":\"steady_clock_monotonic\",\"dds_clock\":\"RMW_system_clock\",";
                statistics("callback", callback); std::cout << ','; statistics("dds", middleware);
                std::cout << '}' << std::endl;
                require(!wrong_core, "callback left control CPU");
                require(last && lost == 0, "probe lost messages or timed out");
                require(percentile(callback, 1) < maximum_ms, "probe exceeded callback latency threshold");
                require(middleware.empty() || percentile(middleware, 1) < maximum_ms,
                    "probe exceeded middleware latency threshold");
            }
        }
        executor.remove_node(node);
        node.reset();
    } catch (const std::exception &error) {
        std::cerr << "ROS transport probe: " << error.what() << '\n'; result = 1;
    }
    if (rclcpp::ok()) { rclcpp::shutdown(); }
    return result;
}
