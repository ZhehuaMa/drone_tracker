#include "session.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

#include "../sc233hgs_camera_device.h"

/** @brief 无模型的真实相机/录像联合验收入口，路径和秒数均须显式提供。 */
int main(int argc, char **argv) {
    try {
        if (argc != 6) {
            throw std::invalid_argument("usage: recording_driver DIRECTORY SECONDS CAMERA MEDIA ENTITY");
        }
        const int seconds{std::stoi(argv[2])};
        if (seconds < 1 || seconds > 1800) { throw std::invalid_argument("duration must be 1..1800 seconds"); }
        FrameSourceConfig config{};
        config.path = argv[3]; config.camera_media_device = argv[4]; config.camera_sensor_entity = argv[5];
        config.width = 1920; config.height = 1200; config.fps = 60; config.trigger_mode = false;
        config.camera_sensor_model = CameraSensorModel::Sc233hgs;
        config.camera_recording.enabled = true; config.camera_recording.directory = argv[1];
        config.camera_recording.segment_seconds = 10;
        config.camera_recording.identity = "independent recording_driver; no models/ROS/OSD";
        rk3576::Sc233hgsCameraDevice camera{config, std::make_shared<rk3576::CameraSystem>()};
        camera.prepare();
        rk3576::recording::Session recording{config};
        recording.start(camera.format());
        std::cout << "prepared phase=" << recording.status().phase << " reason=" << recording.status().reason << std::endl;
        camera.start();
        FrameSourceStatus state{};
        state.state = FrameSourceState::Running;
        const auto start{std::chrono::steady_clock::now()};
        auto last_report{start};
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds{seconds}) {
            auto sample{camera.next(100, true, recording.sink())};
            if (sample.result == rk3576::CameraSampleResult::FrameReady) { ++state.read; ++state.enqueued; }
            state.time_source = sample.strategy;
            const auto now{std::chrono::steady_clock::now()};
            if (now - last_report >= std::chrono::seconds{1}) {
                const auto status{recording.status()};
                std::cout << "phase=" << status.phase << " accepted=" << status.accepted << " dropped=" << status.dropped
                    << " written=" << status.written << " synced=" << status.synced << " reason=" << status.reason << std::endl;
                last_report = now;
            }
        }
        recording.stopAccepting();
        const auto error{camera.finish()};
        state.state = error.empty() ? FrameSourceState::Stopped : FrameSourceState::Failed;
        state.reason = error; state.camera = camera.captureStatus(); state.sequence_gaps = camera.sequenceGaps();
        recording.stop(state);
        const auto final{recording.status()};
        std::cout << "final phase=" << final.phase << " accepted=" << final.accepted << " dropped=" << final.dropped
            << " encoded=" << final.encoded << " synced=" << final.synced << " stop_us=" << final.stop_us
            << " session=" << final.session << " reason=" << final.reason << std::endl;
        return final.phase == "finished" && error.empty() ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
