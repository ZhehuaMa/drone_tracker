#ifndef RK3576_SC233HGS_CAMERA_FRAME_SOURCE_H
#define RK3576_SC233HGS_CAMERA_FRAME_SOURCE_H

#include "ar0234_camera_frame_source.h"
#include "recording/session.h"

namespace rk3576 {
namespace recording { class Session; }
using Sc233hgsCameraDependencies = Ar0234CameraDependencies;
using Sc233hgsCameraDiagnostic = Ar0234CameraDiagnostic;

/**
 * @brief SC233HGS 连续帧源。
 *
 * 该类保留独立的传感器型号入口，实际队列和时间语义复用已验证的单生产者、
 * 单消费者实现。SC233HGS 配置始终关闭软件触发。
 */
class Sc233hgsCameraFrameSource final : public FrameSource, public CameraRecordingControl {
public:
    explicit Sc233hgsCameraFrameSource(FrameSourceConfig config, Sc233hgsCameraDependencies dependencies = {},
        std::unique_ptr<recording::Session> session = {});
    ~Sc233hgsCameraFrameSource() override;

    CameraRecordingControl *recordingControl() noexcept override { return this; }
    void useSessionControl(std::uint64_t stream) override;
    void finishRecording() noexcept override;
    void requestRecording(std::uint64_t tracking_round, bool enabled,
        RecordingEndReason reason = RecordingEndReason::UserOff) override;
    bool start() override;
    bool stop() override;
    bool isRunning() const override;
    FrameSourceStatus status() const override;
    bool getImageSize(int &width, int &height) const override;
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override;
    bool tryFrame(FrameRef &frame) override;
    void configureRecording(bool enabled, const std::string &, const std::string &) override;
    Sc233hgsCameraDiagnostic diagnostic() const;

private:
    void recordingLoop() noexcept;
    std::unique_ptr<recording::Session> recording_;
    recording::SessionDispatcher dispatcher_{};
    std::mutex recording_mutex_{};
    std::condition_variable recording_changed_{};
    std::thread recording_thread_{};
    bool session_control_{}, recording_exit_{}, recording_desired_{}, recording_busy_{};
    std::uint64_t stream_id_{}, tracking_round_{}, request_version_{};
    FrameSourceStatus start_snapshot_{}, end_snapshot_{};
    std::string end_reason_{"user_off"};
    std::unique_ptr<Ar0234CameraFrameSource> implementation_;
};
} // namespace rk3576

#endif // RK3576_SC233HGS_CAMERA_FRAME_SOURCE_H
