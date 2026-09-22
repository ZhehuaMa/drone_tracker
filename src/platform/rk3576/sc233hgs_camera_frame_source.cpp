#include "sc233hgs_camera_frame_source.h"

#include "sc233hgs_camera_device.h"

#include <stdexcept>

namespace rk3576 {
Sc233hgsCameraFrameSource::Sc233hgsCameraFrameSource(
    FrameSourceConfig config, Sc233hgsCameraDependencies dependencies) {
    if (config.trigger_mode || config.camera_trigger_rate_hz != 0.0) {
        throw std::invalid_argument("SC233HGS supports continuous capture only; software trigger is unsupported");
    }
    config.camera_sensor_model = CameraSensorModel::Sc233hgs;
    // 注入构造 SC233HGS 专属设备的工厂，使共用的队列/触发实现无需反向依赖本类型。
    dependencies.make_device = [](const FrameSourceConfig &device_config,
                                  std::shared_ptr<CameraSystem> device_system) {
        return std::make_unique<Sc233hgsCameraDevice>(device_config, std::move(device_system));
    };
    implementation_ = std::make_unique<Ar0234CameraFrameSource>(std::move(config), std::move(dependencies));
}

Sc233hgsCameraFrameSource::~Sc233hgsCameraFrameSource() { stop(); }
bool Sc233hgsCameraFrameSource::start() { return implementation_->start(); }
bool Sc233hgsCameraFrameSource::stop() {
    const bool result{implementation_->stop()};
    implementation_->discardFrames();
    return result;
}
bool Sc233hgsCameraFrameSource::isRunning() const { return implementation_->isRunning(); }
FrameSourceStatus Sc233hgsCameraFrameSource::status() const { return implementation_->status(); }
bool Sc233hgsCameraFrameSource::getImageSize(int &width, int &height) const {
    return implementation_->getImageSize(width, height);
}
bool Sc233hgsCameraFrameSource::waitFrame(FrameRef &frame, std::chrono::microseconds timeout) {
    return implementation_->waitFrame(frame, timeout);
}
bool Sc233hgsCameraFrameSource::tryFrame(FrameRef &frame) { return implementation_->tryFrame(frame); }
void Sc233hgsCameraFrameSource::configureRecording(bool enabled, const std::string &directory,
                                                   const std::string &prefix) {
    implementation_->configureRecording(enabled, directory, prefix);
}
Sc233hgsCameraDiagnostic Sc233hgsCameraFrameSource::diagnostic() const {
    return implementation_->diagnostic();
}
} // namespace rk3576
