#include "osd/mission_state.h"

#include <iostream>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using Policy = osd::MissionState;
using Phase = osd::MissionPhase;
Policy::Clock::time_point at(int milliseconds) { return Policy::Clock::time_point{1s + 1ms * milliseconds}; }
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
void flight(Policy &policy, int time, bool guided = true, bool armed = true, bool connected = true) {
    policy.flight(connected, armed, guided, at(time), at(time));
}
void contracts() {
    for (bool flight_first : {false, true}) {
        Policy policy{};
        if (flight_first) { flight(policy, 0); }
        policy.mission(true, at(0));
        if (!flight_first) {
            require(policy.phase() == Phase::Pending, "missing flight status confirmed mission");
            policy.expire(at(9000));
            require(policy.phase() == Phase::Pending, "never-received flight invalidated pending");
            flight(policy, 9000);
        }
        require(policy.active(), "input arrival order prevented confirmation");
        const auto attempt{policy.attempt()};
        require(policy.mission(true, at(9000)).count == 0 && policy.attempt() == attempt,
            "repeated true renewed attempt");
        policy.mission(false, at(9000));
        require(!policy.active() && policy.phase() == Phase::Inactive, "mission end retained banner");
        policy.cancel(false, at(9000));
        policy.cancel(true, at(9000));
        require(policy.phase() == Phase::Inactive, "cancellation erased observed false boundary");
        flight(policy, 9000); policy.mission(true, at(9000));
        require(policy.active() && policy.attempt() == attempt + 1, "later start failed");
    }
    Policy entry{};
    flight(entry, 0, false); entry.mission(true, at(0));
    require(entry.phase() == Phase::Pending, "ACRO entry invalidated attempt");
    flight(entry, 50, false); flight(entry, 100);
    require(entry.active(), "OFFBOARD entry did not confirm");
    flight(entry, 110, false); flight(entry, 120);
    require(entry.phase() == Phase::Invalidated, "brief OFFBOARD exit resurrected mission");
    entry.mission(true, at(120));
    require(!entry.active(), "repeated true resurrected invalidated attempt");
    entry.mission(false, at(130)); entry.mission(true, at(140));
    require(entry.active(), "false then true did not reauthorize");
    for (int failure : {0, 1, 2, 3}) {
        for (bool pending : {false, true}) {
            Policy policy{};
            flight(policy, 0, !pending); policy.mission(true, at(0));
            if (failure == 0) { flight(policy, 10, true, false); }
            if (failure == 1) { flight(policy, 10, true, true, false); }
            if (failure == 2) { policy.expire(at(2001)); }
            if (failure == 3) { policy.flight(true, true, true, at(2001), at(2001)); }
            require(policy.phase() == Phase::Invalidated, "authorization loss did not invalidate");
            flight(policy, 2010); policy.mission(true, at(2010));
            require(!policy.active(), "recovery resurrected invalidated attempt");
        }
    }
    for (bool dismiss : {false, true}) {
        Policy policy{};
        policy.cancel(dismiss, at(0));
        policy.cancel(dismiss, at(1));
        flight(policy, 10); policy.mission(true, at(20));
        require(!policy.active(), "cancellation before delayed true did not block");
        policy.mission(false, at(30)); policy.mission(true, at(40));
        require(policy.active(), "cancellation permanently blocked new attempt");
    }
    Policy stale{};
    flight(stale, 0); stale.expire(at(3000)); stale.mission(true, at(3000));
    require(stale.phase() == Phase::Invalidated, "old cached flight authorized new attempt");
    Policy long_mission{};
    flight(long_mission, 0); long_mission.mission(true, at(0));
    for (int time{1000}; time <= 10000; time += 1000) {
        flight(long_mission, time); long_mission.expire(at(time));
        require(long_mission.active(), "mission silence expired a healthy mission");
    }
    long_mission.expire(at(12000));
    require(long_mission.active(), "inclusive freshness boundary changed");
    long_mission.expire(at(12001));
    require(!long_mission.active(), "flight expiry did not clear banner");
}
} // namespace
int main() {
    try { contracts(); std::cout << "Mission state contracts passed\n"; return 0; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
