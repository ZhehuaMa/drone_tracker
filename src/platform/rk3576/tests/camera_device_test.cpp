#include "camera_test_support.h"

#include <algorithm>
#include <future>

#include <iostream>

namespace {
using namespace camera_test;
using namespace rk3576;
void initializationFailures() {
    for (const std::string failure : {"configure", "entity", "open_sensor", "sensor_format", "sensor_interval",
             "open_capture", "caps", "set_format", "get_format", "event", "allocate", "query", "map",
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
                && system->realtime_reads == 1, "System clock adjustment changed round offset");
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
void configurationAndTool() {
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
        initializationFailures(); frameAndCleanupFailures(); pixelsAndMetadata(); configurationAndTool();
        std::cout << "camera device tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
