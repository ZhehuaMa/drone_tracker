#ifndef DRONE_TRACKER_OUTPUT_VALIDITY_H
#define DRONE_TRACKER_OUTPUT_VALIDITY_H

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace tracking {
using Nanoseconds = std::int64_t;
inline Nanoseconds monotonicNow() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::optional<Nanoseconds> addTime(Nanoseconds anchor, Nanoseconds duration) {
    if (anchor < 0 || duration < 0 || anchor > std::numeric_limits<Nanoseconds>::max() - duration) { return {}; }
    return anchor + duration;
}
inline std::optional<Nanoseconds> microsecondsToNs(std::int64_t value) {
    if (value < 0 || value > std::numeric_limits<Nanoseconds>::max() / 1000) { return {}; }
    return value * 1000;
}
enum class ClockMapping : std::uint8_t { Unavailable, CaptureMonotonicV1, StandaloneAdmissionV1 };
inline const char *clockMappingName(ClockMapping mapping) {
    switch (mapping) {
    case ClockMapping::CaptureMonotonicV1: return "capture-monotonic-v1";
    case ClockMapping::StandaloneAdmissionV1: return "standalone-admission-v1";
    default: return "unavailable";
    }
}
struct EvidenceClock {
    ClockMapping mapping{ClockMapping::Unavailable};
    Nanoseconds anchor_ns{};
    bool valid{};
};
// Map source age at admission. Future source time and arithmetic overflow are unusable.
inline EvidenceClock captureClock(std::int64_t capture_us, std::int64_t source_now_us, Nanoseconds decision_ns) {
    if (capture_us < 0 || source_now_us < capture_us || decision_ns < 0) { return {}; }
    const auto age{microsecondsToNs(source_now_us - capture_us)};
    if (!age || *age > decision_ns) { return {}; }
    return {ClockMapping::CaptureMonotonicV1, decision_ns - *age, true};
}
inline bool usableClock(const EvidenceClock &clock, Nanoseconds now) {
    return clock.valid && clock.mapping != ClockMapping::Unavailable && clock.anchor_ns >= 0 && now >= clock.anchor_ns;
}
inline bool fresh(const EvidenceClock &clock, Nanoseconds now, int lifetime_ms) {
    const auto deadline{addTime(clock.anchor_ns, static_cast<Nanoseconds>(lifetime_ms) * 1000000)};
    return usableClock(clock, now) && deadline && now < *deadline;
}
enum class Phase : std::uint8_t { Searching, PreparingProvisional, VerifyingProvisional, VerifiedTracking };
enum class LossReason : std::uint8_t { None, Expired, Disagreement, Ambiguous, NoTarget, TrackingFailure, Geometry, Clock };
struct Permission {
    std::uint64_t round{}, attempt{}, revision{}, seed_frame{}, seed_request{};
    std::uint64_t confirmation_frame{}, confirmation_source_us{};
    Phase phase{Phase::Searching};
    EvidenceClock evidence{};
    Nanoseconds deadline_ns{}, attempt_deadline_ns{}, verified_ns{};
    bool allowed{};
};
struct LossEvent {
    std::uint64_t round{}, attempt{}, revision{};
    LossReason reason{};
    Nanoseconds revoked_ns{};
    std::int64_t source_now_us{};
};
// Model state is worker-owned. This object only protects values and never calls I/O.
// Publication takes its own serialization lock BEFORE calling these short operations.
class OutputValidity {
public:
    void reset(std::uint64_t round) {
        std::lock_guard<std::mutex> lock{mutex_};
        state_ = {}; state_.round = round; state_.revision = ++revision_;
        pending_ = {}; pending_count_ = 0; next_attempt_ = 0; closed_ = false;
    }
    Permission snapshot() const { std::lock_guard<std::mutex> lock{mutex_}; return state_; }
    std::optional<Permission> begin(std::uint64_t round, std::uint64_t seed_frame,
        std::uint64_t seed_request, Nanoseconds now, int timeout_ms) {
        std::lock_guard<std::mutex> lock{mutex_};
        const auto deadline{addTime(now, static_cast<Nanoseconds>(timeout_ms) * 1000000)};
        if (closed_ || state_.round != round || state_.allowed || !deadline) { return {}; }
        state_.attempt = ++next_attempt_; state_.revision = ++revision_;
        state_.seed_frame = seed_frame; state_.seed_request = seed_request;
        state_.confirmation_frame = 0; state_.confirmation_source_us = 0; state_.evidence = {};
        state_.deadline_ns = 0; state_.verified_ns = 0;
        state_.phase = Phase::PreparingProvisional; state_.attempt_deadline_ns = *deadline;
        return state_;
    }
    bool prepared(const Permission &identity) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!same(identity) || state_.phase != Phase::PreparingProvisional) { return false; }
        state_.phase = Phase::VerifyingProvisional; return true;
    }
    bool confirm(const Permission &identity, std::uint64_t frame, std::uint64_t source_us,
        const EvidenceClock &clock, Nanoseconds now, int lifetime_ms, std::int64_t source_now_us = 0) {
        return confirm(identity, frame, source_us, clock, [now] { return now; }, lifetime_ms, source_now_us);
    }
    bool confirm(const Permission &identity, std::uint64_t frame, std::uint64_t source_us,
        const EvidenceClock &clock, const std::function<Nanoseconds()> &decision_clock,
        int lifetime_ms, std::int64_t source_now_us) {
        std::lock_guard<std::mutex> lock{mutex_};
        const auto now{decision_clock()};
        // A delayed supervisor must not let fresh evidence revive an expired instance.
        if (same(identity) && state_.allowed
            && (!usableClock(state_.evidence, now) || now >= state_.deadline_ns)) {
            revokeLocked(usableClock(state_.evidence, now) ? LossReason::Expired : LossReason::Clock,
                now, source_now_us);
            return false;
        }
        if (closed_ || !same(identity) || !fresh(clock, now, lifetime_ms)
            || (state_.phase != Phase::VerifyingProvisional && state_.phase != Phase::VerifiedTracking)
            || frame <= state_.seed_frame || pending_count_ == pending_.size()) { return false; }
        if (state_.phase != Phase::VerifiedTracking && now >= state_.attempt_deadline_ns) { return false; }
        state_.evidence = clock; state_.confirmation_frame = frame; state_.confirmation_source_us = source_us;
        state_.deadline_ns = *addTime(clock.anchor_ns, static_cast<Nanoseconds>(lifetime_ms) * 1000000);
        if (!state_.allowed) { state_.revision = ++revision_; state_.verified_ns = now; }
        state_.allowed = true; state_.phase = Phase::VerifiedTracking; return true;
    }
    bool revoke(LossReason reason, Nanoseconds now, std::int64_t source_now_us,
        const std::optional<Permission> &identity = {}) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (identity && !same(*identity)) { return false; }
        return revokeLocked(reason, now, source_now_us);
    }
    bool expire(Nanoseconds now, std::int64_t source_now_us, std::uint64_t expected_round = 0) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (expected_round && expected_round != state_.round) { return false; }
        if (state_.allowed && (!usableClock(state_.evidence, now) || now >= state_.deadline_ns)) {
            return revokeLocked(usableClock(state_.evidence, now) ? LossReason::Expired : LossReason::Clock,
                now, source_now_us);
        }
        return false;
    }
    bool admits(const Permission &identity, Nanoseconds now, std::int64_t source_now_us) {
        return admit(identity, [now] { return now; }, source_now_us).has_value();
    }
    std::optional<Nanoseconds> admit(const Permission &identity,
        const std::function<Nanoseconds()> &clock, std::int64_t source_now_us) {
        std::lock_guard<std::mutex> lock{mutex_};
        const auto now{clock()};
        if (state_.allowed && (!usableClock(state_.evidence, now) || now >= state_.deadline_ns)) {
            revokeLocked(LossReason::Expired, now, source_now_us);
        }
        if (!closed_ && pending_count_ == 0 && same(identity) && state_.allowed) { return now; }
        return {};
    }
    void abandon(const Permission &identity) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (same(identity) && !state_.allowed) { state_.phase = Phase::Searching; state_.revision = ++revision_; }
    }
    void close() {
        std::lock_guard<std::mutex> lock{mutex_};
        closed_ = true; state_.allowed = false; state_.phase = Phase::Searching; state_.revision = ++revision_;
    }
    std::optional<LossEvent> pending() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return pending_count_ ? std::optional<LossEvent>{pending_[0]} : std::nullopt;
    }
    void dispose(const LossEvent &event) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!pending_count_ || pending_[0].round != event.round || pending_[0].revision != event.revision) { return; }
        if (pending_count_ == 2) { pending_[0] = pending_[1]; }
        --pending_count_;
    }
private:
    bool same(const Permission &identity) const {
        return state_.round == identity.round && state_.attempt == identity.attempt
            && state_.revision == identity.revision;
    }
    bool revokeLocked(LossReason reason, Nanoseconds now, std::int64_t source_now_us) {
        if (!state_.allowed) { return false; }
        state_.allowed = false; state_.phase = Phase::Searching; state_.revision = ++revision_;
        pending_[pending_count_++] = {state_.round, state_.attempt, state_.revision, reason, now, source_now_us};
        return true;
    }
    mutable std::mutex mutex_{};
    Permission state_{};
    std::array<LossEvent, 2> pending_{};
    std::size_t pending_count_{};
    std::uint64_t revision_{}, next_attempt_{};
    bool closed_{};
};
inline void validateTimeouts(int confirmation, int candidate, int interval) {
    if (confirmation <= 0 || candidate <= 0 || interval <= 0 || interval > confirmation) {
        throw std::invalid_argument("tracker timeout values must be positive integers; expiry interval must not exceed confirmation timeout");
    }
}
} // namespace tracking
#endif // DRONE_TRACKER_OUTPUT_VALIDITY_H
