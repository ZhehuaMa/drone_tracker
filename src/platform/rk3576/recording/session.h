#ifndef RK3576_RECORDING_SESSION_H
#define RK3576_RECORDING_SESSION_H

#include <memory>
#include <functional>
#include <string>
#include <mutex>
#include <condition_variable>
#include "shared_slots.h"

namespace rk3576::recording {
/** @brief 校验启动值及已解析路径的挂载类型；资源权限由工作进程准备时处理。 */
void validateConfig(const FrameSourceConfig &config);

/** @brief 管理一轮独立录像进程；旧进程未回收时拒绝再次分配共享区。 */
class Session final {
public:
    using ReapProcess = std::function<pid_t(pid_t, int *, int)>;
    explicit Session(FrameSourceConfig config, std::string executable = {}, ReapProcess reap = {});
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    void start(const CameraFormat &format) noexcept;
    void setContext(std::uint64_t stream, std::uint64_t tracking_round, FrameSourceStatus start);
    std::shared_ptr<CameraNv12Sink> sharedSink() noexcept;
    void stopAccepting() noexcept;
    void stop(const FrameSourceStatus &camera, const std::string &reason = "source_stopped") noexcept;
    CameraNv12Sink *sink() noexcept;
    CameraRecordingStatus status() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
/**
 * @brief Fixed capture-side dispatcher. Each frame pins one session from stage through
 * publish/cancel and disposition. Closing detaches reception without waiting for I/O.
 * drain() runs only on the recording control executor before closing session metadata.
 */
class SessionDispatcher final : public CameraNv12Sink {
public:
    void attach(std::shared_ptr<CameraNv12Sink> sink) {
        std::lock_guard<std::mutex> lock{mutex_};
        current_ = std::move(sink);
    }
    void detach() noexcept {
        std::lock_guard<std::mutex> lock{mutex_};
        if (auto slots = std::dynamic_pointer_cast<RecordingSlots>(current_)) {
            slots->header().stop_accepting.store(true, std::memory_order_release);
        }
        current_.reset();
    }
    void drain() {
        std::unique_lock<std::mutex> lock{mutex_};
        changed_.wait(lock, [this] { return !visiting_; });
    }
    void stage(const CameraNv12View &view) noexcept override {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            visiting_ = true;
            frame_ = current_;
        }
        if (frame_) { frame_->stage(view); }
    }
    void publish() noexcept override {
        // Linearize final acceptance with detach; pixel staging never holds this lock.
        std::lock_guard<std::mutex> lock{mutex_};
        if (frame_) { frame_->publish(); }
    }
    void cancel() noexcept override { if (frame_) { frame_->cancel(); } }
    void disposition(const CameraSample &sample, std::uint64_t gaps, bool dropped) noexcept override {
        if (frame_) { frame_->disposition(sample, gaps, dropped); }
    }
    void finishFrame() noexcept override {
        cancel();
        std::shared_ptr<CameraNv12Sink> released{};
        {
            std::lock_guard<std::mutex> lock{mutex_};
            released.swap(frame_);
            visiting_ = false;
        }
        changed_.notify_all();
    }
private:
    std::mutex mutex_{};
    std::condition_variable changed_{};
    std::shared_ptr<CameraNv12Sink> current_{}, frame_{};
    bool visiting_{};
};
} // namespace rk3576::recording
#endif // RK3576_RECORDING_SESSION_H
