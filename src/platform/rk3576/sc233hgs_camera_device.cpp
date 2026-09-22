#include "sc233hgs_camera_device.h"

#include <linux/videodev2.h>
#include <stdexcept>

namespace rk3576 {
namespace {
FrameSourceConfig forceSc233hgs(FrameSourceConfig config) {
    config.camera_sensor_model = CameraSensorModel::Sc233hgs;
    return config;
}
} // namespace
Sc233hgsCameraDevice::Sc233hgsCameraDevice(FrameSourceConfig config, std::shared_ptr<CameraSystem> system)
    : Rk3576CameraCaptureBase{forceSc233hgs(std::move(config)), std::move(system)} {
    if (this->config().trigger_mode) {
        throw std::invalid_argument("SC233HGS supports continuous capture only; software trigger is unsupported");
    }
}
Sc233hgsCameraDevice::~Sc233hgsCameraDevice() {
    try { finish(); } catch (...) {}
}
void Sc233hgsCameraDevice::configureSensor() {
    // SC233HGS 直接打开已经由生产板系统镜像准备好的 rkisp_mainpath 采集节点；
    // 不调用 media-ctl，也不读写任何传感器控制项。
}
std::uint32_t Sc233hgsCameraDevice::pixelFormat() const { return V4L2_PIX_FMT_NV12; }
const char *Sc233hgsCameraDevice::pixelFormatName() const { return "NV12"; }
bool Sc233hgsCameraDevice::hasOpenSensorState() const { return false; }
std::string Sc233hgsCameraDevice::finishSensor() { return {}; }
} // namespace rk3576
