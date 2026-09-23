#include "ar0234_camera_frame_source.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "mv_camera/readerwriterqueue.h"

namespace rk3576 {
std::int64_t Ar0234TriggerPlan::due() const {
    return origin_us + static_cast<std::int64_t>(std::llround(index * period_us));
}
std::uint64_t Ar0234TriggerPlan::skipLate(std::int64_t now) {
    if (now - due() < period_us) { return 0; }
    const auto next{static_cast<std::uint64_t>(std::floor((now - origin_us) / period_us)) + 1};
    const auto skipped{next - index};
    index = next;
    return skipped;
}
struct Ar0234CameraFrameSource::Impl {
    using Queue = moodycamel::BlockingReaderWriterQueue<FrameRef>;
    FrameSourceConfig config{};
    Ar0234CameraDependencies dependencies{};
    std::unique_ptr<Rk3576CameraCaptureBase> device{};
    std::unique_ptr<Queue> queue{std::make_unique<Queue>(30)};
    std::thread worker{};
    std::thread trigger_worker{};
    std::mutex wait_mutex{};
    std::condition_variable changed{};
    bool capture_ready{};
    std::atomic<bool> triggers_stopped{false};
    std::int64_t stream_started_us{};
    std::atomic<bool> stopped{false};
    mutable std::mutex mutex{};
    Ar0234CameraDiagnostic record{};

