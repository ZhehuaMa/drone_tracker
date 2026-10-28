#include "osd/ros_telemetry.h"
#include "osd/mission_state.h"

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
        int rssi_time{};
        osd::TelemetryStore link{std::chrono::milliseconds{2000}, 0,
            [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{rssi_time}}; }};
        require(!link.snapshot().rssi_percent.value, "startup RSSI invented");
        mavros_msgs::msg::RCIn rc{};
        for (int percent : {0, 85, 100}) {
            rc.rssi = static_cast<std::uint8_t>(percent); link.receive(rc);
            require(link.snapshot().rssi_percent.value == percent, "RSSI percentage rescaled or lost");
        }
        for (int invalid : {101, 254, 255}) {
            rc.rssi = 85; link.receive(rc);
            rc.rssi = static_cast<std::uint8_t>(invalid); link.receive(rc);
            require(!link.snapshot().rssi_percent.value, "invalid RSSI retained old percentage");
        }
        rc.rssi = 85; link.receive(rc);
        rssi_time = 2000;
        require(link.snapshot().rssi_percent.value == 85, "RSSI expired at inclusive boundary");
        rssi_time = 2001;
        battery.present = true; battery.voltage = 22.2; link.receive(battery);
        temperature.temperature = 31; link.receive(temperature);
        const auto link_expired{link.snapshot()};
        require(!link_expired.rssi_percent.value && link_expired.battery.value
            && link_expired.temperature_c.value == 31, "unrelated telemetry refreshed RSSI");
        rc.rssi = 9; link.receive(rc);
        require(link.snapshot().rssi_percent.value == 9, "RSSI did not recover");
        int position_time{};
        osd::TelemetryStore coordinates{std::chrono::milliseconds{2000}, 0,
            [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{position_time}}; }};
        require(!coordinates.snapshot().position.value && !coordinates.snapshot().flight.value,
            "startup has invented telemetry");
        sensor_msgs::msg::NavSatFix fix{};
        fix.status.status = 0; fix.latitude = 0; fix.longitude = 0;
        coordinates.receive(fix);
        require(coordinates.snapshot().position.value && coordinates.snapshot().position.value->latitude_deg == 0
            && !coordinates.snapshot().home_distance_m && !coordinates.snapshot().gps.value,
            "zero fix requires arming, home, or GPSRAW");
        for (int invalid{0}; invalid < 6; ++invalid) {
            fix.status.status = 0; fix.latitude = 31.2304; fix.longitude = 121.4737;
            coordinates.receive(fix);
            switch (invalid) {
            case 0: fix.status.status = -1; break;
            case 1: fix.latitude = 91; break;
            case 2: fix.longitude = -181; break;
            case 3: fix.latitude = std::numeric_limits<double>::quiet_NaN(); break;
            case 4: fix.longitude = std::numeric_limits<double>::infinity(); break;
            case 5: fix.latitude = -91; break;
            }
            coordinates.receive(fix);
            require(!coordinates.snapshot().position.value, "invalid position retained old coordinates");
        }
        fix.status.status = 0; fix.latitude = 31.2304; fix.longitude = 121.4737;
        coordinates.receive(fix);
        position_time = 2000;
        require(coordinates.snapshot().position.value.has_value(), "position expires before timeout boundary");
        position_time = 2001; coordinates.receive(battery);
        require(!coordinates.snapshot().position.value && coordinates.snapshot().battery.value,
            "position does not expire independently");
        coordinates.receive(fix);
        require(coordinates.snapshot().position.value->longitude_deg == 121.4737, "position did not recover");
        flight.connected = true; flight.armed = true; coordinates.receive(flight);
        require(coordinates.snapshot().flight.value->armed, "armed state missing");
        flight.connected = false; coordinates.receive(flight);
        require(!coordinates.snapshot().flight.value->connected, "disconnected state hidden");
        position_time += 2001;
        require(!coordinates.snapshot().flight.value, "flight state did not expire");
        flight.connected = true; flight.armed = false; coordinates.receive(flight);
        require(coordinates.snapshot().flight.value->connected && !coordinates.snapshot().flight.value->armed,
            "flight state did not recover");
        int mission_time{};
        const auto mission_now{[&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{mission_time}}; }};
        osd::TelemetryStore mission_store{std::chrono::milliseconds{2000}, 0, mission_now};
        osd::MissionState mission{};
        flight.connected = true; flight.armed = true; flight.guided = true; flight.mode = "OFFBOARD|ANGLE";
        const auto original{mission_store.receive(flight)};
        require(original.value->guided && original.value->mode == "OFFBOARD|ANGLE", "guided conversion lost");
        mission.flight(true, true, original.value->guided, original.received, mission_now());
        mission.mission(true, mission_now());
        const auto calendar_before{std::chrono::system_clock::now()};
        const auto corrected_calendar{calendar_before + std::chrono::hours{24}};
        (void)corrected_calendar; // Calendar values never enter the elapsed-time API.
        mission_time = 100;
        require(mission_store.snapshot().flight.received == original.received, "snapshot renewed receipt");
        mission.expire(mission_now());
        require(mission.active(), "calendar correction altered expiry");
        mission_time = 2001;
        for (int count{0}; count < 10; ++count) {
            require(mission_store.snapshot().flight.received == original.received, "summary changed original receipt");
        }
        mission.expire(mission_now());
        require(!mission.active() && !mission_store.snapshot().flight.value, "summaries kept flight alive");
        mission.mission(false, mission_now());
        for (mission_time = 2001; mission_time < 65000; mission_time += 1000) {
            const auto report{mission_store.receive(flight)};
            mission.flight(true, true, report.value->guided, report.received, mission_now());
            if (mission_time == 2001) { mission.mission(true, mission_now()); }
            mission.expire(mission_now());
            require(mission.active(), "event silence expired healthy long mission");
        }
        std::cout << "OSD telemetry contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
