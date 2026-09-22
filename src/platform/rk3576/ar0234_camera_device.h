#ifndef RK3576_AR0234_CAMERA_DEVICE_H
#define RK3576_AR0234_CAMERA_DEVICE_H

#include "rk3576_camera_capture_base.h"

namespace rk3576 {
/**
 * @brief AR0234 专属采集设备。
 *
 * mmap 缓冲区管理、出队入队轮询、停止唤醒、时钟映射和 Gray8 帧复制的生命
 * 周期已经抽取到 `Rk3576CameraCaptureBase`；本类只实现 AR0234 专属部分：
 * 通过 media-ctl 配置传感器格式、读写一组私有触发控制项、协商 Y8 单色格式，
 * 以及软件触发命令。
 */
class Ar0234CameraDevice final : public Rk3576CameraCaptureBase {
public:
    Ar0234CameraDevice(FrameSourceConfig config, std::shared_ptr<CameraSystem> system);
    ~Ar0234CameraDevice() override;
    Ar0234CameraDevice(const Ar0234CameraDevice &) = delete;
    Ar0234CameraDevice &operator=(const Ar0234CameraDevice &) = delete;

    /** @brief 直接发送单次软件命令；成功不表示相机已经曝光。 */
    void trigger() override;
    std::vector<std::string> unrestoredControls() const override;

protected:
    void configureSensor() override;
    std::uint32_t pixelFormat() const override;
    const char *pixelFormatName() const override;
    bool hasOpenSensorState() const override;
    std::string finishSensor() override;

private:
    void snapshotControls();
    void setControl(std::uint32_t id, int value, const std::string &name);
    void writeAndVerifyControl(std::uint32_t id, int value, const std::string &operation);
    std::string restoreControls();
    struct SavedControl { std::uint32_t id{}; int value{}; std::string name{}; bool changed{}; };
    std::vector<SavedControl> controls_{};
    mutable std::mutex control_mutex_{};
    int sensor_fd_{-1};
    std::string sensor_path_{};
};
} // namespace rk3576
#endif // RK3576_AR0234_CAMERA_DEVICE_H
