#include "output_validity.h"
#include "tracking_validators.h"
#include "model_test_support.h"
#include <future>
#include <iostream>

namespace {
using model_test::require;
constexpr std::int64_t ms{1000000};
tracking::Permission verify(tracking::OutputValidity &state, std::uint64_t seed, std::int64_t start,
    std::int64_t anchor, std::int64_t now) {
    const auto attempt{state.begin(1, seed, seed, start * ms, 1000)};
    require(attempt.has_value(), "seed refused"); require(state.prepared(*attempt), "preparation refused");
    require(state.confirm(*attempt, seed + 1, (anchor * 1000),
        {tracking::ClockMapping::StandaloneAdmissionV1, anchor * ms, true}, now * ms, 100), "verification refused");
    return state.snapshot();
}
void clocks() {
    const tracking::EvidenceClock f1{tracking::ClockMapping::StandaloneAdmissionV1, 5000 * ms, true};
    const tracking::EvidenceClock f2{tracking::ClockMapping::StandaloneAdmissionV1, 5140 * ms, true};
    require(5170 * ms - f1.anchor_ns == 170 * ms && 5170 * ms - f2.anchor_ns == 30 * ms,
        "immutable per-image replay age changed");
    require(!tracking::fresh(f1, 5170 * ms, 100) && tracking::fresh(f2, 5170 * ms, 100), "replay join age");
    require(tracking::fresh(f2, 5240 * ms - 1, 100) && !tracking::fresh(f2, 5240 * ms, 100), "exact expiry");
    const auto production{tracking::captureClock(1000000, 1080000, 5000 * ms)};
    require(5030 * ms - production.anchor_ns == 110 * ms && !tracking::fresh(production, 5030 * ms, 100),
        "pre-admission camera age was discarded");
    require(!tracking::captureClock(1080001, 1080000, 5000 * ms).valid, "future capture accepted");
    require(!tracking::fresh(f2, 5140 * ms - 1, 100), "future execution anchor accepted");
    require(!tracking::addTime(INT64_MAX, 1) && !tracking::microsecondsToNs(INT64_MAX), "time overflow");
    for (const auto values : {std::array<int,3>{0,1000,10}, {100,0,10}, {100,1000,0}, {100,1000,101}}) {
        bool rejected{}; try { tracking::validateTimeouts(values[0], values[1], values[2]); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "invalid timeout settings accepted");
    }
    tracking::validateTimeouts(100, 1000, 10);
    const auto appearance{tracking::appearanceValidation(0.0001f)};
    const auto motion{tracking::motionValidation({10000,10000,1000,1000}, {0,0,10,10})};
    require(appearance.allow && motion.allow && std::string{appearance.status} == "not_evaluated"
        && std::string{motion.status} == "not_evaluated", "deferred validators gained hidden rejection authority");
}
void ordering() {
    tracking::OutputValidity state{}; state.reset(1);
    const auto a{verify(state, 1, 1000, 1000, 1010)};
    require(state.admits(a, 1090 * ms, 1090000), "fresh A refused");
    require(state.revoke(tracking::LossReason::Disagreement, 1090 * ms, 1090000, a), "A not revoked");
    const auto loss{state.pending()}; require(loss && loss->attempt == a.attempt, "loss registration not atomic");
    // Supervisor is paused before publication; B can verify independently of the event publisher.
    const auto b{verify(state, 3, 1100, 1150, 1160)};
    require(!state.admits(a, 1161 * ms, 1161000), "old revision restored A");
    require(!state.admits(b, 1161 * ms, 1161000), "B overtook A's loss attempt");
    state.dispose(*loss);
    require(state.admits(b, 1180 * ms, 1180000), "B refused after A disposed");
    require((b.verified_ns - loss->revoked_ns) / ms == 70 && (1180 * ms - loss->revoked_ns) / ms == 90,
        "verification/admission recovery milestones conflated");
    state.dispose(*loss); require(!state.pending(), "duplicate supervisor retried A");
    require(state.expire(1250 * ms, 1250000), "B expiry delayed beyond exact deadline");
    require(!state.expire(1251 * ms, 1251000), "expiry duplicated loss event");
    require(!state.admits(b, 1251 * ms, 1251000), "expired output admitted");
    const auto revoked{state.snapshot()};
    require(!state.confirm(revoked, 100, 1, {tracking::ClockMapping::StandaloneAdmissionV1, 1250 * ms, true},
        1251 * ms, 100), "revoked instance renewed without a new acquisition attempt");
    state.close(); require(!state.begin(1, 5, 5, 1260 * ms, 1000), "OFF admitted old-round attempt");
    state.reset(2); require(!state.expire(2000 * ms, 2000000, 1), "old callback acted on new round");
    require(!state.pending() && !state.admits(b, 1300 * ms, 1300000), "round reset retained old permission");
}
void expiryWhileInferenceBlocked() {
    tracking::OutputValidity state{}; state.reset(1);
    const auto identity{verify(state, 1, 1000, 1000, 1010)};
    std::promise<void> release{}; auto gate{release.get_future()};
    auto model{std::async(std::launch::async, [&] { gate.wait(); return identity; })};
    require(state.expire(1100 * ms, 1100000), "expiry waited for model work or image consumption");
    release.set_value();
    require(!state.admits(model.get(), 1101 * ms, 1101000), "in-flight work restored expired permission");
}
void confirmationCannotSkipExpiry() {
    for (const auto elapsed : {1100, 1101}) {
        tracking::OutputValidity state{}; state.reset(1);
        const auto identity{verify(state, 1, 1000, 1000, 1010)};
        const tracking::EvidenceClock newer{tracking::ClockMapping::StandaloneAdmissionV1, 1090 * ms, true};
        const std::function<tracking::Nanoseconds()> decision{[elapsed] { return elapsed * ms; }};
        require(!state.confirm(identity, 3, 1090000, newer, decision, 100, elapsed * 1000),
            "new evidence renewed an expired instance before timer servicing");
        const auto loss{state.pending()};
        require(loss && loss->attempt == identity.attempt && loss->reason == tracking::LossReason::Expired
            && loss->revoked_ns == elapsed * ms && loss->source_now_us == elapsed * 1000,
            "confirmation expiry did not atomically register correctly timed loss");
        require(!state.snapshot().allowed && !state.admits(identity, elapsed * ms, elapsed * 1000),
            "expired revision regained permission");
    }
}
}
int main() {
    try { clocks(); ordering(); expiryWhileInferenceBlocked(); confirmationCannotSkipExpiry(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
