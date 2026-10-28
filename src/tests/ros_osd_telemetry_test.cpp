#include "osd/presenter.h"
#include "osd/ros_telemetry.h"

#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
#include <quadrotor_msgs/msg/low_level_feedback.hpp>
#endif

namespace {
class EventFrame final : public Frame {
public:
    int width() const noexcept override { return 2; }
    int height() const noexcept override { return 2; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return 1; }
    std::unique_ptr<FrameReadView> mapRead() const override { return {}; }
};
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
                            "-r", "/fpv/temperature_baro:=/osd_test/temperature",
                            "-r", "/mavros/rc/in:=/osd_test/rc",
                            "-r", "/mavros/state:=/osd_test/flight",
                            "-r", "/fpv/mission_state:=/osd_test/mission"});
        auto node{std::make_shared<rclcpp::Node>("osd_test_subscriber", options)};
        std::vector<bool> mission_events{};
        std::vector<osd::SteadyClock::time_point> mission_times{};
        std::vector<bool> flight_events{};
        osd::TelemetryStore *observed_store{};
        osd::OsdTelemetryAdapter adapter{*node,
            [&](osd::TelemetrySnapshot snapshot) { presenter.updateTelemetry(std::move(snapshot)); },
            std::chrono::milliseconds{300}, 0,
            [&](bool active, osd::SteadyClock::time_point received) {
                mission_events.push_back(active); mission_times.push_back(received);
                presenter.missionReceived(active, received);
            },
            [&](osd::Measurement<osd::FlightState> report) {
                // snapshot takes the store lock: this would deadlock if the sink ran under it.
                const auto snapshot{observed_store->snapshot()};
                if (snapshot.flight.received != report.received) {
                    throw std::runtime_error("flight sink lost original receipt time");
                }
                flight_events.push_back(report.value->guided);
                presenter.flightReceived(std::move(report));
            }};
        observed_store = &adapter.store();
        auto source{std::make_shared<rclcpp::Node>("osd_test_publisher")};
        auto mission{source->create_publisher<std_msgs::msg::Bool>("/osd_test/mission", 10)};
        auto flight{source->create_publisher<mavros_msgs::msg::State>("/osd_test/flight", 10)};
        auto battery{source->create_publisher<sensor_msgs::msg::BatteryState>("/osd_test/battery", 10)};
        auto temperature{source->create_publisher<sensor_msgs::msg::Temperature>(
            "/osd_test/temperature", rclcpp::QoS{10}.best_effort())};
        auto radar{source->create_publisher<std_msgs::msg::Int8>("/fpv/radar_state", 10)};
        auto rc{source->create_publisher<mavros_msgs::msg::RCIn>(
            "/osd_test/rc", rclcpp::QoS{10}.best_effort())};
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
        auto feedback{source->create_publisher<quadrotor_msgs::msg::LowLevelFeedback>("/fpv/low_level_feedback", 10)};
#endif
        rclcpp::executors::SingleThreadedExecutor executor{};
        executor.add_node(node);
        executor.add_node(source);
        sensor_msgs::msg::BatteryState value{};
        value.present = true; value.voltage = 24; value.capacity = 13; value.charge = 10;
        sensor_msgs::msg::Temperature temp{}; temp.temperature = 28;
        bool received{false}, expired{false}, recovered{false};
        mavros_msgs::msg::RCIn signal{}; signal.rssi = 85;
        const auto deadline{osd::SteadyClock::now() + std::chrono::seconds{5}};
        while (osd::SteadyClock::now() < deadline) {
            if (!received) { battery->publish(value); }
            if (!received || expired) { signal.rssi = expired ? 9 : 85; rc->publish(signal); }
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
                    && state.telemetry.rssi_percent.value == 85
#ifdef OSD_TEST_LOW_LEVEL_FEEDBACK
                    && state.telemetry.battery_warning.value == 3
#endif
                    ) {
                    if (state.tracker_state != 0) { throw std::runtime_error("radar overwrote local tracker state"); }
                    received = true;
                }
                if (received && !state.telemetry.battery.value && !state.telemetry.rssi_percent.value
                    && state.telemetry.temperature_c.value == 28) {
                    expired = true;
                }
                if (expired && state.telemetry.rssi_percent.value == 9) {
                    recovered = true;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        if (!received || !expired || !recovered || battery->get_subscription_count() == 0
            || rc->get_subscription_count() == 0
            || temperature->get_subscription_count() == 0) {
            throw std::runtime_error("ROS remap, QoS or independent expiry failed");
        }
        const auto spinUntil{[&](const auto &condition) {
            const auto limit{osd::SteadyClock::now() + std::chrono::seconds{3}};
            while (!condition()) {
                if (osd::SteadyClock::now() >= limit) { throw std::runtime_error("mission ROS delivery timeout"); }
                executor.spin_some();
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }};
        spinUntil([&] { return mission->get_subscription_count() > 0 && flight->get_subscription_count() > 0; });
        const auto active{[&] {
            std::lock_guard<std::mutex> lock{output.mutex}; return output.state.mission_active;
        }};
        mavros_msgs::msg::State authorization{};
        authorization.connected = true; authorization.armed = true; authorization.guided = true;
        authorization.mode = "OFFBOARD|ANGLE";
        const auto sendFlight{[&](bool guided) {
            authorization.guided = guided; authorization.mode = guided ? "OFFBOARD|ANGLE" : "ACRO";
            const auto count{flight_events.size()}; flight->publish(authorization);
            spinUntil([&] { return flight_events.size() == count + 1; });
        }};
        const auto sendMission{[&](bool value) {
            std_msgs::msg::Bool event{}; event.data = value;
            const auto count{mission_events.size()}; mission->publish(event);
            spinUntil([&] { return mission_events.size() == count + 1; });
        }};
        sendFlight(true);
        if (active()) { throw std::runtime_error("OFFBOARD inferred missed mission start"); }
        const auto tracking_round{presenter.beginRound()};
        if (!presenter.submit({std::make_shared<EventFrame>(), tracking_round, 1, 1, 2, {{0, 0, 1, 1, ""}}})) {
            throw std::runtime_error("tracking fixture rejected");
        }
        sendMission(true); spinUntil(active);
        sendMission(false); spinUntil([&] { return !active(); });
        {
            std::lock_guard<std::mutex> lock{output.mutex};
            if (output.state.tracker_state != 2 || output.state.boxes.empty()) {
                throw std::runtime_error("mission end altered unchanged Tracking=2");
            }
        }
        sendFlight(true);
        const auto rapid_start{mission_events.size()};
        for (bool value : {true, false, true}) {
            std_msgs::msg::Bool event{}; event.data = value; mission->publish(event);
        }
        spinUntil([&] { return mission_events.size() == rapid_start + 3; });
        if (!mission_events[rapid_start] || mission_events[rapid_start + 1] || !mission_events[rapid_start + 2]
            || mission_times.back() - mission_times[rapid_start] >= std::chrono::milliseconds{100}) {
            throw std::runtime_error("rapid mission transitions coalesced or exceeded summary interval");
        }
        spinUntil(active);
        const auto before_exit{flight_events.size()};
        authorization.guided = false; flight->publish(authorization);
        authorization.guided = true; flight->publish(authorization);
        spinUntil([&] { return flight_events.size() == before_exit + 2; });
        spinUntil([&] { return !active(); });
        sendMission(true);
        if (active()) { throw std::runtime_error("brief received OFFBOARD loss resurrected attempt"); }
        sendMission(false); sendMission(true); spinUntil(active);
        sendMission(false); spinUntil([&] { return !active(); });
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
