#ifndef RK3576_SC233HGS_CAMERA_DEVICE_H
#define RK3576_SC233HGS_CAMERA_DEVICE_H

#include "rk3576_camera_capture_base.h"

namespace rk3576 {
/**
 * @brief SC233HGS 专属采集设备。
 *
 * mmap 缓冲区管理、出队入队轮询、停止唤醒、时钟映射和 Gray8 帧复制的生命
 * 周期与 AR0234 共用同一个 `Rk3576CameraCaptureBase` 基类实现；本类只提供
 * SC233HGS 专属部分：不通过 media-ctl 配置传感器、不读写任何触发相关控制
 * 项，只协商已由 ISP 准备好的 NV12 采集节点，最终只取用其中的 Y（亮度）平
 * 面作为 Gray8 输出。
 */
class Sc233hgsCameraDevice final : public Rk3576CameraCaptureBase {
public:
    Sc233hgsCameraDevice(FrameSourceConfig config, std::shared_ptr<CameraSystem> system);
    ~Sc233hgsCameraDevice() override;

    Sc233hgsCameraDevice(const Sc233hgsCameraDevice &) = delete;
    Sc233hgsCameraDevice &operator=(const Sc233hgsCameraDevice &) = delete;

protected:
    /** @brief SC233HGS 不需要 media-ctl 配置或控制项读写，直接使用已就绪的采集节点。 */
    void configureSensor() override;
    std::uint32_t pixelFormat() const override;
    const char *pixelFormatName() const override;
    bool hasOpenSensorState() const override;
    std::string finishSensor() override;
};
} // namespace rk3576

#endif // RK3576_SC233HGS_CAMERA_DEVICE_H
