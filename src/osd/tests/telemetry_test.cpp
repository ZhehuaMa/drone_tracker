#include "osd/ros_telemetry.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
}
int main() {
    try {
        int time_ms{0};
        osd::TelemetryStore store{std::chrono::milliseconds{2000}, 0,
            [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{time_ms}}; }};
        require(!store.snapshot().attitude.value, "unknown attitude is zero");
        sensor_msgs::msg::Imu imu{};
        imu.orientation.w = 2;
        store.receive(imu);
        require(std::abs(store.snapshot().attitude.value->heading_deg - 90) < 1e-9, "ENU heading wrong");
        imu.orientation_covariance[0] = -1;
        store.receive(imu);
        require(!store.snapshot().attitude.value, "unknown orientation accepted");
        imu.orientation_covariance[0] = 0;
        imu.orientation.w = std::numeric_limits<double>::quiet_NaN();
        store.receive(imu);
        require(!store.snapshot().attitude.value, "NaN orientation accepted");
        sensor_msgs::msg::BatteryState battery{};
        battery.present = true;
        battery.capacity = 13;
        battery.charge = 10;
        battery.voltage = 22.2;
        battery.current = -3;
        battery.percentage = 0.5;
        store.receive(battery);
        require(store.snapshot().battery.value->consumed_mah == 3000, "charge units wrong");
        require(store.snapshot().battery.value->percentage == 0.5, "valid battery percentage missing");
        require(!store.snapshot().battery.value->cell_voltage, "cell count guessed");
        battery.capacity = std::numeric_limits<float>::quiet_NaN();
        battery.percentage = -1;
        store.receive(battery);
        require(!store.snapshot().battery.value->consumed_mah, "missing capacity accepted");
        require(!store.snapshot().battery.value->percentage, "unknown percentage accepted");
        mavros_msgs::msg::GPSRAW gps{};
        gps.fix_type = 3;
        gps.vel = 1234;
        gps.cog = 12345;
        gps.satellites_visible = 12;
        store.receive(gps);
        require(std::abs(*store.snapshot().gps.value->speed_mps - 12.34) < 1e-9, "speed units");
        require(store.snapshot().gps.value->fix_type == 3, "GPS fix level missing");
        gps.vel = 65535; gps.cog = 65535; gps.satellites_visible = 255; gps.fix_type = 255;
        store.receive(gps);
        require(!store.snapshot().gps.value, "GPS sentinel accepted");
        mavros_msgs::msg::State flight{};
        flight.connected = true;
        flight.armed = true;
        store.receive(flight);
        sensor_msgs::msg::NavSatFix position{};
        position.status.status = 0;
        position.latitude = 30;
        position.longitude = 120;
        store.receive(position);
        require(!store.snapshot().home_distance_m, "late GPS created home");
        flight.armed = false; store.receive(flight);
        mavros_msgs::msg::ExtendedState landed{};
        landed.landed_state = 1; store.receive(landed);
        sensor_msgs::msg::FluidPressure pressure{};
        pressure.fluid_pressure = 100000; store.receive(pressure);
        require(std::abs(*store.snapshot().altitude_m) < 1e-9, "ground baseline missing");
        flight.armed = true; store.receive(flight);
        position.latitude = 30.001; store.receive(position);
        pressure.fluid_pressure = 99000; store.receive(pressure);
        require(*store.snapshot().home_distance_m > 100, "home distance wrong");
        require(*store.snapshot().altitude_m > 80, "armed pressure baseline reset");
        std_msgs::msg::Bool failsafe{}; failsafe.data = true; store.receive(failsafe);
        store.receiveBatteryWarning(3);
        require(store.snapshot().warning == "FAILSAFE", "warning priority");
        require(store.snapshot().battery_warning.value == 3, "battery grade missing");
        flight.connected = false; store.receive(flight);
        require(store.snapshot().warning == "CONNECTION LOST", "connection priority");
        time_ms = 2100;
        sensor_msgs::msg::Temperature temperature{}; temperature.temperature = 20; store.receive(temperature);
        const auto expired{store.snapshot()};
        require(!expired.flight.value && !expired.battery.value && expired.temperature_c.value == 20,
            "groups do not expire independently");
        require(!expired.gps.value && !expired.battery_warning.value, "GPS or battery grade did not expire");
        require(expired.warning == "CONNECTION UNKNOWN", "expired alert retained");
        require(!expired.altitude_m && !expired.home_distance_m, "expired derived state retained");
        temperature.temperature = std::numeric_limits<double>::infinity(); store.receive(temperature);
        require(!store.snapshot().temperature_c.value, "infinite temperature accepted");
        std::cout << "OSD telemetry contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