    Impl(FrameSourceConfig input, Ar0234CameraDependencies injected)
        : config{std::move(input)}, dependencies{std::move(injected)} {
        if (!dependencies.system) { dependencies.system = std::make_shared<CameraSystem>(); }
        if (!dependencies.launch) {
            dependencies.launch = [](std::function<void()> work) { return std::thread{std::move(work)}; };
        }
        if (!dependencies.trigger_now) {
            dependencies.trigger_now = [this] { return dependencies.system->clockUs(CLOCK_MONOTONIC); };
        }
        if (!dependencies.make_device) {
            // 默认工厂构造 AR0234 设备；SC233HGS 帧源封装会注入构造 Sc233hgsCameraDevice 的工厂。
            dependencies.make_device = [](const FrameSourceConfig &device_config,
                                          std::shared_ptr<CameraSystem> device_system) {
                return std::make_unique<Ar0234CameraDevice>(device_config, std::move(device_system));
            };
        }
        if (!dependencies.trigger_wait) {
            dependencies.trigger_wait = [this](std::condition_variable &cv, std::unique_lock<std::mutex> &lock,
                                               std::int64_t due, const std::function<bool()> &stop) {
                cv.wait_for(lock, std::chrono::microseconds{std::max<std::int64_t>(0,
                    due - dependencies.trigger_now())}, stop);
            };
        }
        device = dependencies.make_device(config, dependencies.system);
        device->prepare();
        record.format = device->format();
        const auto error{device->finish()};
        if (!error.empty()) { throw std::runtime_error(error); }
        record.source.state = FrameSourceState::Ready;
        record.source.path = config.path;
    }
    void fail(const std::string &reason) {
        std::lock_guard<std::mutex> lock{mutex};
        record.source.state = FrameSourceState::Failed;
        if (!record.source.reason.empty()) { record.source.reason += "; "; }
        record.source.reason += reason;
    }
    void requestStop() {
        {
            std::lock_guard<std::mutex> lock{wait_mutex};
            stopped.store(true);
            triggers_stopped.store(true);
        }
        changed.notify_all();
        const auto error{device->wake()};
        if (!error.empty()) { fail(error); }
    }
    void finish() {
        const auto before{std::chrono::steady_clock::now()};
        const auto error{device->finish()};
        const auto elapsed{std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - before).count()};
        if (!error.empty()) { fail(error); }
        std::lock_guard<std::mutex> lock{mutex};
        record.cleanup_us = elapsed;
        record.source.camera = device->captureStatus();
        record.unrestored_controls = device->unrestoredControls();
        record.source.controls_restored = record.unrestored_controls.empty();
        if (record.source.state != FrameSourceState::Failed) {
            record.source.state = FrameSourceState::Stopped;
            record.source.reason = "Stopped by caller";
        }
    }
    void triggerRun() noexcept {
        try {
            Ar0234TriggerPlan plan{dependencies.trigger_now(), 1000000.0 / config.camera_trigger_rate_hz, 0};
            std::uint64_t sent{};
            while (!stopped.load() && !triggers_stopped.load() && sent < dependencies.trigger_limit) {
                const auto now{dependencies.trigger_now()};
                const auto skipped{plan.skipLate(now)};
                if (skipped) {
                    std::lock_guard<std::mutex> lock{mutex};
                    record.source.trigger_skipped += skipped;
                }
                if (now < plan.due()) {
                    std::unique_lock<std::mutex> lock{wait_mutex};
                    dependencies.trigger_wait(changed, lock, plan.due(), [this] {
                        return stopped.load() || triggers_stopped.load();
                    });
                    continue;
                }
                if (stopped.load() || triggers_stopped.load()) { break; }
                Ar0234TriggerEvent event{};
                event.before_us = dependencies.trigger_now();
                try { device->trigger(); event.success = true; }
                catch (const std::exception &error) { event.error = error.what(); }
                event.after_us = dependencies.trigger_now();
                {
                    std::lock_guard<std::mutex> lock{mutex};
                    auto &state{record.source};
                    if (event.success) {
                        if (state.trigger_success == 0) { state.first_trigger_us = event.before_us; }
                        ++state.trigger_success;
                        state.last_trigger_us = event.before_us;
                        if (state.trigger_success > 1 && state.last_trigger_us > state.first_trigger_us) {
                            state.actual_trigger_hz = (state.trigger_success - 1) * 1000000.0
                                / (state.last_trigger_us - state.first_trigger_us);
                        }
                    } else { ++state.trigger_errors; }
                }
                if (dependencies.observe_trigger) { dependencies.observe_trigger(event); }
                if (!event.success) { throw std::runtime_error(event.error); }
                if (event.after_us < event.before_us) { throw std::runtime_error("Trigger clock moved backwards"); }
                ++sent;
                ++plan.index;
            }
        } catch (const std::exception &error) { fail(error.what()); requestStop(); }
        catch (...) { fail("Unknown trigger worker failure"); requestStop(); }
    }
    void run() noexcept {
        try {
            {
                std::lock_guard<std::mutex> lock{wait_mutex};
                capture_ready = true;
            }
            changed.notify_all();
            auto last_dequeue{stream_started_us};
            while (!stopped.load()) {
                auto sample{device->next(100, dependencies.compare_pixels)};
                if (stopped.load()) {
                    if (sample.result == CameraSampleResult::FrameReady) { device->discardReadyFrame(); }
                    break;
                }
                if (sample.result == CameraSampleResult::NoSample) {
                    const auto now{dependencies.system->clockUs(CLOCK_MONOTONIC)};
                    if (now < last_dequeue || now - last_dequeue >= 2000000) {
                        throw std::runtime_error("Camera capture timeout: no successful dequeue for 2 seconds");
                    }
                    continue;
                }
                last_dequeue = sample.dequeue_us;
                if (sample.result == CameraSampleResult::WarmupDiscarded) {
                    std::lock_guard<std::mutex> lock{mutex};
                    record.source.camera = device->captureStatus();
                    record.source.sequence_gaps = device->sequenceGaps();
                    record.driver_dropped = device->sequenceGaps();
                    record.source.time_source = sample.strategy;
                    continue;
                }
                const bool enqueued{queue->try_enqueue(sample.frame)};
                Ar0234CameraDiagnostic snapshot{};
                {
                    std::lock_guard<std::mutex> lock{mutex};
                    ++record.source.read;
                    record.source.frame_index = sample.sequence;
                    record.driver_dropped = device->sequenceGaps();
                    record.source.sequence_gaps = device->sequenceGaps();
                    record.source.camera = device->captureStatus();
                    record.source.time_source = sample.strategy;
                    record.source.clock_offset_delta_us = sample.clock_offset_delta_us;
                    if (enqueued) { ++record.source.enqueued; } else { ++record.source.dropped; }
                    snapshot = record;
                }
                if (dependencies.observe) { dependencies.observe(sample, snapshot); }
            }
        } catch (const std::exception &error) {
            fail(error.what());
        } catch (...) {
            fail("Unknown camera worker failure");
        }
        {
            std::lock_guard<std::mutex> lock{mutex};
            record.source.camera = device->captureStatus();
            record.source.sequence_gaps = device->sequenceGaps();
            record.driver_dropped = device->sequenceGaps();
        }
        requestStop();
    }
};
Ar0234CameraFrameSource::Ar0234CameraFrameSource(FrameSourceConfig config, Ar0234CameraDependencies dependencies)
    : impl_{std::make_unique<Impl>(std::move(config), std::move(dependencies))} {}
