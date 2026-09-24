#include "sc233hgs_camera_frame_source.h"

#include "sc233hgs_camera_device.h"
#include "recording/session.h"

#include <stdexcept>

namespace rk3576 {
Sc233hgsCameraFrameSource::Sc233hgsCameraFrameSource(
    FrameSourceConfig config, Sc233hgsCameraDependencies dependencies) {
    if (config.trigger_mode || config.camera_trigger_rate_hz != 0.0) {
        throw std::invalid_argument("SC233HGS supports continuous capture only; software trigger is unsupported");
    }
    config.camera_sensor_model = CameraSensorModel::Sc233hgs;
    recording::validateConfig(config);
    if (config.camera_recording.enabled) { recording_ = std::make_unique<recording::Session>(config); }
    // 注入构造 SC233HGS 专属设备的工厂，使共用的队列/触发实现无需反向依赖本类型。
    dependencies.make_device = [](const FrameSourceConfig &device_config,
                                  std::shared_ptr<CameraSystem> device_system) {
        return std::make_unique<Sc233hgsCameraDevice>(device_config, std::move(device_system));
    };
    implementation_ = std::make_unique<Ar0234CameraFrameSource>(std::move(config), std::move(dependencies));
}

Sc233hgsCameraFrameSource::~Sc233hgsCameraFrameSource() { stop(); }
bool Sc233hgsCameraFrameSource::start() {
    if (implementation_->isRunning()) { return false; }
    implementation_->stop();
    if (recording_) {
        recording_->start(implementation_->diagnostic().format);
        implementation_->setRecordingSink(recording_->sink());
    }
    const bool started{implementation_->start()};
    if (!started && recording_) { recording_->stop(implementation_->status()); }
    return started;
}
bool Sc233hgsCameraFrameSource::stop() {
    if (recording_) { recording_->stopAccepting(); }
    const bool result{implementation_->stop()};
    if (recording_) { recording_->stop(implementation_->status()); }
    implementation_->discardFrames();
    return result;
}
bool Sc233hgsCameraFrameSource::isRunning() const { return implementation_->isRunning(); }
FrameSourceStatus Sc233hgsCameraFrameSource::status() const {
    auto result{implementation_->status()};
    if (recording_) { result.recording = recording_->status(); }
    return result;
}
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
