#include "osd/presenter.h"

#include <stdexcept>
#include <utility>

namespace osd {
OsdPresenter::OsdPresenter(std::unique_ptr<DisplayBackend> backend, std::chrono::milliseconds stale,
                           std::function<SteadyClock::time_point()> now,
                           std::function<void(const TimingWindow &)> timing_sink, std::function<void()> before_work)
    : timing_sink_{std::move(timing_sink)}, before_work_{std::move(before_work)},
      backend_{std::move(backend)}, stale_{stale}, now_{std::move(now)} {
    if (!backend_ || !now_ || stale_.count() <= 0) {
        throw std::invalid_argument("OSD requires backend and positive frame timeout");
    }
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
std::uint64_t OsdPresenter::beginRound() {
    std::lock_guard<std::mutex> round_lock{round_mutex_};
    reportTiming();
    std::optional<PresentationPacket> discarded{};
    std::lock_guard<std::mutex> lock{mutex_};
    if (!active_ && !stopping_ && failure_.empty()) {
        ++round_;
        last_display_.reset();
        ++interval_generation_;
        ++version_;
        last_sequence_ = 0;
        active_ = true;
        status_ = DisplayStatus::WaitingFirst;
        discarded.swap(pending_);
        wake_.notify_all();
    }
    return round_;
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
    reportTiming();
}
bool OsdPresenter::submit(PresentationPacket packet) {
    std::optional<PresentationPacket> incoming{std::move(packet)};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!active_ || stopping_ || !failure_.empty() || incoming->round != round_
            || !incoming->frame || incoming->sequence <= last_sequence_) {
            ++counters_.rejected;
            if (timing_sink_) { ++timingWindow(round_).rejected; }
            return false;
        }
        last_sequence_ = incoming->sequence;
        last_received_ = now_();
        if (timing_sink_) {
            incoming->accepted = last_received_;
            ++timingWindow(round_).submitted;
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
        std::uint64_t rendered_version{~std::uint64_t{0}};
        while (true) {
            std::optional<PresentationPacket> packet{};
            PresentationState next{last};
            std::uint64_t version{}, timing_round{}, generation{};
            SteadyClock::time_point render_start{}, accepted{};
            TimingSample sample{};
            bool first_video{};
            {
                std::unique_lock<std::mutex> lock{mutex_};
                wake_.wait_for(lock, std::chrono::milliseconds{100}, [&] {
                    return stopping_ || pending_ || rendered_version != version_;
                });
                if (stopping_) {
                    break;
                }
                version = version_;
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
                next.status = status_;
                if (active_ && status_ == DisplayStatus::Live && now_() - last_received_ > stale_) {
                    next.status = DisplayStatus::Stale;
                    last_display_.reset();
                    ++interval_generation_;
                }
                generation = interval_generation_;
            }
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
                        sample.add(TimingStage::SubmitToDisplay, end - accepted);
                        if (generation == interval_generation_ && version == version_ && active_) {
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
                    next.tracker_state = packet->tracker_state;
                    next.boxes = std::move(packet->boxes);
                    next.has_video = true;
                    next.status = DisplayStatus::Live;
                    // 从这里开始显示只使用后端存储，冻结不占用采集帧。
                    packet.reset();
                } else if (next.status != DisplayStatus::Live) {
                    next.boxes.clear();
                }
                const bool displayed{backend_->present(next, [this, version] {
                    std::lock_guard<std::mutex> lock{mutex_};
                    return !stopping_ && version == version_;
                })};
                if (!displayed) {
                    backend_->discardPrepared();
                    finish_timing(TimingResult::Rejected);
                    continue;
                }
                finish_timing(TimingResult::Success);
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
