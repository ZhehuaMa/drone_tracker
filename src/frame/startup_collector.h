#ifndef FRAME_STARTUP_COLLECTOR_H
#define FRAME_STARTUP_COLLECTOR_H
#include <mutex>
#include "recording_receipt.h"
#include "diagnostics/timing.h"
/** @brief Bounded round evidence. Control locks precede this mutex; hooks take only this mutex. */
class StartupCollector final {
public:
    explicit StartupCollector(RecordingStartupEvidence evidence) : evidence_{evidence} {}
    void observe(bool initialization, const AlgorithmSource &source) noexcept {
        std::lock_guard<std::mutex> lock{mutex_};
        if (frozen_) { return; }
        auto &known{initialization ? evidence_.initialization_known : evidence_.yolo_known};
        if (!known) {
            (initialization ? evidence_.first_initialization : evidence_.first_yolo) = source;
            known = true;
            if (initialization) { evidence_.initialization_mono_us = diagnostics::monotonicNs() / 1000; }
        }
    }
    void eligible() noexcept {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!frozen_ && !evidence_.first_eligible_mono_us) {
            evidence_.first_eligible_mono_us = diagnostics::monotonicNs() / 1000;
        }
    }
    RecordingStartupEvidence snapshot() const noexcept {
        std::lock_guard<std::mutex> lock{mutex_};
        return evidence_;
    }
    RecordingStartupEvidence freeze(bool cancelled, bool fallback, std::uint64_t withheld,
                                    std::uint64_t rejected, std::uint64_t replaced) noexcept {
        std::lock_guard<std::mutex> lock{mutex_};
        frozen_ = true;
        evidence_.cancelled = cancelled; evidence_.fallback = fallback;
        evidence_.dispatch_withheld = withheld; evidence_.dispatch_rejected = rejected;
        evidence_.pending_replaced = replaced;
        return evidence_;
    }
private:
    mutable std::mutex mutex_{};
    RecordingStartupEvidence evidence_{};
    bool frozen_{};
};
#endif // FRAME_STARTUP_COLLECTOR_H
