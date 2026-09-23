#include "sc233hgs_camera_device.h"
#include "sc233hgs_camera_frame_source.h"
#include "../platform_factory.h"
#include "camera_test_support.h"

#include <iostream>
#include <utility>

namespace {
using namespace rk3576;
using namespace camera_test;

FrameSourceConfig config() {
    FrameSourceConfig value{};
    value.path = "/dev/video31";
    value.width = 1920;
    value.height = 1200;
    value.fps = 60;
    value.trigger_mode = false;
    value.output_width = 0;
    value.output_height = 0;
    value.camera_media_device = "/dev/media0";
    value.camera_sensor_entity = "sc233hgs 4-0030";
    value.camera_trigger_rate_hz = 0.0;
    value.camera_sensor_model = CameraSensorModel::Sc233hgs;
    return value;
}

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void rejectsSoftwareTrigger() {
    auto value{config()};
    value.trigger_mode = true;
    value.camera_trigger_rate_hz = 20.0;
    bool rejected{};
    try {
        Sc233hgsCameraFrameSource source{value};
    } catch (const std::invalid_argument &error) {
        rejected = std::string{error.what()}.find("continuous") != std::string::npos;
    }
    require(rejected, "SC233HGS software trigger was not rejected");
}

void rejectsNonZeroContinuousRate() {
    auto value{config()};
    value.camera_trigger_rate_hz = 1.0;
    bool rejected{};
    try {
        Sc233hgsCameraFrameSource source{value};
    } catch (const std::invalid_argument &error) {
        rejected = std::string{error.what()}.find("continuous") != std::string::npos;
    }
    require(rejected, "SC233HGS non-zero continuous rate was not rejected");
}

void rejectsScaling() {
    auto value{config()};
    value.output_width = value.width / 2;
    bool rejected{};
    try {
        Sc233hgsCameraFrameSource source{value, dependencies(std::make_shared<System>())};
    } catch (const std::invalid_argument &error) {
        rejected = std::string{error.what()}.find("scaling unsupported") != std::string::npos;
    }
    require(rejected, "SC233HGS accepted non-native output dimensions");
}

void rejectsRecording() {
    auto system{std::make_shared<System>()};
    system->nv12 = true;
    auto value{config()};
    Sc233hgsCameraFrameSource source{value, dependencies(system)};
    rejects([&] { source.configureRecording(true, "/tmp/recording", "sc233hgs"); }, "unsupported");
    source.configureRecording(false, "", "");
}

void rejectsFormatMismatch() {
    auto system{std::make_shared<System>()};
    auto value{config()};
    bool rejected{};
    try {
        Sc233hgsCameraDevice device{value, system};
        device.prepare();
    } catch (const std::exception &error) {
        rejected = std::string{error.what()}.find("NV12") != std::string::npos;
    }
    require(rejected, "SC233HGS accepted a non-NV12 format");
    system->clean();
}

void reportsCaptureOpenFailure() {
    auto system{std::make_shared<System>()};
    system->failure = "open_capture";
    auto value{config()};
    bool rejected{};
    try {
        Sc233hgsCameraDevice device{value, system};
        device.prepare();
    } catch (const std::exception &error) {
        const std::string reason{error.what()};
        rejected = reason.find("/dev/video31") != std::string::npos
            && reason.find("open capture") != std::string::npos;
    }
    require(rejected, "SC233HGS open failure omitted device path or operation");
    system->clean();
}

void copiesYPlaneAndRestarts() {
    auto system{std::make_shared<System>()};
    system->nv12 = true;
    system->width = 8;
    system->height = 3;
    system->stride = 16;
    auto value{config()};
    value.width = 8;
    value.height = 3;
    Sc233hgsCameraDevice device{value, system};
    device.prepare();
    device.start();
    for (int i{}; i < 7; ++i) { require(!device.next(1, true).frame, "Warmup copied pixels"); }
    const auto sample{device.next(1, true)};
    require(sample.frame && sample.pixels_equal, "SC233HGS Y plane copy failed");
    require(sample.frame->mapRead()->rowStrideBytes() == 8, "SC233HGS output retained stride padding");
    require(device.finish().empty(), "SC233HGS finish failed");
    system->clean();
}

void preservesFrameAfterDeviceLifetime() {
    FrameRef held{};
    std::unique_ptr<FrameReadView> view{};
    auto system{std::make_shared<System>()};
    auto value{config()};
    value.width = 8;
    value.height = 3;
    system->nv12 = true;
    system->width = value.width;
    system->height = value.height;
    system->stride = 16;
    {
        Sc233hgsCameraDevice device{value, system};
        device.prepare();
        device.start();
        for (int i{}; i < 7; ++i) { device.next(1); }
        const auto sample{device.next(1)};
        require(static_cast<bool>(sample.frame), "SC233HGS did not produce a frame for lifetime test");
        held = sample.frame;
        view = held->mapRead();
        require(device.finish().empty(), "SC233HGS lifetime test cleanup failed");
    }
    system->pixels.clear();
    system->clean();
    held.reset();
    for (int row{}; row < value.height; ++row) {
        for (int column{}; column < value.width; ++column) {
            require(view->mat().at<unsigned char>(row, column) == (row + column + 7) % 251,
                    "SC233HGS frame depends on driver buffer lifetime");
        }
    }
}

void rejectsRepeatedAndBackwardsTimestamps() {
    for (const auto [name, seconds] : {std::pair<const char *, std::int64_t>{"repeated", 10},
                                       {"backwards", 9}}) {
        auto system{std::make_shared<System>()};
        system->nv12 = true;
        system->width = 8;
        system->height = 3;
        system->stride = 16;
        system->mutate = [seconds](v4l2_buffer &value) {
            if (value.sequence >= 8) {
                value.timestamp.tv_sec = seconds;
                value.timestamp.tv_usec = seconds == 10 ? 112000 : 0;
            }
        };
        auto value{config()};
        value.width = system->width;
        value.height = system->height;
        Sc233hgsCameraDevice device{value, system};
        device.prepare();
        device.start();
        for (int i{}; i < 7; ++i) { device.next(1); }
        require(static_cast<bool>(device.next(1).frame),
                "SC233HGS timestamp test did not produce first frame");
        bool rejected{};
        try {
            device.next(1);
        } catch (const std::exception &error) {
            rejected = std::string{error.what()}.find("repeated or backwards") != std::string::npos;
        }
        require(rejected, (std::string{"SC233HGS "} + name + " timestamp was accepted").c_str());
        require(device.finish().empty(), "SC233HGS timestamp test cleanup failed");
        system->clean();
    }
}

void queueDropsNewFramesAndCanRestart() {
    auto system{std::make_shared<System>()};
    system->nv12 = true;
    system->width = 8;
    system->height = 3;
    system->stride = 16;
    system->frame_limit = 100;
    auto value{config()};
    value.width = 8;
    value.height = 3;
    Sc233hgsCameraFrameSource source{value, dependencies(system)};
    for (int round{}; round < 2; ++round) {
        require(source.start(), "SC233HGS start failed");
        await([&] { return source.status().read == 93; });
        const auto status{source.status()};
        require(status.dropped > 0 && status.enqueued + status.dropped == 93,
                "SC233HGS queue did not drop new frames");
        FrameRef frame{};
        require(source.tryFrame(frame) && frame, "SC233HGS queue lost oldest frame");
        require(source.stop(), "SC233HGS stop failed");
        system->clean();
        require(!source.tryFrame(frame) && !frame, "SC233HGS retained old frame after stop");
    }
}

void warmupLifecycle() {
    auto system{std::make_shared<System>()};
    system->nv12 = true;
    system->width = 8;
    system->height = 3;
    system->stride = 16;
    auto value{config()};
    value.width = 8;
    value.height = 3;
    system->frame_limit = 11;
    system->mutate = [](v4l2_buffer &v) {
        const int times[]{100, 100, 117, 117, 134, 151, 168, 185, 202, 219, 236};
        v.timestamp.tv_sec = 0;
        v.timestamp.tv_usec = times[v.sequence];
    };
    Sc233hgsCameraFrameSource source{value, dependencies(system)};
    require(source.start(), "Warmup start failed");
    await([&] { return source.status().read == 1; });
    require(source.stop(), "Warmup stop failed");
    auto status{source.status()};
    require(status.camera.dequeued_total == 11 && status.camera.warmup_discarded == 10
        && status.camera.warmup_repeats == 2 && status.camera.warmup_examined == 11
        && status.camera.first_sequence == 10 && status.camera.resources_reclaimed
        && status.camera.terminal_discarded == 0 && status.camera.evidence.size() == 11,
        "Measured warmup accounting failed");
    system->clean();
    system->frame_limit = 3;
    for (int round{}; round < 5; ++round) {
        require(source.start(), "Warmup restart failed");
        await([&] { return source.status().camera.warmup_discarded == 3; });
        FrameRef frame{};
        require(!source.tryFrame(frame), "Warmup leaked frame");
        const auto begin{std::chrono::steady_clock::now()};
        require(source.stop(), "Warmup cancellation failed");
        require(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds{250}, "Slow warmup OFF");
        status = source.status();
        require(status.read == 0 && !status.camera.first_frame_known && status.camera.phase == "cancelled"
            && status.camera.dequeued_total == status.camera.warmup_discarded + status.camera.terminal_discarded,
            "Cancelled warmup accounting failed");
        system->clean();
    }
    system->frame_limit = 120;
    system->mutate = [](v4l2_buffer &v) { v.timestamp.tv_sec = 10; v.timestamp.tv_usec = 0; };
    require(source.start(), "Repeated warmup start failed");
    await([&] { return !source.isRunning(); });
    require(!source.stop(), "Repeated warmup did not fail");
    status = source.status();
    require(status.camera.dequeued_total == 120 && status.camera.warmup_discarded == 120
        && status.camera.evidence.size() == 120 && status.camera.terminal_discarded == 0
        && status.reason.find("sample limit") != std::string::npos, "Sample boundary accounting failed");
    system->clean();
    system->frame_limit = 8;
    system->mutate = {};
    require(source.start(), "Failed warmup could not restart");
    await([&] { return source.status().read == 1; });
    require(source.stop(), "Recovery stop failed");
    system->clean();
}

void warmupFailurePaths() {
    for (const std::string failure : {"return", "sequence", "backwards", "map"}) {
        auto system{std::make_shared<System>()};
        system->nv12 = true;
        system->width = 8;
        system->height = 3;
        system->stride = 16;
        auto value{config()};
        value.width = 8;
        value.height = 3;
        if (failure == "return") { system->failure = "return"; }
        if (failure == "map") { system->failure = "map"; system->occurrence = 3; }
        if (failure == "sequence") { system->sequence_step = 0; }
        if (failure == "backwards") {
            system->mutate = [](v4l2_buffer &v) { if (v.sequence == 1) { v.timestamp.tv_sec = 9; } };
        }
        Sc233hgsCameraFrameSource source{value, dependencies(system)};
        source.start();
        await([&] { return !source.isRunning(); });
        require(!source.stop(), "Warmup fatal error accepted");
        const auto status{source.status()};
        require(status.read == 0 && status.camera.dequeued_total
            == status.camera.warmup_discarded + status.camera.terminal_discarded,
            "Fatal error accounting failed");
        if (failure == "return") {
            require(status.camera.terminal_discarded == 1 && status.camera.warmup_discarded == 0
                && system->counts["return"] == 1, "Failed QBUF counted as discard or retried");
        }
        system->clean();
    }
}

class InterruptedSystem final : public System {
public:
    bool interrupt_poll{};
    bool interrupt_dequeue{};
    std::function<void()> clock_hook{};
    int poll(pollfd *fds, nfds_t count, int timeout) override {
        if (produced > 0) {
            monotonic += 300000;
            if (interrupt_poll) { errno = EINTR; return -1; }
        }
        return System::poll(fds, count, timeout);
    }
    int ioctl(int fd, unsigned long request, void *argument) override {
        if (request == VIDIOC_DQBUF && produced > 0 && interrupt_dequeue) {
            errno = EAGAIN;
            return -1;
        }
        return System::ioctl(fd, request, argument);
    }
    std::int64_t clockUs(clockid_t clock) override {
        if (clock_hook && clock == CLOCK_MONOTONIC) {
            auto hook{std::move(clock_hook)};
            hook();
        }
        return System::clockUs(clock);
    }
};
void warmupDeadlineAndSequence() {
    auto value{config()};
    value.width = 8;
    value.height = 3;
    for (int mode{}; mode < 4; ++mode) {
        auto system{std::make_shared<InterruptedSystem>()};
        system->nv12 = true; system->width = 8; system->height = 3; system->stride = 16;
        Sc233hgsCameraDevice device{value, system};
        device.prepare(); device.start();
        require(device.next(1).result == CameraSampleResult::WarmupDiscarded, "No initial observation");
        system->interrupt_poll = mode == 0;
        system->interrupt_dequeue = mode == 1;
        if (mode < 2) {
            bool failed{};
            for (int i{}; i < 10 && !failed; ++i) {
                try { device.next(100); }
                catch (const std::exception &e) {
                    failed = std::string{e.what()}.find("time limit") != std::string::npos;
                }
            }
            require(failed && system->produced == 1, "Interrupted wait renewed deadline");
        } else if (mode == 2) {
            system->clock_hook = [&] { system->monotonic += 2000000; device.wake(); };
            require(!device.next(1).frame, "OFF at deadline produced frame");
            require(device.captureStatus().reason.empty(), "OFF at deadline reported timeout");
        } else {
            system->monotonic += 2000000;
            rejects([&] { device.next(1); }, "time limit");
            require(system->produced == 1, "Read after deadline");
        }
        require(device.finish().empty(), "Interrupted warmup cleanup failed");
        system->clean();
    }
    for (bool gap : {false, true}) {
        auto system{std::make_shared<System>()};
        system->nv12 = true; system->width = 8; system->height = 3; system->stride = 16;
        system->mutate = [gap](v4l2_buffer &v) {
            v.sequence = gap ? v.sequence * 2 : v.sequence + 0xfffffffcU;
        };
        Sc233hgsCameraDevice device{value, system};
        device.prepare(); device.start();
        for (int i{}; i < 7; ++i) { require(!device.next(1).frame, "Sequence observation leaked frame"); }
        require(static_cast<bool>(device.next(1).frame), "Gap or wrap reset warmup");
        require(device.sequenceGaps() == (gap ? 7 : 0), "Wrong gap or wrap accounting");
        device.finish(); system->clean();
    }
}

void factoryDispatchesExplicitModel() {
    auto system{std::make_shared<System>()};
    system->nv12 = true;
    FactoryOptions options{};
    options.enable_camera = true;
    options.camera.system = system;
    auto factory{createFactory({}, options)};
    auto value{config()};
    auto created{factory->createFrameSource(value)};
    require(created && dynamic_cast<Sc233hgsCameraFrameSource *>(created.value.get()),
            "Factory did not create SC233HGS source");
    require(system->counts["control_query"] == 0 && system->counts["entity"] == 0,
            "SC233HGS initialized AR0234 controls");
    value.camera_sensor_model = static_cast<CameraSensorModel>(99);
    auto rejected{factory->createFrameSource(value)};
    require(!rejected && rejected.error.reason.find("ar0234") != std::string::npos
            && rejected.error.reason.find("sc233hgs") != std::string::npos
            && rejected.error.reason.find("requested enum value=99") != std::string::npos,
            "Factory accepted unknown sensor model");
    created.value.reset();
    system->clean();
}
} // namespace

int main() {
    try {
        rejectsSoftwareTrigger();
        rejectsNonZeroContinuousRate();
        rejectsScaling();
        rejectsRecording();
        rejectsFormatMismatch();
        reportsCaptureOpenFailure();
        copiesYPlaneAndRestarts();
        preservesFrameAfterDeviceLifetime();
        rejectsRepeatedAndBackwardsTimestamps();
        queueDropsNewFramesAndCanRestart();
        warmupLifecycle();
        warmupFailurePaths();
        warmupDeadlineAndSequence();
        factoryDispatchesExplicitModel();
        std::cout << "SC233HGS configuration tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
