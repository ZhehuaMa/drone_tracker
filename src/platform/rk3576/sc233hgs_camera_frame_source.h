#ifndef RK3576_SC233HGS_CAMERA_FRAME_SOURCE_H
#define RK3576_SC233HGS_CAMERA_FRAME_SOURCE_H

#include "ar0234_camera_frame_source.h"

namespace rk3576 {
using Sc233hgsCameraDependencies = Ar0234CameraDependencies;
using Sc233hgsCameraDiagnostic = Ar0234CameraDiagnostic;

/**
 * @brief SC233HGS 连续帧源。
 *
 * 该类保留独立的传感器型号入口，实际队列和时间语义复用已验证的单生产者、
 * 单消费者实现。SC233HGS 配置始终关闭软件触发。
 */
class Sc233hgsCameraFrameSource final : public FrameSource {
public:
    explicit Sc233hgsCameraFrameSource(FrameSourceConfig config, Sc233hgsCameraDependencies dependencies = {});
    ~Sc233hgsCameraFrameSource() override;

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
    std::unique_ptr<Ar0234CameraFrameSource> implementation_;
};
} // namespace rk3576

#endif // RK3576_SC233HGS_CAMERA_FRAME_SOURCE_H
