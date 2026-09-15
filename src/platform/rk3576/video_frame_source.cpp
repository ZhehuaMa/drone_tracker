#include "video_frame_source.h"

#include <iostream>
#include <stdexcept>

#include "host_frame.h"
#include "mv_camera/readerwriterqueue.h"

namespace rk3576 {
namespace {
void logDiagnostic(const VideoDiagnostic &value) {
    static std::mutex mutex{};
    std::lock_guard<std::mutex> lock{mutex};
    std::clog << "rk3576 video [" << value.path << "] state=" << static_cast<int>(value.state)
              << " frame=" << value.frame_index << " pts=";
    if (value.has_pts) {
        std::clog << value.pts;
    } else {
        std::clog << "missing";
    }
    std::clog << " read=" << value.read << " enqueued=" << value.enqueued << " dropped=" << value.dropped
              << " reason=" << value.reason << '\n';
}
} // namespace
struct Rk3576VideoFrameSource::Impl {
    using Queue = moodycamel::BlockingReaderWriterQueue<FrameRef>;
    FrameSourceConfig config{};
    VideoDependencies dependencies{};
    std::atomic<bool> stopped{false};
    std::atomic<VideoState> state{VideoState::Ready};
    std::unique_ptr<VideoReader> reader{};
    std::unique_ptr<Queue> queue{std::make_unique<Queue>(30)};
    VideoSample first{};
    std::thread worker{};
    int width{};
    int height{};
    std::int64_t base{};
    VideoClock::TimePoint origin{};
    VideoDiagnostic record{};
    mutable std::mutex diagnostic_mutex{};
    VideoDiagnostic published{};

