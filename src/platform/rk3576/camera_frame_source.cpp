#include "camera_frame_source.h"

#include <atomic>
#include <stdexcept>

#include "mv_camera/readerwriterqueue.h"

namespace rk3576 {
struct Rk3576CameraFrameSource::Impl {
    using Queue = moodycamel::BlockingReaderWriterQueue<FrameRef>;
    FrameSourceConfig config{};
    CameraDependencies dependencies{};
    std::unique_ptr<CameraDevice> device{};
    std::unique_ptr<Queue> queue{std::make_unique<Queue>(30)};
    std::thread worker{};
    std::atomic<bool> stopped{false};
    mutable std::mutex mutex{};
    CameraDiagnostic record{};

    Impl(FrameSourceConfig input, CameraDependencies injected)
        : config{std::move(input)}, dependencies{std::move(injected)} {
        if (!dependencies.system) { dependencies.system = std::make_shared<CameraSystem>(); }
        if (!dependencies.launch) {
            dependencies.launch = [](std::function<void()> work) { return std::thread{std::move(work)}; };
        }
        device = std::make_unique<CameraDevice>(config, dependencies.system);
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
    void finish() {
        const auto before{std::chrono::steady_clock::now()};
        const auto error{device->finish()};
        const auto elapsed{std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - before).count()};
        if (!error.empty()) { fail(error); }
        std::lock_guard<std::mutex> lock{mutex};
        record.cleanup_us = elapsed;
        if (record.source.state != FrameSourceState::Failed) {
            record.source.state = FrameSourceState::Stopped;
            record.source.reason = "Stopped by caller";
        }
    }
    void run() noexcept {
        try {
            auto last_dequeue{dependencies.system->clockUs(CLOCK_MONOTONIC)};
            bool have_sequence{false};
            std::uint32_t previous_sequence{};
            while (!stopped.load()) {
                auto sample{device->next(100, dependencies.compare_pixels)};
                if (stopped.load()) { break; }
                if (!sample.frame) {
                    const auto now{dependencies.system->clockUs(CLOCK_MONOTONIC)};
                    if (now < last_dequeue || now - last_dequeue >= 2000000) {
                        throw std::runtime_error("Camera capture timeout: no successful dequeue for 2 seconds");
                    }
                    continue;
                }
                last_dequeue = sample.dequeue_us;
                std::uint64_t missing{};
                if (have_sequence) {
                    // 无符号减法允许 sequence 在 2^32 处自然回绕；倒退/重复不是丢帧。
                    const std::uint32_t delta{sample.sequence - previous_sequence};
                    if (delta == 0 || delta > 0x7fffffffU) {
                        throw std::runtime_error("Camera sequence repeated/backwards: "
                            + std::to_string(sample.sequence));
                    }
                    missing = delta - 1;
                }
                previous_sequence = sample.sequence;
                have_sequence = true;
                const bool enqueued{queue->try_enqueue(sample.frame)};
                CameraDiagnostic snapshot{};
                {
                    std::lock_guard<std::mutex> lock{mutex};
                    ++record.source.read;
                    record.source.frame_index = sample.sequence;
                    record.driver_dropped += missing;
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
        try { finish(); } catch (const std::exception &error) { fail(error.what()); }
    }
};
Rk3576CameraFrameSource::Rk3576CameraFrameSource(FrameSourceConfig config, CameraDependencies dependencies)
    : impl_{std::make_unique<Impl>(std::move(config), std::move(dependencies))} {}
Rk3576CameraFrameSource::~Rk3576CameraFrameSource() { stop(); }
bool Rk3576CameraFrameSource::start() {
    if (isRunning()) { return false; }
    if (impl_->worker.joinable()) { impl_->worker.join(); }
    impl_->stopped.store(false);
    try {
        {
            std::lock_guard<std::mutex> lock{impl_->mutex};
            impl_->record = {};
            impl_->record.source.path = impl_->config.path;
        }
        impl_->queue = std::make_unique<Impl::Queue>(30);
        impl_->device = std::make_unique<CameraDevice>(impl_->config, impl_->dependencies.system);
        impl_->device->prepare();
        impl_->device->start();
        {
            std::lock_guard<std::mutex> lock{impl_->mutex};
            impl_->record.format = impl_->device->format();
            impl_->record.clock_pair = impl_->device->clockPair();
            impl_->record.source.state = FrameSourceState::Running;
        }
        impl_->worker = impl_->dependencies.launch([this] { impl_->run(); });
        if (!impl_->worker.joinable()) { throw std::runtime_error("Camera thread launcher returned no worker"); }
        return true;
    } catch (const std::exception &error) {
        impl_->fail(error.what());
    } catch (...) {
        impl_->fail("Unknown camera start failure");
    }
    impl_->finish();
    return false;
}
bool Rk3576CameraFrameSource::stop() {
    const auto before{std::chrono::steady_clock::now()};
    impl_->stopped.store(true);
    const auto error{impl_->device->wake()};
    if (!error.empty()) { impl_->fail(error); }
    if (impl_->worker.joinable()) { impl_->worker.join(); }
    // run 已清理设备；未启动或启动失败时 finish 同样幂等。
    if (status().state == FrameSourceState::Ready) { impl_->finish(); }
    const auto elapsed{std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - before).count()};
    std::lock_guard<std::mutex> lock{impl_->mutex};
    impl_->record.stop_us = elapsed;
    return impl_->record.source.state != FrameSourceState::Failed;
}
bool Rk3576CameraFrameSource::isRunning() const { return status().state == FrameSourceState::Running; }
FrameSourceStatus Rk3576CameraFrameSource::status() const { return diagnostic().source; }
CameraDiagnostic Rk3576CameraFrameSource::diagnostic() const {
    std::lock_guard<std::mutex> lock{impl_->mutex};
    return impl_->record;
}
bool Rk3576CameraFrameSource::getImageSize(int &width, int &height) const {
    width = impl_->config.width;
    height = impl_->config.height;
    return true;
}
bool Rk3576CameraFrameSource::waitFrame(FrameRef &frame, std::chrono::microseconds timeout) {
    frame.reset();
    return impl_->queue->wait_dequeue_timed(frame, timeout);
}
bool Rk3576CameraFrameSource::tryFrame(FrameRef &frame) {
    frame.reset();
    return impl_->queue->try_dequeue(frame);
}
void Rk3576CameraFrameSource::configureRecording(bool enabled, const std::string &, const std::string &) {
    if (enabled) { throw std::invalid_argument("RK3576 camera recording unsupported"); }
}
} // namespace rk3576
