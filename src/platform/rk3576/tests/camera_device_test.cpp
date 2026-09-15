#include "camera_test_support.h"

#include <algorithm>
#include <future>
#include <limits>

#include <iostream>

namespace {
using namespace camera_test;
using namespace rk3576;
void initializationFailures() {
    for (const std::string failure : {"configure", "entity", "open_sensor", "sensor_format", "sensor_interval",
             "control_query", "control_get", "control_set", "open_capture", "caps", "set_format", "get_format", "event", "allocate", "query", "map",
             "queue", "clock", "stream_on"}) {
        auto system{std::make_shared<System>()};
        system->failure = failure;
        CameraDevice device{config(), system};
        rejects([&] { device.prepare(); device.start(); }, failure == "configure" || failure == "entity"
            ? "tool failure" : "");
        device.finish();
        system->clean();
    }
    for (const std::string failure : {"query", "map", "queue"}) {
        auto system{std::make_shared<System>()};
        system->failure = failure;
        system->occurrence = 3;
        CameraDevice device{config(), system};
        device.prepare();
        rejects([&] { device.start(); }, "");
        require(system->counts["unmap"] == (failure == "queue" ? 3 : 2), "Partial mapping cleanup count");
        system->clean();
    }
    for (int kind{0}; kind < 7; ++kind) {
        auto system{std::make_shared<System>()};
        system->bad_sensor = kind == 0;
        system->bad_interval = kind == 1;
        system->bad_format = kind == 2;
        system->bad_caps = kind == 3;
        system->bad_query = kind == 4;
        system->format_changes_after_buffers = kind == 5;
        if (kind == 6) { system->buffer_count = 1; }
        CameraDevice device{config(), system};
        rejects([&] { device.prepare(); device.start(); }, "");
        system->clean();
    }
}
void frameAndCleanupFailures() {
    for (const std::string failure : {"poll", "dequeue", "return", "stream_off", "release", "unmap", "close"}) {
        auto system{std::make_shared<System>()};
        CameraDevice device{config(), system};
        device.prepare();
        device.start();
        system->failure = failure;
        system->occurrence = system->counts[failure] + 1;
        if (failure == "poll" || failure == "dequeue" || failure == "return") {
            rejects([&] { device.next(1); }, "");
        } else {
            require(!device.finish().empty(), "Cleanup error discarded");
        }
        device.finish();
        system->clean();
        if (failure == "stream_off") {
            const auto off{std::find(system->calls.begin(), system->calls.end(), "stream_off")};
            require(off != system->calls.end() && *(off + 1) == "close" && *(off + 2) == "unmap",
                    "STREAMOFF failure must close before unmap");
        }
    }
}
void pixelsAndMetadata() {
    FrameRef held{};
    std::unique_ptr<FrameReadView> view{};
    auto system{std::make_shared<System>()};
    {
        CameraDevice device{config(), system};
        device.prepare();
        require(system->counts["allocate"] == 0 && system->counts["stream_on"] == 0, "Probe starts capture");
        device.start();
        const auto sample{device.next(1, true)};
        require(sample.pixels_checked && sample.pixels_equal && sample.strategy == "driver_monotonic",
                "Missing pixel/time evidence");
        held = sample.frame;
        view = held->mapRead();
        require(view->rowStrideBytes() == 1920, "Padding copied into output");
        require(system->counts["return"] == 1, "Frame published before return");
        system->realtime -= 9000000000;
        const auto after_clock_adjustment{device.next(1)};
        require(after_clock_adjustment.frame->timestampUs() == sample.frame->timestampUs() + 16000
                && after_clock_adjustment.clock_offset_delta_us < -8999000000LL,
                "System clock adjustment changed round offset or lost diagnostic");
        for (int i{0}; i < 5; ++i) { device.next(1); }
        require(device.finish().empty(), "Normal cleanup failed");
    }
    system->pixels.clear();
    system->clean();
    held.reset();
    for (int row{0}; row < 1200; ++row) {
        for (int column{0}; column < 1920; ++column) {
            require(view->mat().at<unsigned char>(row, column) == (row + column) % 251, "Pixel/lifetime mismatch");
        }
    }
    for (int kind{0}; kind < 8; ++kind) {
        auto broken{std::make_shared<System>()};
        broken->mutate = [kind](v4l2_buffer &value) {
            auto &plane{value.m.planes[0]};
            switch (kind) {
            case 0: value.index = 99; break;
            case 1: value.length = 2; break;
            case 2: value.flags |= V4L2_BUF_FLAG_ERROR; break;
            case 3: plane.data_offset = plane.bytesused + 1; break;
            case 4: plane.bytesused = plane.length + 1; break;
            case 5: plane.length += 100; plane.bytesused += 100; break;
            case 6: plane.bytesused = plane.data_offset + 10; break;
            case 7: value.timestamp.tv_usec = 1000000; break;
            }
        };
        CameraDevice device{config(), broken};
        device.prepare(); device.start();
        rejects([&] { device.next(1); }, kind == 7 ? "timeval" : "Invalid dequeued buffer");
        require(broken->counts["return"] == 0, "Invalid metadata released as success");
        device.finish(); broken->clean();
    }
}
void controlLifecycle() {
    auto settings{config()};
    settings.trigger_mode = true;
    settings.camera_trigger_rate_hz = 20;
    for (int failure_at : {1, 2, 3}) {
        auto system{std::make_shared<System>()};
        const auto original{system->controls};
        system->failure = "control_set";
        system->occurrence = failure_at;
        CameraDevice device{settings, system};
        rejects([&] { device.prepare(); }, "set ");
        require(system->controls == original, "Partial configuration not restored");
        system->clean();
    }
    auto system{std::make_shared<System>()};
    const auto original{system->controls};
    CameraDevice device{settings, system};
    device.prepare();
    require(system->trigger_calls == 0 && !system->streaming, "Prepare triggered acquisition");
    device.start();
    device.trigger();
    require(system->trigger_calls == 1, "Single command not sent");
    system->failure = "trigger"; system->occurrence = 2;
    rejects([&] { device.trigger(); }, "soft_trgone");
    system->failure = "control_set";
    system->occurrence = system->counts["control_set"] + 1;
    require(device.finish().find("frame_rate") != std::string::npos, "Restore failure missing control");
    require(device.unrestoredControls() == std::vector<std::string>{"frame_rate"}, "Lost unrestored item");
    require(system->descriptors.empty(), "Restore failure leaked control handle");
    require(device.finish().empty() && device.unrestoredControls().empty(), "Restore retry failed");
    require(system->controls == original, "Wrong restored values");
    system->clean();
    const auto closed{system->counts["close"]};
    device.finish();
    require(system->counts["close"] == closed, "Repeated finish closed twice");
    auto unreadable{std::make_shared<System>()};
    unreadable->failure = "control_get"; unreadable->occurrence = 2;
    CameraDevice blocked{settings, unreadable};
    rejects([&] { blocked.prepare(); }, "read");
    require(unreadable->counts["control_set"] == 0 && unreadable->counts["configure"] == 0,
            "Modified controls before all snapshots available");
    unreadable->clean();
}
/** @brief 模拟写入已受理但传感器稍后才返回新值，不依赖真实睡眠。 */
class DelayedControlSystem final : public System {
public:
    std::int64_t now{};
    std::int64_t delay_us{20000};
    std::int64_t ready_us{};
    int pending_value{};
    bool pending{};
    int sleeps{};
    std::int64_t clockUs(clockid_t) override { return now; }
    void sleepUs(std::int64_t duration_us) override {
        require(duration_us > 0 && duration_us <= 5000, "Invalid control retry wait");
        now += duration_us;
        ++sleeps;
    }
    int ioctl(int fd, unsigned long request, void *argument) override {
        constexpr unsigned SOURCE{0x00981902};
        if (request == VIDIOC_G_CTRL && pending && now >= ready_us) {
            controls.at(SOURCE) = pending_value;
            pending = false;
        }
        if (request == VIDIOC_S_CTRL) {
            const auto value{*static_cast<v4l2_control *>(argument)};
            const int old{controls.count(value.id) ? controls.at(value.id) : 0};
            const int result{System::ioctl(fd, request, argument)};
            if (result == 0 && value.id == SOURCE) {
                pending = old != value.value;
                pending_value = value.value;
                ready_us = now + delay_us;
                controls.at(SOURCE) = old;
            }
            return result;
        }
        return System::ioctl(fd, request, argument);
    }
};
void controlReadbackDeadline() {
    auto settings{config()};
    settings.trigger_mode = true;
    settings.camera_trigger_rate_hz = 20;
    for (const std::int64_t delay : {0, 20000, 100000}) {
        auto system{std::make_shared<DelayedControlSystem>()};
        system->delay_us = delay;
        CameraDevice device{settings, system};
        device.prepare();
        require(system->controls.at(0x00981902) == 0 && system->now == delay,
                "Configuration did not wait for requested control value");
        require(device.finish().empty() && system->controls.at(0x00981902) == 1
                && system->now == 2 * delay, "Delayed restore failed");
        require(system->counts["control_set"] == 6, "Readback retry rewrote control");
        system->clean();
    }
    auto system{std::make_shared<DelayedControlSystem>()};
    CameraDevice device{settings, system};
    device.prepare();
    system->delay_us = 200000;
    const auto before{system->now};
    const auto error{device.finish()};
    require(error.find("/dev/test-subdev restore trigger_src readback timeout expected=1 actual=0")
                != std::string::npos && error.find("elapsed_us=100000 timeout_us=100000") != std::string::npos,
            "Timeout missing actual/expected value or duration");
    require(system->now - before == 100000 && system->descriptors.empty()
            && device.unrestoredControls() == std::vector<std::string>{"trigger_src"},
            "Timeout lost recovery state or leaked handles");
    system->delay_us = 20000;
    require(device.finish().empty() && device.unrestoredControls().empty()
            && system->controls.at(0x00981902) == 1, "Recovery after timeout failed");
    system->clean();

    auto slow{std::make_shared<DelayedControlSystem>()};
    slow->delay_us = 200000;
    CameraDevice failed{settings, slow};
    rejects([&] { failed.prepare(); }, "set trigger_src readback timeout expected=0 actual=1");
    require(slow->now == 100000 && slow->controls.at(0x00981901) == 0
            && slow->controls.at(0x00981902) == 1, "Configuration timeout did not roll back");
    slow->clean();

    auto unreadable{std::make_shared<DelayedControlSystem>()};
    CameraDevice read_error{settings, unreadable};
    read_error.prepare();
    unreadable->failure = "control_get";
    unreadable->occurrence = unreadable->counts["control_get"] + 1;
    require(read_error.finish().find("restore frame_rate readback: Input/output error") != std::string::npos,
            "Read syscall error disguised as timeout");
    require(read_error.unrestoredControls() == std::vector<std::string>{"frame_rate"},
            "Read error discarded restore item");
    require(read_error.finish().empty(), "Read error recovery failed");
    unreadable->clean();
}
void configurationAndTool() {
    auto triggered{config()};
    triggered.trigger_mode = true;
    for (double rate : {1.0, 20.0, static_cast<double>(triggered.fps)}) {
        triggered.camera_trigger_rate_hz = rate;
        validateCameraConfig(triggered);
    }
    for (double rate : {0.0, -1.0, 0.5, static_cast<double>(triggered.fps) + 1.0,
                       std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        triggered.camera_trigger_rate_hz = rate;
        rejects([&] { validateCameraConfig(triggered); }, "camera_trigger_rate_hz");
    }
    auto continuous{config()};
    validateCameraConfig(continuous);
    for (double rate : {-1.0, 20.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
        continuous.camera_trigger_rate_hz = rate;
        rejects([&] { validateCameraConfig(continuous); }, "camera_trigger_rate_hz");
    }
    for (int kind{0}; kind < 13; ++kind) {
        auto value{config()};
        switch (kind) {
        case 0: value.path.clear(); break;
        case 1: value.camera_media_device.clear(); break;
        case 2: value.camera_sensor_entity = "a\"b"; break;
        case 3: value.camera_sensor_entity = "a\nb"; break;
        case 4: value.width = 0; break;
        case 5: value.fps = 0; break;
        case 6: value.camera_buffer_count = 1; break;
        case 7: value.camera_buffer_count = 33; break;
        case 8: value.trigger_mode = true; break;
        case 9: value.flip = true; break;
        case 10: value.mirror = true; break;
        case 11: value.use_history_timestamp = true; break;
        case 12: value.output_width = 960; break;
        }
        rejects([&] { validateCameraConfig(value); }, "");
    }
    CameraSystem real{};
    require(real.execute({"/usr/bin/printf", "%s", "a b;$(ignored)"}, 1000) == "a b;$(ignored)",
            "Command parameter boundaries changed");
    rejects([&] { real.execute({"/nonexistent-camera-tool"}, 100); }, "tool missing");
    rejects([&] { real.execute({"/bin/false"}, 1000); }, "failed status");
    rejects([&] { real.execute({"/bin/sh", "-c", "printf sensor-error >&2; exit 7"}, 1000); }, "sensor-error");
    rejects([&] { real.execute({"/bin/sleep", "1"}, 20); }, "timeout");
    const int event{real.event()};
    require(event >= 0, "Real eventfd creation failed");
    auto wait{std::async(std::launch::async, [&] {
        pollfd descriptor{event, POLLIN, 0};
        return real.poll(&descriptor, 1, 2000) == 1 && (descriptor.revents & POLLIN);
    })};
    require(real.signal(event) == 0 && wait.wait_for(std::chrono::milliseconds{250}) == std::future_status::ready
            && wait.get(), "Real eventfd did not interrupt poll");
    require(real.close(event) == 0, "Real eventfd close failed");
}
} // namespace
int main() {
    try {
        initializationFailures(); frameAndCleanupFailures(); pixelsAndMetadata(); controlLifecycle();
        controlReadbackDeadline(); configurationAndTool();
        std::cout << "camera device tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
