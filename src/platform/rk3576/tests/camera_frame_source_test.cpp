#include "camera_test_support.h"
#include "../platform_factory.h"
#include "platform/frame_source_ops.h"

#include <future>
#include <iostream>

namespace {
using namespace camera_test;
using namespace rk3576;
using namespace std::chrono_literals;
void queueAndRestart() {
    auto system{std::make_shared<System>()};
    system->width = 8; system->height = 3; system->stride = 16;
    system->sequence_step = 2;
    auto settings{config()}; settings.width = 8; settings.height = 3;
    Rk3576CameraFrameSource source{settings, dependencies(system)};
    system->clean();
    require(source.status().state == FrameSourceState::Ready, "Factory probe state");
    source.configureRecording(false, "", "");
    rejects([&] { source.configureRecording(true, "", ""); }, "unsupported");
    FrameRef held{};
    for (int round{0}; round < 3; ++round) {
        require(source.start(), "Start failed");
        require(!source.start(), "Duplicate start accepted");
        await([&] { return source.status().read == 100; });
        const auto status{source.diagnostic()};
        require(status.source.dropped > 0 && status.source.enqueued + status.source.dropped == 100,
                "Queue did not drop new frames");
        require(status.driver_dropped == 99, "Driver drops mixed with queue drops");
        require(source.tryFrame(held) && held->mapRead()->mat().at<unsigned char>(0, 0) == 0,
                "Oldest queued frame overwritten or stale round");
        auto consumer{std::async(std::launch::async, [&] {
            FrameRef frame{};
            for (int i{0}; i < 200; ++i) { source.waitFrame(frame, 100us); }
        })};
        const auto before{std::chrono::steady_clock::now()};
        require(source.stop(), "Normal stop failed");
        require(std::chrono::steady_clock::now() - before < 250ms, "Stop exceeds 250 ms");
        consumer.get();
        system->clean();
        FrameRef frame{held};
        while (source.tryFrame(frame)) {}
        require(!frame, "tryFrame failure retains output");
        frame = held;
        require(!source.waitFrame(frame, 1us) && !frame, "waitFrame failure retains output");
        require(source.stop(), "Repeated stop failed");
    }
    require(held->mapRead()->mat().at<unsigned char>(0, 0) == 0, "Shared frame invalidated by stop");
}
void failureAndRetry() {
    for (const std::string failure : {"stream_on", "dequeue", "return", "stream_off"}) {
        auto system{std::make_shared<System>()};
        system->frame_limit = 1;
        Rk3576CameraFrameSource source{config(), dependencies(system)};
        system->failure = failure;
        const bool started{source.start()};
        if (failure == "stream_on") { require(!started, "STREAMON failure accepted"); }
        else if (failure == "stream_off") {
            await([&] { return source.status().read == 1; });
            require(!source.stop(), "STREAMOFF failure hidden");
        } else { await([&] { return source.status().state == FrameSourceState::Failed; }); }
        require(!source.stop() && !source.status().reason.empty(), "Error lost during cleanup");
        system->clean();
        system->failure.clear();
        require(source.start(), "Failed round cannot restart");
        await([&] { return source.status().read == 1; });
        require(source.stop(), "Recovered round stop failed");
        system->clean();
    }
    auto system{std::make_shared<System>()};
    auto injected{dependencies(system)};
    int calls{};
    injected.launch = [&](std::function<void()> work) {
        if (++calls == 1) { throw std::runtime_error("injected launch failure"); }
        return std::thread{std::move(work)};
    };
    Rk3576CameraFrameSource source{config(), injected};
    require(!source.start() && source.status().reason.find("launch failure") != std::string::npos,
            "Thread launch failure hidden");
    system->clean();
    system->frame_limit = 0;
    require(source.start(), "Thread launch retry failed");
    await([&] { return system->waiting.load(); });
    require(source.stop() && source.diagnostic().stop_us < 250000, "Blocked device stop failed");
    system->clean();
    injected.launch = [](std::function<void()>) { return std::thread{}; };
    Rk3576CameraFrameSource empty_worker{config(), injected};
    require(!empty_worker.start(), "Empty launcher accepted");
    system->clean();
}
void timeoutAndFactory() {
    auto system{std::make_shared<System>()};
    system->frame_limit = 0; system->auto_timeout = true;
    Rk3576CameraFrameSource source{config(), dependencies(system)};
    require(source.start(), "Timeout source start");
    await([&] { return source.status().state == FrameSourceState::Failed; });
    source.stop();
    require(source.status().reason.find("2 seconds") != std::string::npos, "No-frame timeout reported as EOF");
    system->clean();
    // 保留旧聚合初始化的字段顺序与默认限制。
    FrameSourceConfig old{FrameSourceType::Camera, "/dev/test", 1920, 1200, 60, false, 0, 0, false, false, false};
    require(old.camera_buffer_count == 4 && old.camera_media_device.empty(), "Aggregate compatibility");
    auto default_factory{createFactory({})};
    require(!default_factory->createFrameSource(config()), "Default factory enabled camera");
    FactoryOptions options{};
    options.enable_camera = true; options.camera = dependencies(system);
    auto enabled{createFactory({}, options)};
    auto result{enabled->createFrameSource(config())};
    require(result && result.value->status().state == FrameSourceState::Ready, "Explicit camera factory failed");
    system->clean();
    auto invalid{config()}; invalid.flip = true;
    auto failure{enabled->createFrameSource(invalid)};
    require(!failure && failure.error.platform == "rk3576" && failure.error.path == invalid.path,
            "Factory diagnostic identity");
}
} // namespace
int main() {
    try {
        queueAndRestart(); failureAndRetry(); timeoutAndFactory();
        std::cout << "camera frame source tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
