#include "osd/presenter.h"

#include <stdexcept>
#include <utility>

namespace osd {
OsdPresenter::OsdPresenter(std::unique_ptr<DisplayBackend> backend, std::chrono::milliseconds stale,
                           std::function<SteadyClock::time_point()> now)
    : backend_{std::move(backend)}, stale_{stale}, now_{std::move(now)} {
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
    std::optional<PresentationPacket> discarded{};
    std::lock_guard<std::mutex> lock{mutex_};
    if (!active_ && !stopping_ && failure_.empty()) {
        ++round_;
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
        ++version_;
        discarded.swap(pending_);
    }
    wake_.notify_all();
}
bool OsdPresenter::submit(PresentationPacket packet) {
    std::optional<PresentationPacket> incoming{std::move(packet)};
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!active_ || stopping_ || !failure_.empty() || incoming->round != round_
            || !incoming->frame || incoming->sequence <= last_sequence_) {
            ++counters_.rejected;
            return false;
        }
        last_sequence_ = incoming->sequence;
        last_received_ = now_();
        ++counters_.submitted;
        if (pending_) {
            ++counters_.replaced;
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
void OsdPresenter::render() noexcept {
    try {
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
            std::uint64_t version{};
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
                next.telemetry = telemetry_;
                next.status = status_;
                if (active_ && status_ == DisplayStatus::Live && now_() - last_received_ > stale_) {
                    next.status = DisplayStatus::Stale;
                }
            }
            next.new_video = packet.has_value();
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
                continue;
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
