#ifndef RK3576_RECORDING_SESSION_H
#define RK3576_RECORDING_SESSION_H

#include <memory>
#include <functional>
#include <string>
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
    void stopAccepting() noexcept;
    void stop(const FrameSourceStatus &camera) noexcept;
    CameraNv12Sink *sink() noexcept;
    CameraRecordingStatus status() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace rk3576::recording
#endif // RK3576_RECORDING_SESSION_H