Ar0234CameraFrameSource::~Ar0234CameraFrameSource() { stop(); }
bool Ar0234CameraFrameSource::start() {
    if (isRunning()) { return false; }
    if (impl_->worker.joinable() || impl_->trigger_worker.joinable()) { stop(); }
    if (!impl_->device->unrestoredControls().empty()) {
        impl_->finish();
        if (!impl_->device->unrestoredControls().empty()) { return false; }
    }
    impl_->stopped.store(false);
    impl_->triggers_stopped.store(false);
    impl_->capture_ready = false;
    try {
        {
            std::lock_guard<std::mutex> lock{impl_->mutex};
            impl_->record = {};
            impl_->record.source.path = impl_->config.path;
            impl_->record.source.software_trigger = impl_->config.trigger_mode;
            impl_->record.source.requested_trigger_hz = impl_->config.camera_trigger_rate_hz;
        }
        impl_->queue = std::make_unique<Impl::Queue>(30);
        impl_->device = impl_->dependencies.make_device(impl_->config, impl_->dependencies.system);
        impl_->device->prepare();
        impl_->device->start();
        impl_->stream_started_us = impl_->dependencies.system->clockUs(CLOCK_MONOTONIC);
        {
            std::lock_guard<std::mutex> lock{impl_->mutex};
            impl_->record.format = impl_->device->format();
            impl_->record.clock_pair = impl_->device->clockPair();
            impl_->record.source.state = FrameSourceState::Running;
        }
        impl_->worker = impl_->dependencies.launch([this] { impl_->run(); });
        if (!impl_->worker.joinable()) { throw std::runtime_error("Camera thread launcher returned no worker"); }
        if (impl_->config.trigger_mode) {
            std::unique_lock<std::mutex> lock{impl_->wait_mutex};
            impl_->changed.wait(lock, [this] { return impl_->capture_ready || impl_->stopped.load(); });
            lock.unlock();
            if (impl_->stopped.load()) { throw std::runtime_error("Capture failed before trigger readiness"); }
            impl_->trigger_worker = impl_->dependencies.launch([this] { impl_->triggerRun(); });
            if (!impl_->trigger_worker.joinable()) { throw std::runtime_error("Trigger launcher returned no worker"); }
        }
        return true;
    } catch (const std::exception &error) {
        impl_->fail(error.what());
    } catch (...) {
        impl_->fail("Unknown camera start failure");
    }
    stop();
    return false;
}
bool Ar0234CameraFrameSource::stop() {
    const auto before{std::chrono::steady_clock::now()};
    impl_->requestStop();
    if (impl_->trigger_worker.joinable()) { impl_->trigger_worker.join(); }
    if (impl_->worker.joinable()) { impl_->worker.join(); }
    impl_->finish();
    const auto elapsed{std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - before).count()};
    std::lock_guard<std::mutex> lock{impl_->mutex};
    impl_->record.stop_us = elapsed;
    impl_->record.source.stop_us = elapsed;
    return impl_->record.source.state != FrameSourceState::Failed;
}
void Ar0234CameraFrameSource::stopTriggers() {
    {
        std::lock_guard<std::mutex> lock{impl_->wait_mutex};
        impl_->triggers_stopped.store(true);
    }
    impl_->changed.notify_all();
    if (impl_->trigger_worker.joinable()) { impl_->trigger_worker.join(); }
}
bool Ar0234CameraFrameSource::isRunning() const { return status().state == FrameSourceState::Running; }
FrameSourceStatus Ar0234CameraFrameSource::status() const { return diagnostic().source; }
Ar0234CameraDiagnostic Ar0234CameraFrameSource::diagnostic() const {
    std::lock_guard<std::mutex> lock{impl_->mutex};
    return impl_->record;
}
bool Ar0234CameraFrameSource::getImageSize(int &width, int &height) const {
    width = impl_->config.width;
    height = impl_->config.height;
    return true;
}
bool Ar0234CameraFrameSource::waitFrame(FrameRef &frame, std::chrono::microseconds timeout) {
    frame.reset();
    return impl_->queue->wait_dequeue_timed(frame, timeout);
}
bool Ar0234CameraFrameSource::tryFrame(FrameRef &frame) {
    frame.reset();
    return impl_->queue->try_dequeue(frame);
}
void Ar0234CameraFrameSource::discardFrames() {
    FrameRef frame{};
    while (impl_->queue->try_dequeue(frame)) {}
}
void Ar0234CameraFrameSource::configureRecording(bool enabled, const std::string &, const std::string &) {
    if (enabled) { throw std::invalid_argument("RK3576 camera recording unsupported"); }
}
} // namespace rk3576
