#include "sc233hgs_camera_frame_source.h"

#include "sc233hgs_camera_device.h"
#include "recording/session.h"

#include <stdexcept>
#include "platform/cpu_affinity.h"

namespace rk3576 {
Sc233hgsCameraFrameSource::Sc233hgsCameraFrameSource(
    FrameSourceConfig config, Sc233hgsCameraDependencies dependencies, std::unique_ptr<recording::Session> session) {
    if (config.trigger_mode || config.camera_trigger_rate_hz != 0.0) {
        throw std::invalid_argument("SC233HGS supports continuous capture only; software trigger is unsupported");
    }
    config.camera_sensor_model = CameraSensorModel::Sc233hgs;
    try {
        recording::validateConfig(config);
    } catch (const std::exception &error) {
        config.camera_recording.preparation_error = error.what();
    }
    if (config.camera_recording.enabled) {
        recording_ = session ? std::move(session) : std::make_unique<recording::Session>(config);
    }
    // 注入构造 SC233HGS 专属设备的工厂，使共用的队列/触发实现无需反向依赖本类型。
    dependencies.make_device = [](const FrameSourceConfig &device_config,
                                  std::shared_ptr<CameraSystem> device_system) {
        return std::make_unique<Sc233hgsCameraDevice>(device_config, std::move(device_system));
    };
    implementation_ = std::make_unique<Ar0234CameraFrameSource>(std::move(config), std::move(dependencies));
}

Sc233hgsCameraFrameSource::~Sc233hgsCameraFrameSource() {
    stop();
    {
        std::lock_guard<std::mutex> lock{recording_mutex_};
        recording_exit_ = true;
    }
    recording_changed_.notify_all();
    if (recording_thread_.joinable()) { recording_thread_.join(); }
}
void Sc233hgsCameraFrameSource::useSessionControl(std::uint64_t stream) {
    if (implementation_->isRunning()) { throw std::logic_error("Select session control before capture"); }
    if (session_control_) { return; }
    stream_id_ = stream;
    session_control_ = true;
    if (recording_) {
        implementation_->setRecordingSink(&dispatcher_);
        recording_thread_ = cpu_affinity::start(cpu_affinity::Role::Recording,
            [this] { recordingLoop(); });
    }
}
void Sc233hgsCameraFrameSource::requestRecording(std::uint64_t round, bool enabled, RecordingEndReason reason) {
    if (!session_control_ || !recording_) { return; }
    std::lock_guard<std::mutex> lock{recording_mutex_};
    if (recording_desired_ == enabled && (!enabled || tracking_round_ == round)) { return; }
    if (!enabled) {
        dispatcher_.endRequest();
        dispatcher_.detach();
        recording_->stopAccepting();
        end_snapshot_ = implementation_->status();
        end_reason_ = reason == RecordingEndReason::ServiceExit ? "service_exit"
            : reason == RecordingEndReason::CameraFailure ? "camera_failed"
            : reason == RecordingEndReason::TrackingFailure ? "tracking_failed" : "user_off";
    }
    if (enabled) {
        dispatcher_.beginRequest();
        start_snapshot_ = implementation_->status();
        request_status_ = {};
        request_status_.enabled = true;
        request_status_.phase = "pending";
        request_status_.request = {stream_id_, round, request_version_ + 1};
        desired_evidence_ = {};
        desired_evidence_.request = request_status_.request;
    } else { request_status_.terminal = true; }

    tracking_round_ = round;
    recording_desired_ = enabled;
    ++request_version_;
    recording_changed_.notify_all();
}
CameraRecordingStatus Sc233hgsCameraFrameSource::recordingRequestStatus() const {
    std::lock_guard<std::mutex> lock{recording_mutex_};
    auto status{request_status_};
    if (recording_ && active_request_.matches(status.request)) {
        const auto actual{recording_->status()};
        if (actual.phase != "idle" && status.phase != "pending" && status.phase != "preparing") {
            status = actual;
            status.request = request_status_.request;
            status.terminal = actual.phase == "failed" || actual.phase == "finished" || request_status_.terminal;
        }
    }
    return status;
}
void Sc233hgsCameraFrameSource::setStartupEvidence(const RecordingStartupEvidence &evidence) noexcept {
    std::lock_guard<std::mutex> lock{recording_mutex_};
    if (evidence.request.matches(active_request_)) {
        active_evidence_ = evidence;
        active_evidence_.preparation_omitted = dispatcher_.preparationOmitted();
    }
    if (evidence.request.matches(request_status_.request)) { desired_evidence_ = evidence; }
}
void Sc233hgsCameraFrameSource::finishRecording() noexcept {
    std::unique_lock<std::mutex> lock{recording_mutex_};
    recording_changed_.wait(lock, [this] { return !recording_busy_; });
}
void Sc233hgsCameraFrameSource::recordingLoop() noexcept {
    std::uint64_t handled{};
    while (true) {
        std::uint64_t version{}, round{};
        FrameSourceStatus start{};
        {
            std::unique_lock<std::mutex> lock{recording_mutex_};
            recording_changed_.wait(lock, [&] { return recording_exit_ || request_version_ != handled; });
            if (recording_exit_) { break; }
            handled = request_version_;
            if (!recording_desired_) { continue; }
            version = request_version_;
            round = tracking_round_;
            start = start_snapshot_;
            recording_busy_ = true;
            active_request_ = request_status_.request;
            active_evidence_ = desired_evidence_;
            request_status_.phase = "preparing";
        }
        recording_->setContext(stream_id_, round, std::move(start), version);
        recording_->start(implementation_->diagnostic().format);
        {
            std::unique_lock<std::mutex> lock{recording_mutex_};
            if (!recording_exit_ && recording_desired_ && request_version_ == version) {
                request_status_.phase = recording_->status().phase;
                dispatcher_.attach(recording_->sharedSink());
                recording_changed_.wait(lock, [&] { return recording_exit_ || request_version_ != version; });
            }
            dispatcher_.detach();
            recording_->stopAccepting();
        }
        dispatcher_.drain();
        FrameSourceStatus snapshot{};
        std::string reason{};
        {
            std::lock_guard<std::mutex> lock{recording_mutex_};
            recording_->setStartupEvidence(active_evidence_);
            snapshot = end_snapshot_;
            reason = end_reason_;
        }
        recording_->stop(snapshot, reason);
        {
            std::lock_guard<std::mutex> lock{recording_mutex_};
            recording_busy_ = false;
        }
        recording_changed_.notify_all();
    }
}
bool Sc233hgsCameraFrameSource::start() {
    if (implementation_->isRunning()) { return false; }
    implementation_->stop();
    if (recording_ && !session_control_) {
        recording_->start(implementation_->diagnostic().format);
        if (recording_->status().phase == "failed") {
            recording_->stop(implementation_->status());
        } else {
            implementation_->setRecordingSink(recording_->sink());
        }
    }
    const bool started{implementation_->start()};
    if (!started && recording_ && !session_control_) { recording_->stop(implementation_->status()); }
    return started;
}
bool Sc233hgsCameraFrameSource::stop() {
    if (session_control_) { requestRecording(0, false, RecordingEndReason::ServiceExit); }
    else if (recording_) { recording_->stopAccepting(); }
    const bool result{implementation_->stop()};
    if (recording_ && !session_control_) { recording_->stop(implementation_->status()); }
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
