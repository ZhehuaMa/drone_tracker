#include "osd/mission_state.h"

#include <stdexcept>

namespace osd {
const char *missionPhaseName(MissionPhase phase) noexcept {
    switch (phase) {
    case MissionPhase::Unknown: return "unknown";
    case MissionPhase::Inactive: return "inactive";
    case MissionPhase::Pending: return "pending";
    case MissionPhase::Active: return "active";
    case MissionPhase::Invalidated: return "invalidated";
    }
    return "unknown";
}
MissionState::MissionState(std::chrono::milliseconds timeout) : timeout_{timeout} {
    if (timeout_.count() <= 0) { throw std::invalid_argument("Mission flight timeout must be positive"); }
}
void MissionState::transition(MissionUpdate &update, MissionPhase phase, const char *reason, Clock::time_point now) {
    if (phase == phase_) { return; }
    phase_ = phase;
    update.transitions[update.count++] = {phase, reason, attempt_, now};
}
void MissionState::evaluate(MissionUpdate &update, Clock::time_point now) {
    if ((phase_ != MissionPhase::Pending && phase_ != MissionPhase::Active) || !flight_) { return; }
    if (now < flight_->received || now - flight_->received > timeout_) {
        transition(update, MissionPhase::Invalidated, "flight_status_stale", now);
    } else if (!flight_->connected) {
        transition(update, MissionPhase::Invalidated, "disconnected", now);
    } else if (!flight_->armed) {
        transition(update, MissionPhase::Invalidated, "disarmed", now);
    } else if (flight_->guided) {
        transition(update, MissionPhase::Active, "offboard_confirmed", now);
    } else if (phase_ == MissionPhase::Active) {
        transition(update, MissionPhase::Invalidated, "offboard_lost", now);
    }
}
MissionUpdate MissionState::mission(bool active, Clock::time_point now) {
    MissionUpdate update{};
    if (!active) {
        transition(update, MissionPhase::Inactive, "mission_false", now);
    } else if (phase_ == MissionPhase::Unknown || phase_ == MissionPhase::Inactive) {
        ++attempt_;
        transition(update, MissionPhase::Pending, "mission_true", now);
        evaluate(update, now);
    }
    return update;
}
MissionUpdate MissionState::flight(bool connected, bool armed, bool guided,
                                  Clock::time_point received, Clock::time_point now) {
    MissionUpdate update{};
    // A late report cannot conceal a gap after a previously observed report expired.
    evaluate(update, now);
    flight_ = Flight{connected, armed, guided, received};
    evaluate(update, now);
    return update;
}
MissionUpdate MissionState::cancel(bool dismiss, Clock::time_point now) {
    MissionUpdate update{};
    // Preserve an end boundary already received before an idempotent cancellation.
    if (phase_ != MissionPhase::Inactive) {
        transition(update, MissionPhase::Invalidated, dismiss ? "trigger_dismiss" : "trigger_off", now);
    }
    return update;
}
MissionUpdate MissionState::expire(Clock::time_point now) {
    MissionUpdate update{};
    evaluate(update, now);
    return update;
}
} // namespace osd
