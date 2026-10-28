#ifndef OSD_MISSION_STATE_H
#define OSD_MISSION_STATE_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace osd {
enum class MissionPhase { Unknown, Inactive, Pending, Active, Invalidated };
struct MissionTransition {
    MissionPhase phase{};
    const char *reason{};
    std::uint64_t attempt{};
    std::chrono::steady_clock::time_point time{};
};
struct MissionUpdate {
    std::array<MissionTransition, 2> transitions{};
    std::size_t count{};
};
/** @brief Remembers mission events and independently expires periodic flight authorization.
 * All calls are serialized by the owner. The clock measures elapsed time, not calendar time.
 */
class MissionState {
public:
    using Clock = std::chrono::steady_clock;
    explicit MissionState(std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});
    MissionUpdate mission(bool active, Clock::time_point now);
    MissionUpdate flight(bool connected, bool armed, bool guided, Clock::time_point received, Clock::time_point now);
    MissionUpdate cancel(bool dismiss, Clock::time_point now);
    MissionUpdate expire(Clock::time_point now);
    bool active() const noexcept { return phase_ == MissionPhase::Active; }
    MissionPhase phase() const noexcept { return phase_; }
    std::uint64_t attempt() const noexcept { return attempt_; }
private:
    struct Flight {
        bool connected{}, armed{}, guided{};
        Clock::time_point received{};
    };
    void transition(MissionUpdate &update, MissionPhase phase, const char *reason, Clock::time_point now);
    void evaluate(MissionUpdate &update, Clock::time_point now);
    const std::chrono::milliseconds timeout_;
    MissionPhase phase_{MissionPhase::Unknown};
    std::optional<Flight> flight_{};
    std::uint64_t attempt_{};
};
const char *missionPhaseName(MissionPhase phase) noexcept;
} // namespace osd
#endif // OSD_MISSION_STATE_H
