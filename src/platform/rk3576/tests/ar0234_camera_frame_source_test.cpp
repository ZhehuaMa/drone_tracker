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
    Ar0234CameraFrameSource source{settings, dependencies(system)};
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
        Ar0234CameraFrameSource source{config(), dependencies(system)};
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
    Ar0234CameraFrameSource source{config(), injected};
    require(!source.start() && source.status().reason.find("launch failure") != std::string::npos,
            "Thread launch failure hidden");
    system->clean();
    system->frame_limit = 0;
    require(source.start(), "Thread launch retry failed");
    await([&] { return system->waiting.load(); });
    require(source.stop() && source.diagnostic().stop_us < 250000, "Blocked device stop failed");
    system->clean();
    injected.launch = [](std::function<void()>) { return std::thread{}; };
    Ar0234CameraFrameSource empty_worker{config(), injected};
    require(!empty_worker.start(), "Empty launcher accepted");
    system->clean();
}
void softwareLifecycle() {
    Ar0234TriggerPlan plan{0, 50000, 1};
    require(plan.skipLate(120000) == 2 && plan.due() == 150000, "Late periods were not skipped");
    require(plan.skipLate(150001) == 0, "Ordinary wake jitter skipped a period");
    auto settings{config()}; settings.trigger_mode = true; settings.camera_trigger_rate_hz = 20;
    auto system{std::make_shared<System>()}; system->frame_limit = 0;
    auto injected{dependencies(system)};
    std::atomic<std::int64_t> clock{0};
    injected.trigger_now = [&] { return clock.load(); };
    injected.trigger_wait = [&](std::condition_variable &, std::unique_lock<std::mutex> &,
                                std::int64_t due, const std::function<bool()> &) { clock.store(due); };
    injected.trigger_limit = 3;
    injected.observe_trigger = [&](const Ar0234TriggerEvent &event) {
        require(event.success && event.after_us >= event.before_us, "Bad trigger event");
        if (clock == 0) { clock = 120000; }
    };
    Ar0234CameraFrameSource source{settings, injected};
    for (int round{0}; round < 2; ++round) {
        clock = 0;
        require(source.start(), "Software start failed");
        await([&] { return source.status().trigger_success == 3; });
        source.stopTriggers();
        const auto state{source.status()};
        require(state.trigger_skipped == 2 && state.read == 0 && state.trigger_errors == 0,
                "Trigger/capture counters mixed");
        require(source.stop() && source.diagnostic().stop_us < 250000, "Software stop exceeded limit");
        require(source.stop(), "Repeated software stop failed");
        system->clean();
    }
    auto failed{std::make_shared<System>()}; failed->frame_limit = 0;
    auto launch{dependencies(failed)};
    int calls{};
    launch.launch = [&](std::function<void()> work) {
        if (++calls == 2) { throw std::runtime_error("second launch failed"); }
        return std::thread{std::move(work)};
    };
    Ar0234CameraFrameSource rollback{settings, launch};
    require(!rollback.start(), "Second thread failure accepted");
    require(rollback.status().reason.find("second launch") != std::string::npos, "Lost launch failure");
    require(!rollback.stop(), "Failure lost after stop");
    failed->clean();

    // 触发调用在途时令采集等待失败；清理必须等待触发返回。
    class CaptureFailureSystem final : public System {
    public:
        std::atomic<bool> fail_poll{false};
        int poll(pollfd *fds, nfds_t count, int timeout) override {
            if (fail_poll) { errno = EIO; return -1; }
            return System::poll(fds, count, timeout);
        }
    };
    auto concurrent{std::make_shared<CaptureFailureSystem>()}; concurrent->frame_limit = 0;
    std::promise<void> entered{}; std::promise<void> release{};
    auto permit{release.get_future().share()};
    concurrent->trigger_hook = [&] { entered.set_value(); permit.wait(); };
    auto overlap{dependencies(concurrent)};
    overlap.trigger_limit = 1;
    Ar0234CameraFrameSource active{settings, overlap};
    require(active.start(), "In-flight source start");
    entered.get_future().wait();
    concurrent->fail_poll = true;
    await([&] { return active.status().state == FrameSourceState::Failed; });
    auto stopping{std::async(std::launch::async, [&] { return active.stop(); })};
    require(stopping.wait_for(20ms) == std::future_status::timeout, "Closed in-flight trigger");
    release.set_value();
    require(!stopping.get(), "Capture failure was hidden");
    concurrent->clean();

    auto command_error{std::make_shared<System>()}; command_error->frame_limit = 0;
    command_error->failure = "trigger";
    Ar0234CameraFrameSource broken{settings, dependencies(command_error)};
    require(broken.start(), "Command error start");
    await([&] { return broken.status().state == FrameSourceState::Failed; });
    require(!broken.stop() && broken.status().trigger_errors == 1, "Command failure hidden");
    command_error->clean();
}
void timeoutAndFactory() {
    auto system{std::make_shared<System>()};
    system->frame_limit = 0; system->auto_timeout = true;
    Ar0234CameraFrameSource source{config(), dependencies(system)};
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
        queueAndRestart(); failureAndRetry(); softwareLifecycle(); timeoutAndFactory();
        std::cout << "camera frame source tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
