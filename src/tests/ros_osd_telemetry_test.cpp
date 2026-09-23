#include "osd/presenter.h"
#include "osd/ros_telemetry.h"

#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
#include <quadrotor_msgs/msg/low_level_feedback.hpp>
#endif

namespace {
struct Output {
    std::mutex mutex{};
    osd::PresentationState state{};
};
class CaptureDisplay final : public osd::DisplayBackend {
public:
    explicit CaptureDisplay(Output &output) : output_{output} {}
    void initialize() override {}
    void prepareVideo(const Frame &) override {}
    bool present(const osd::PresentationState &state, const std::function<bool()> &authorize) override {
        if (!authorize()) { return false; }
        std::lock_guard<std::mutex> lock{output_.mutex};
        output_.state = state;
        return true;
    }
    void discardPrepared() noexcept override {}
    void interrupt() noexcept override {}
    void shutdown() noexcept override {}
private:
    Output &output_;
};
}
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    try {
        Output output{};
        osd::OsdPresenter presenter{std::make_unique<CaptureDisplay>(output)};
        presenter.start();
        const auto round{presenter.beginRound()};
        presenter.endRound(round, osd::DisplayStatus::Stopped);
        rclcpp::NodeOptions options{};
        options.arguments({"--ros-args", "-r", "/mavros/battery:=/osd_test/battery",
                            "-r", "/fpv/temperature_baro:=/osd_test/temperature"});
        auto node{std::make_shared<rclcpp::Node>("osd_test_subscriber", options)};
        osd::OsdTelemetryAdapter adapter{*node,
            [&](osd::TelemetrySnapshot snapshot) { presenter.updateTelemetry(std::move(snapshot)); },
            std::chrono::milliseconds{300}, 0};
        auto source{std::make_shared<rclcpp::Node>("osd_test_publisher")};
        auto battery{source->create_publisher<sensor_msgs::msg::BatteryState>("/osd_test/battery", 10)};
        auto temperature{source->create_publisher<sensor_msgs::msg::Temperature>(
            "/osd_test/temperature", rclcpp::QoS{10}.best_effort())};
        auto radar{source->create_publisher<std_msgs::msg::Int8>("/fpv/radar_state", 10)};
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
        auto feedback{source->create_publisher<quadrotor_msgs::msg::LowLevelFeedback>("/fpv/low_level_feedback", 10)};
#endif
        rclcpp::executors::SingleThreadedExecutor executor{};
        executor.add_node(node);
        executor.add_node(source);
        sensor_msgs::msg::BatteryState value{};
        value.present = true; value.voltage = 24; value.capacity = 13; value.charge = 10;
        sensor_msgs::msg::Temperature temp{}; temp.temperature = 28;
        bool received{false}, expired{false};
        const auto deadline{osd::SteadyClock::now() + std::chrono::seconds{5}};
        while (osd::SteadyClock::now() < deadline) {
            if (!received) { battery->publish(value); }
            temperature->publish(temp);
            std_msgs::msg::Int8 radar_message{}; radar_message.data = 2; radar->publish(radar_message);
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
            quadrotor_msgs::msg::LowLevelFeedback feedback_message{};
            feedback_message.battery_state = quadrotor_msgs::msg::LowLevelFeedback::BAT_CRITICAL;
            feedback->publish(feedback_message);
#endif
            executor.spin_some();
            {
                std::lock_guard<std::mutex> lock{output.mutex};
                const auto &state{output.state};
                if (received && (state.status != osd::DisplayStatus::Stopped || !state.boxes.empty())) {
                    throw std::runtime_error("telemetry changed stopped presentation state");
                }
                if (state.telemetry.battery.value && state.telemetry.battery.value->consumed_mah == 3000
                    && state.telemetry.temperature_c.value == 28 && state.telemetry.radar_state.value == 2
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
                    && state.telemetry.battery_warning.value == 3
#endif
                    ) {
                    if (state.tracker_state != 0) { throw std::runtime_error("radar overwrote local tracker state"); }
                    received = true;
                }
                if (received && !state.telemetry.battery.value && state.telemetry.temperature_c.value == 28) {
                    expired = true;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        if (!received || !expired || battery->get_subscription_count() == 0
            || temperature->get_subscription_count() == 0) {
            throw std::runtime_error("ROS remap, QoS or independent expiry failed");
        }
        presenter.shutdown();
        rclcpp::shutdown();
        std::cout << "ROS OSD telemetry integration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        rclcpp::shutdown();
        return 1;
    }
}