    Impl(const FrameSourceConfig &input, VideoDependencies injected)
        : config{input}, dependencies{std::move(injected)} {
        if (!dependencies.open) {
            dependencies.open = [](const std::string &path, const std::atomic<bool> &flag) {
                return std::make_unique<MjpegVideoReader>(path, flag);
            };
        }
        if (!dependencies.clock) {
            dependencies.clock = std::make_shared<VideoClock>();
        }
        if (!dependencies.launch) {
            dependencies.launch = [](std::function<void()> work) { return std::thread{std::move(work)}; };
        }
        if (!dependencies.diagnose) {
            dependencies.diagnose = logDiagnostic;
        }
        record.path = config.path;
        prepare();
        reader.reset();
        first = {};
        publish(VideoState::Ready);
    }
    void prepare() {
        reader = dependencies.open(config.path, stopped);
        if (!reader) {
            throw std::runtime_error("Reader creation returned empty");
        }
        auto result{reader->next()};
        if (result.status != VideoReadStatus::Frame || result.sample.gray.empty()
            || result.sample.gray.type() != CV_8UC1) {
            throw std::runtime_error("First frame: " + result.reason);
        }
        first = std::move(result.sample);
        record.frame_index = 1;
        record.pts = first.pts;
        record.has_pts = first.has_pts;
        if (width != 0 && (width != first.gray.cols || height != first.gray.rows)) {
            throw std::runtime_error("Reopened video dimensions changed");
        }
        width = first.gray.cols;
        height = first.gray.rows;
        const auto pts{videoPtsUs(first.pts, first.has_pts, reader->timeBase(), -1)};
        if (config.use_history_timestamp) {
            base = historyBase(reader->metadata());
            videoTimestamp(base, pts);
        }
    }
    void publish(VideoState next) noexcept {
        // 状态改变不依赖诊断分配成功；回调抛出也不能逃逸工作线程。
        state.store(next);
        try {
            record.state = next;
            {
                std::lock_guard<std::mutex> lock{diagnostic_mutex};
                published = record;
            }
            dependencies.diagnose(record);
        } catch (...) {
        }
    }
    void fail(const char *reason) noexcept {
        try {
            record.reason = reason;
        } catch (...) {
        }
        publish(VideoState::Failed);
    }
    void run() noexcept {
        try {
            VideoSample sample{std::move(first)};
            record.frame_index = 0;
            std::int64_t previous{-1};
            while (!stopped.load()) {
                ++record.read;
                ++record.frame_index;
                record.pts = sample.pts;
                record.has_pts = sample.has_pts;
                if (sample.gray.type() != CV_8UC1 || sample.gray.cols != width || sample.gray.rows != height) {
                    throw std::runtime_error("Decoded video dimensions or format changed");
                }
                const auto pts{videoPtsUs(sample.pts, sample.has_pts, reader->timeBase(), previous)};
                const auto timestamp{videoTimestamp(base, pts)};
                const auto deadline{videoDeadline(origin, pts)};
                previous = pts;
                if (!dependencies.clock->waitUntil(deadline, stopped) || stopped.load()) {
                    break;
                }
                auto frame{copyGrayFrame(sample.gray, static_cast<std::uint64_t>(timestamp))};
                if (queue->try_enqueue(std::move(frame))) {
                    ++record.enqueued;
                } else {
                    ++record.dropped;
                }
                // 逐帧快照供测试及内部诊断使用；默认日志仅在本轮结束输出。
                {
                    std::lock_guard<std::mutex> lock{diagnostic_mutex};
                    published = record;
                    published.state = VideoState::Running;
                }
                auto result{reader->next()};
                if (stopped.load()) {
                    break;
                }
                if (result.status == VideoReadStatus::Eof) {
                    record.reason = "Demux EOF";
                    publish(VideoState::Eof);
                    return;
                }
                if (result.status != VideoReadStatus::Frame) {
                    ++record.frame_index;
                    record.pts = result.sample.pts;
                    record.has_pts = result.sample.has_pts;
                    const char *kind{result.status == VideoReadStatus::DecodeError ? "JPEG decode: "
                        : result.status == VideoReadStatus::SizeChanged ? "Size change: " : "Demux read: "};
                    record.reason = std::string{kind} + result.reason;
                    publish(VideoState::Failed);
                    return;
                }
                sample = std::move(result.sample);
            }
            record.reason = "Stopped by caller";
            publish(VideoState::Stopped);
        } catch (const std::exception &error) {
            fail(error.what());
        } catch (...) {
            fail("Unknown worker failure");
        }
    }
};
Rk3576VideoFrameSource::Rk3576VideoFrameSource(const FrameSourceConfig &config, VideoDependencies dependencies)
    : impl_{std::make_unique<Impl>(config, std::move(dependencies))} {}
Rk3576VideoFrameSource::~Rk3576VideoFrameSource() {
    stop();
}
bool Rk3576VideoFrameSource::start() {
    if (isRunning()) {
        return false;
    }
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->stopped.store(false);
    try {
        impl_->record = {};
        impl_->record.path = impl_->config.path;
        impl_->queue = std::make_unique<Impl::Queue>(30);
        // 初始化仅验证输入；每轮都按当前路径打开，避免沿用已移走的文件句柄。
        impl_->reader.reset();
        impl_->first = {};
        impl_->prepare();
        if (!impl_->config.use_history_timestamp) {
            impl_->base = impl_->dependencies.clock->systemUs();
        }
        impl_->origin = impl_->dependencies.clock->steadyNow();
        impl_->record.frame_index = 1;
        impl_->record.pts = impl_->first.pts;
        impl_->record.has_pts = impl_->first.has_pts;
        const auto pts{videoPtsUs(impl_->first.pts, impl_->first.has_pts, impl_->reader->timeBase(), -1)};
        videoTimestamp(impl_->base, pts);
        videoDeadline(impl_->origin, pts);
        impl_->publish(VideoState::Running);
        impl_->worker = impl_->dependencies.launch([this] { impl_->run(); });
        if (!impl_->worker.joinable()) {
            throw std::runtime_error("Thread launcher returned no worker");
        }
        return true;
    } catch (const std::exception &error) {
        impl_->reader.reset();
        impl_->first = {};
        impl_->fail(error.what());
    } catch (...) {
        impl_->reader.reset();
        impl_->first = {};
        impl_->fail("Unknown start failure");
    }
    return false;
}
bool Rk3576VideoFrameSource::stop() {
    impl_->stopped.store(true);
    impl_->dependencies.clock->wake();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->reader.reset();
    impl_->first = {};
    if (impl_->state.load() == VideoState::Ready) {
        impl_->record.reason = "Stopped before start";
        impl_->publish(VideoState::Stopped);
    }
    return true;
}
bool Rk3576VideoFrameSource::isRunning() const {
    return impl_->state.load() == VideoState::Running;
}
bool Rk3576VideoFrameSource::getImageSize(int &width, int &height) const {
    width = impl_->width;
    height = impl_->height;
    return true;
}
bool Rk3576VideoFrameSource::waitFrame(FrameRef &frame, std::chrono::microseconds timeout) {
    frame.reset();
    return impl_->queue->wait_dequeue_timed(frame, timeout);
}
bool Rk3576VideoFrameSource::tryFrame(FrameRef &frame) {
    frame.reset();
    return impl_->queue->try_dequeue(frame);
}
void Rk3576VideoFrameSource::configureRecording(bool, const std::string &, const std::string &) {}
VideoDiagnostic Rk3576VideoFrameSource::diagnostic() const {
    std::lock_guard<std::mutex> lock{impl_->diagnostic_mutex};
    return impl_->published;
}
FrameSourceStatus Rk3576VideoFrameSource::status() const {
    const auto value{diagnostic()};
    FrameSourceState state{FrameSourceState::Unknown};
    switch (value.state) {
    case VideoState::Ready: state = FrameSourceState::Ready; break;
    case VideoState::Running: state = FrameSourceState::Running; break;
    case VideoState::Stopped: state = FrameSourceState::Stopped; break;
    case VideoState::Eof: state = FrameSourceState::Eof; break;
    case VideoState::Failed: state = FrameSourceState::Failed; break;
    }
    return {state, value.path, value.reason, value.frame_index, value.read, value.enqueued, value.dropped};
}
} // namespace rk3576
