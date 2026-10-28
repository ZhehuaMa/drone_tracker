#include "osd/presenter.h"
#include "osd/layout.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace osd {
namespace {
const char *statusName(DisplayStatus status) {
    switch (status) {
    case DisplayStatus::WaitingInput: return "WaitingInput";
    case DisplayStatus::WaitingFirst: return "WaitingFirst";
    case DisplayStatus::Live: return "Live";
    case DisplayStatus::Stale: return "Stale";
    case DisplayStatus::Stopped: return "Stopped";
    case DisplayStatus::Eof: return "Eof";
    case DisplayStatus::InputFailure: return "InputFailure";
    }
    return "Unknown";
}
} // namespace
OsdPresenter::OsdPresenter(std::unique_ptr<DisplayBackend> backend, std::chrono::milliseconds stale,
                           std::function<SteadyClock::time_point()> now,
                           std::function<void(const TimingWindow &)> timing_sink, std::function<void()> before_work,
                           std::function<void(const std::string &)> diagnostic_sink,
                           std::chrono::milliseconds flight_timeout)
    : timing_sink_{std::move(timing_sink)}, before_work_{std::move(before_work)},
      diagnostic_sink_{std::move(diagnostic_sink)},
      backend_{std::move(backend)}, stale_{stale}, now_{std::move(now)}, mission_{flight_timeout} {
    if (!backend_ || !now_ || stale_.count() <= 0) {
        throw std::invalid_argument("OSD requires backend and positive frame timeout");
    }
}
void OsdPresenter::emitDiagnostic(const std::string &message) const noexcept {
    if (!diagnostic_sink_) { return; }
    try { diagnostic_sink_(message); } catch (...) { /* 诊断失败不影响显示。 */ }
}
OsdPresenter::~OsdPresenter() { shutdown(); }
void OsdPresenter::start() {
    std::unique_lock<std::mutex> lock{mutex_};
    if (started_) {
        throw std::logic_error("OSD presenter already started");
    }
    started_ = true;
    if (timing_sink_) { timing_window_start_ = now_(); }
    thread_ = std::thread{&OsdPresenter::render, this};
    wake_.wait(lock, [this] { return initialized_ || finished_; });
    if (!failure_.empty()) {
        const std::string error{failure_};
        lock.unlock();
        shutdown();
        throw std::runtime_error(error);
    }
}
void OsdPresenter::shutdown() noexcept {
    std::optional<PresentationPacket> discarded{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
        active_ = false;
        ++version_;
        discarded.swap(pending_);
    }
    wake_.notify_all();
    backend_->interrupt();
    if (thread_.joinable()) {
        thread_.join();
    }
}
void OsdPresenter::beginStream(std::uint64_t stream) {
    std::optional<PresentationPacket> discarded{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (stopping_ || !failure_.empty()) { throw std::runtime_error("Presenter unavailable"); }
        continuous_ = true;
        stream_ = stream;
        round_ = 0;
        last_sequence_ = 0;
        tracking_phase_ = TrackingPhase::Idle;
        active_ = true;
        status_ = DisplayStatus::WaitingInput;
        last_display_.reset();
        ++interval_generation_;
        ++version_;
        discarded.swap(pending_);
    }
    wake_.notify_all();
}
void OsdPresenter::endStream(std::uint64_t stream, DisplayStatus reason) {
    std::optional<PresentationPacket> discarded{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!continuous_ || stream != stream_) { return; }
        active_ = false;
        status_ = reason;
        ++version_;
        ++interval_generation_;
        last_display_.reset();
        discarded.swap(pending_);
    }
    wake_.notify_all();
}
std::uint64_t OsdPresenter::controlTracking(std::uint64_t round, TrackingPhase phase, std::uint64_t cutoff) {
    std::lock_guard<std::mutex> round_lock{round_mutex_};
    bool new_round{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        new_round = round > round_;
    }
    // Keep the existing bounded current/in-flight timing windows without breaking the stream interval chain.
    if (new_round) { reportTiming(); }
    std::optional<PresentationPacket> discarded{};
    std::uint64_t version{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!continuous_ || !active_) { return version_; }
        if (round < round_) { return version_; }
        round_ = round;
        tracking_phase_ = phase;
        tracking_control_received_ = now_();
        cutoff_ = cutoff;
        ++version_;
        version = version_;
        discarded.swap(pending_);
    }
    wake_.notify_all();
    emitDiagnostic("OSD control event=tracking round=" + std::to_string(round)
        + " phase=" + std::to_string(static_cast<int>(phase)) + " version=" + std::to_string(version));
    return version;
}
std::uint64_t OsdPresenter::beginRound() {
    std::lock_guard<std::mutex> round_lock{round_mutex_};
    reportTiming();
    std::optional<PresentationPacket> discarded{};
    std::uint64_t round{};
    DisplayStatus status{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!active_ && !stopping_ && failure_.empty()) {
            ++round_;
            trigger_.clear();
            last_display_.reset();
            ++interval_generation_;
            ++version_;
            last_sequence_ = 0;
            active_ = true;
            status_ = DisplayStatus::WaitingFirst;
            discarded.swap(pending_);
            wake_.notify_all();
        }
        round = round_;
        status = status_;
    }
    emitDiagnostic("OSD control event=begin_round round=" + std::to_string(round)
        + " status=" + statusName(status));
    return round;
}
void OsdPresenter::showTrigger(std::uint64_t round, bool on) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (round != round_ || stopping_ || !failure_.empty()) { return; }
    trigger_ = on ? "TRIG ON" : "TRIG OFF";
    trigger_until_ = now_() + std::chrono::seconds{2};
    if (!continuous_) { ++version_; }
    wake_.notify_all();
}
std::vector<std::string> OsdPresenter::recordMissionUpdate(const MissionUpdate &update) {
    std::vector<std::string> diagnostics{};
    for (std::size_t i{0}; i < update.count; ++i) {
        const auto &transition{update.transitions[i]};
        ++overlay_revision_;
        std::ostringstream message{};
        message << "OSD mission phase=" << missionPhaseName(transition.phase)
                << " reason=" << transition.reason << " overlay_revision=" << overlay_revision_
                << " attempt=" << transition.attempt << " monotonic_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(transition.time.time_since_epoch()).count();
        diagnostics.push_back(message.str());
    }
    if (update.count) { wake_.notify_all(); }
    return diagnostics;
}
void OsdPresenter::missionReceived(bool active, SteadyClock::time_point received) {
    std::vector<std::string> diagnostics{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (stopping_ || !failure_.empty()) { return; }
        diagnostics = recordMissionUpdate(mission_.mission(active, received));
    }
    for (const auto &message : diagnostics) { emitDiagnostic(message); }
}
void OsdPresenter::flightReceived(Measurement<FlightState> flight) {
    if (!flight.value) { return; }
    std::vector<std::string> diagnostics{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (stopping_ || !failure_.empty()) { return; }
        diagnostics = recordMissionUpdate(mission_.flight(flight.value->connected, flight.value->armed,
            flight.value->guided, flight.received, now_()));
    }
    for (const auto &message : diagnostics) { emitDiagnostic(message); }
}
void OsdPresenter::cancelMission(bool dismiss) {
    std::vector<std::string> diagnostics{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (stopping_ || !failure_.empty()) { return; }
        diagnostics = recordMissionUpdate(mission_.cancel(dismiss, now_()));
    }
    for (const auto &message : diagnostics) { emitDiagnostic(message); }
}
void OsdPresenter::endRound(std::uint64_t round, DisplayStatus reason) {
    std::lock_guard<std::mutex> round_lock{round_mutex_};
    if (reason != DisplayStatus::Stopped && reason != DisplayStatus::Eof && reason != DisplayStatus::InputFailure) {
        throw std::invalid_argument("OSD endRound requires a stop reason");
    }
    std::optional<PresentationPacket> discarded{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (round != round_ || (!active_ && status_ != DisplayStatus::WaitingInput)) {
            return;
        }
        active_ = false;
        status_ = reason;
        last_display_.reset();
        ++interval_generation_;
        ++version_;
        discarded.swap(pending_);
    }
    wake_.notify_all();
    emitDiagnostic("OSD control event=end_round round=" + std::to_string(round)
        + " status=" + statusName(reason));
    reportTiming();
}
bool OsdPresenter::submit(PresentationPacket packet) {
    std::optional<PresentationPacket> incoming{std::move(packet)};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!active_ || stopping_ || !failure_.empty() || incoming->round != round_
            || !incoming->frame || incoming->sequence <= last_sequence_
            || (continuous_ && (incoming->stream != stream_ || incoming->control_version != version_
                || (tracking_phase_ == TrackingPhase::Running
                    ? (incoming->kind != PacketKind::Tracking || !incoming->tracker_state
                        || incoming->source_sequence <= cutoff_)
                    : (incoming->kind != PacketKind::Preview || incoming->tracker_state
                        || !incoming->boxes.empty()))))) {
            ++counters_.rejected;
            if (timing_sink_) { ++timingWindow(round_).rejected; }
            return false;
        }
        last_sequence_ = incoming->sequence;
        last_received_ = now_();
        if (timing_sink_) {
            incoming->accepted = last_received_;
            auto &window{timingWindow(round_)};
            ++window.submitted;
            if (incoming->kind == PacketKind::Preview) { ++window.preview_submitted; }
            else { ++window.tracking_submitted; }
        }
        ++counters_.submitted;
        if (pending_) {
            ++counters_.replaced;
            if (timing_sink_) { ++timingWindow(round_).replaced; }
        }
        incoming.swap(pending_);
    }
    // 被替换包在提交锁外释放，避免帧析构阻塞控制事件。
    wake_.notify_all();
    return true;
}
void OsdPresenter::updateTelemetry(TelemetrySnapshot snapshot) {
    std::lock_guard<std::mutex> lock{mutex_};
    std::swap(telemetry_, snapshot);
}
PresentationCounters OsdPresenter::counters() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return counters_;
}
std::string OsdPresenter::failure() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return failure_;
}
TimingWindow &OsdPresenter::timingWindow(std::uint64_t round) {
    for (auto &window : timing_windows_) {
        if (window && window->round == round) { return *window; }
    }
    for (auto &window : timing_windows_) {
        if (!window) {
            window.emplace();
            window->round = round;
            window->stream = stream_;
            window->id = ++timing_window_id_;
            window->start = timing_window_start_;
            return *window;
        }
    }
    throw std::logic_error("OSD timing round windows exhausted");
}
void OsdPresenter::reportTiming() {
    if (!timing_sink_) { return; }
    std::array<std::optional<TimingWindow>, 3> snapshots{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        timingWindow(round_); // 即使窗口为空也明确输出。
        const auto end{now_()};
        snapshots.swap(timing_windows_);
        for (auto &window : snapshots) {
            if (window) { window->end = end; }
        }
        timing_window_start_ = end;
    }
    for (const auto &window : snapshots) {
        if (window) { timing_sink_(*window); }
    }
}
void OsdPresenter::render() noexcept {
    try {
        if (before_work_) { before_work_(); }
        backend_->initialize();
        {
            std::lock_guard<std::mutex> lock{mutex_};
            initialized_ = true;
        }
        wake_.notify_all();
        PresentationState last{};
        std::uint64_t rendered_version{~std::uint64_t{0}}, rendered_overlay{~std::uint64_t{0}};
        while (true) {
            std::optional<PresentationPacket> packet{};
            PresentationState next{last};
            std::uint64_t version{}, overlay_revision{}, timing_round{}, generation{};
            std::vector<std::string> mission_diagnostics{};
            SteadyClock::time_point render_start{}, accepted{}, control_received{};
            TimingSample sample{};
            bool first_video{}, continuous{};
            {
                std::unique_lock<std::mutex> lock{mutex_};
                wake_.wait_for(lock, std::chrono::milliseconds{100}, [&] {
                    return stopping_ || pending_ || rendered_version != version_ || rendered_overlay != overlay_revision_;
                });
                if (stopping_) {
                    break;
                }
                mission_diagnostics = recordMissionUpdate(mission_.expire(now_()));
                overlay_revision = overlay_revision_;
                version = version_;
                continuous = continuous_;
                packet.swap(pending_);
                if (timing_sink_) {
                    render_start = now_();
                    timing_round = packet ? packet->round : round_;
                    if (packet) {
                        accepted = packet->accepted;
                        sample.add(TimingStage::PendingWait, render_start - accepted);
                        first_video = first_video_round_ != packet->round;
                        first_video_round_ = packet->round;
                    }
                }
                next.telemetry = telemetry_;
                next.round = round_;
                next.stream = stream_;
                next.control_version = version_;
                control_received = tracking_control_received_;
                next.tracking_phase = continuous_ ? tracking_phase_ : TrackingPhase::Running;
                if (continuous_ && tracking_phase_ != TrackingPhase::Running) {
                    next.boxes.clear();
                    next.has_tracking_result = false;
                }
                next.trigger = !trigger_.empty() && now_() < trigger_until_ ? trigger_ : "";
                next.mission_active = mission_.active();
                next.overlay_revision = overlay_revision;
                next.mission_attempt = mission_.attempt();
                next.status = status_;
                if (active_ && status_ == DisplayStatus::Live && now_() - last_received_ > stale_) {
                    next.status = DisplayStatus::Stale;
                    last_display_.reset();
                    ++interval_generation_;
                }
                generation = interval_generation_;
            }
            for (const auto &message : mission_diagnostics) { emitDiagnostic(message); }
            next.new_video = packet.has_value();
            const bool new_video{next.new_video};
            const auto finish_timing{[&](TimingResult result) {
                backend_->setTimingSample(nullptr);
                if (!timing_sink_) { return; }
                const auto end{now_()};
                sample.add(TimingStage::RenderTotal, end - render_start);
                std::uint64_t children{};
                for (std::size_t i{0}; i < static_cast<std::size_t>(TimingStage::RenderTotal); ++i) {
                    children += sample.stages[i].value_or(0);
                }
                children += sample.stages[static_cast<std::size_t>(TimingStage::SourcePrepare)].value_or(0);
                children += sample.stages[static_cast<std::size_t>(TimingStage::OverlayUpload)].value_or(0);
                const auto total{sample.stages[static_cast<std::size_t>(TimingStage::RenderTotal)].value()};
                sample.stages[static_cast<std::size_t>(TimingStage::Other)] = total > children ? total - children : 0;
                std::lock_guard<std::mutex> lock{mutex_};
                auto &window{timingWindow(timing_round)};
                if (result == TimingResult::Success) {
                    ++window.commits;
                    if (new_video) {
                        ++window.displayed;
                        if (next.kind == PacketKind::Preview) { ++window.preview_displayed; }
                        else { ++window.tracking_displayed; }
                        sample.add(TimingStage::SubmitToDisplay, end - accepted);
                        // Tracking controls do not break a completed frame's continuous stream interval.
                        const bool same_segment{continuous ? next.stream == stream_ : version == version_};
                        if (generation == interval_generation_ && same_segment && active_) {
                            if (last_display_) { sample.add(TimingStage::NewFrameInterval, end - *last_display_); }
                            last_display_ = end;
                        }
                    }
                }
                window.add(sample, new_video, result, first_video);
            }};
            backend_->setTimingSample(timing_sink_ ? &sample : nullptr);
            try {
                if (packet) {
                    backend_->prepareVideo(*packet->frame);
                    next.round = packet->round;
                    next.sequence = packet->sequence;
                    next.frame_time_us = packet->frame_time_us;
                    next.tracker_state = packet->tracker_state.value_or(0);
                    next.has_tracking_result = packet->tracker_state.has_value();
                    next.kind = packet->kind;
                    next.source_sequence = packet->source_sequence;
                    next.stream = packet->stream;
                    next.boxes = std::move(packet->boxes);
                    next.has_video = true;
                    next.status = DisplayStatus::Live;
                    // 从这里开始显示只使用后端存储，冻结不占用采集帧。
                    packet.reset();
                } else if (next.status != DisplayStatus::Live) {
                    next.boxes.clear();
                }
                const auto authorize{[this, version, &overlay_revision] {
                    std::lock_guard<std::mutex> lock{mutex_};
                    return !stopping_ && version == version_ && overlay_revision == overlay_revision_;
                }};
                bool displayed{};
                while (!(displayed = backend_->present(next, authorize))) {
                    std::lock_guard<std::mutex> lock{mutex_};
                    if (stopping_ || version != version_ || overlay_revision == overlay_revision_) { break; }
                    // Only the overlay became obsolete. Keep the already copied, paired video
                    // and redraw its overlay without touching packet admission or source ownership.
                    overlay_revision = overlay_revision_;
                    next.mission_active = mission_.active();
                    next.overlay_revision = overlay_revision;
                    next.mission_attempt = mission_.attempt();
                    next.telemetry = telemetry_;
                }
                if (!displayed) {
                    backend_->discardPrepared();
                    finish_timing(TimingResult::Rejected);
                    continue;
                }
                finish_timing(TimingResult::Success);
                if (diagnostic_sink_) {
                    try {
                        const std::string hud{statusBarText(next)};
                        const std::string target{targetLabelText(next)};
                        if (next.status != last.status || next.has_video != last.has_video
                            || hud != statusBarText(last) || target != targetLabelText(last)
                            || next.tracking_phase != last.tracking_phase
                            || next.boxes.empty() != last.boxes.empty()
                            || (continuous && next.control_version != last.control_version)) {
                            std::ostringstream message{};
                            message << "OSD display round=" << next.round << " sequence=" << next.sequence
                                    << " status=" << statusName(next.status)
                                    << " hud=\"" << hud << "\" target_label=\"" << target << "\""
                                    << " boxes=" << next.boxes.size() << " has_video=" << next.has_video
                                    << " new_video=" << next.new_video << " mission=" << next.mission_active
                                    << " stream=" << next.stream << " source_sequence=" << next.source_sequence
                                    << " kind=" << (next.kind == PacketKind::Preview ? "preview" : "tracking")
                                    << " overlay_revision=" << next.overlay_revision
                                    << " attempt=" << next.mission_attempt << " monotonic_us="
                                    << std::chrono::duration_cast<std::chrono::microseconds>(now_().time_since_epoch()).count()
                                    << " control_version=" << next.control_version << " control_to_display_us="
                                    << std::chrono::duration_cast<std::chrono::microseconds>(now_() - control_received).count();
                            emitDiagnostic(message.str());
                        }
                    } catch (...) {
                        // 日志构造失败不能中断已提交的 HDMI 画面。
                    }
                }
                last = std::move(next);
                {
                    std::lock_guard<std::mutex> lock{mutex_};
                    ++counters_.refreshes;
                    if (last.new_video) {
                        ++counters_.displayed;
                        if (version == version_) {
                            status_ = DisplayStatus::Live;
                        }
                    }
                    rendered_version = version;
                    rendered_overlay = overlay_revision;
                }
            } catch (...) {
                finish_timing(TimingResult::Failure);
                throw;
            }
        }
    } catch (const std::exception &error) {
        std::lock_guard<std::mutex> lock{mutex_};
        failure_ = error.what();
        active_ = false;
    } catch (...) {
        std::lock_guard<std::mutex> lock{mutex_};
        failure_ = "OSD unknown backend failure";
        active_ = false;
    }
    backend_->shutdown();
    std::optional<PresentationPacket> discarded{};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        discarded.swap(pending_);
        finished_ = true;
    }
    wake_.notify_all();
}
} // namespace osd
