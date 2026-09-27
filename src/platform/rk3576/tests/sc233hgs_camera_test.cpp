#include "sc233hgs_camera_device.h"
#include "sc233hgs_camera_frame_source.h"
#include "../platform_factory.h"
#include "camera_test_support.h"
#include "../recording/nv12_copy.h"
#include "../recording/shared_slots.h"

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

void recordingConfigFailureKeepsCameraRunning() {
    auto system{std::make_shared<System>()};
    system->nv12 = true;
    auto value{config()};
    value.camera_recording.enabled = true;
    value.camera_recording.bitrate_bps = 0;
    Sc233hgsCameraFrameSource source{value, dependencies(system)};
    require(source.start(), "invalid recording config stopped healthy SC233HGS camera");
    const auto status{source.status()};
    require(status.recording.phase == "failed" && !status.recording.reason.empty()
        && status.state == FrameSourceState::Running,
        "SC233HGS recording config failure was not isolated");
    source.stop();
    system->clean();
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

class RecordingInput final : public CameraNv12Sink {
public:
    std::vector<std::uint8_t> pixels{std::vector<std::uint8_t>(48)};
    unsigned stages{};
    unsigned publications{};
    unsigned cancellations{};
    bool staged{};
    bool reject{};
    CameraNv12View identity{};
    void stage(const CameraNv12View &view) noexcept override {
        ++stages;
        identity = view;
        identity.pixels = nullptr;
        staged = !reject && copyNv12(view, pixels.data(), pixels.size());
    }
    void publish() noexcept override { if (staged) { ++publications; } staged = false; }
    void cancel() noexcept override { if (staged) { ++cancellations; } staged = false; }
};
class FinalCheckSystem final : public System {
public:
    std::function<void()> after_return{};
    int ioctl(int fd, unsigned long request, void *argument) override {
        const int result{System::ioctl(fd, request, argument)};
        if (request == VIDIOC_QBUF && streaming && produced == 8 && after_return) { after_return(); }
        return result;
    }
};
void sharedRecordingSlots() {
    RecordingSlots slots{8, 4, 23};
    std::vector<std::uint8_t> source(96, 42);
    CameraNv12View view{}; view.pixels = source.data(); view.available_bytes = source.size();
    view.format.width = 8; view.format.height = 4; view.format.stride = 16;
    view.format.colorspace = V4L2_COLORSPACE_REC709;
    view.format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
    view.format.ycbcr_encoding = V4L2_YCBCR_ENC_709;
    view.format.transfer_function = V4L2_XFER_FUNC_709;
    slots.stage(view);
    require(slots.acquire() == -1, "Unpublished slot visible");
    slots.cancel();
    require(slots.acquire() == -1 && slots.statistics().cancelled == 1, "Cancelled slot visible");
    for (unsigned index{}; index < 6; ++index) {
        view.sequence = index; view.timestamp_us = index + 1;
        slots.stage(view); slots.publish();
    }
    require(slots.statistics().published == 4 && slots.statistics().full == 2
        && slots.statistics().ranges[0].first == 4 && slots.statistics().ranges[0].last == 5,
        "Full slots did not preserve originals and record drops");
    const int oldest{slots.acquire()};
    require(oldest >= 0 && slots.identity(oldest).sequence == 0 && slots.identity(oldest).round == 23,
        "Wrong round or FIFO order");
    const auto &stored_format{slots.identity(oldest).format};
    require(stored_format.colorspace == view.format.colorspace
        && stored_format.quantization == view.format.quantization
        && stored_format.ycbcr_encoding == view.format.ycbcr_encoding
        && stored_format.transfer_function == view.format.transfer_function,
        "Recording shared slot changed color metadata");
    slots.stage(view); slots.publish();
    require(slots.statistics().full == 3 && slots.pixels(oldest)[0] == 42, "Reading slot reused");
    slots.release(oldest);
    view.timestamp_us = 10; view.sequence = 10;
    slots.stage(view); slots.publish();
    const int next{slots.acquire()};
    require(next >= 0 && slots.identity(next).sequence == 1, "Reused slot overtook old frame");
    slots.release(next);
    RecordingSlots next_round{8, 4, 24};
    require(next_round.acquire() == -1, "New round contains old frame");
    RecordingSlots attached{next_round.fd(), 8, 4, 24};
    next_round.stage(view); next_round.publish();
    const int shared{attached.acquire()};
    require(shared >= 0 && attached.identity(shared).round == 24 && attached.pixels(shared)[0] == 42,
        "Attached mapping lost shared contents");
    attached.release(shared);
    CameraSample disposition{};
    disposition.result = CameraSampleResult::FrameReady;
    disposition.sequence = view.sequence;
    next_round.disposition(disposition, 2, true);
    RecordingSlots::Event event{};
    require(attached.nextEvent(event) && event.kind == 6 && event.count == 2
        && event.first_sequence == view.sequence - 2 && event.last_sequence == view.sequence - 1,
        "Driver gap range missing");
    require(attached.nextEvent(event) && event.kind == 7 && event.first_sequence == view.sequence,
        "Tracking queue drop range missing");
    next_round.stage(view);
    next_round.header().stop_accepting.store(true);
    next_round.publish();
    require(attached.acquire() == -1 && next_round.header().accepted.load() == 1
        && next_round.header().dropped.load() == 1, "Stop between stage and publish accepted a new frame");
    rejects([] { RecordingSlots too_large{8192, 8192, 1}; }, "64 MiB");
}

void recordingFinalConfirmation() {
    for (int mode{}; mode < 5; ++mode) {
        auto system{std::make_shared<FinalCheckSystem>()};
        system->nv12 = true; system->width = 8; system->height = 4; system->stride = 16;
        auto value{config()}; value.width = 8; value.height = 4;
        Sc233hgsCameraDevice device{value, system};
        RecordingInput sink{};
        device.prepare(); device.start();
        for (int index{}; index < 7; ++index) { device.next(1, true, &sink); }
        require(sink.stages == 0 && sink.publications == 0, "Observation copied NV12");
        if (mode == 1) { system->after_return = [&] { system->monotonic += 2000000; }; }
        if (mode == 2) { system->after_return = [&] { device.wake(); }; }
        if (mode == 3) { system->failure = "return"; system->occurrence = 8; }
        if (mode == 4) { sink.reject = true; }
        if (mode == 1 || mode == 3) {
            rejects([&] { device.next(1, true, &sink); }, mode == 1 ? "time limit" : "QBUF");
        } else {
            const auto sample{device.next(1, true, &sink)};
            require(static_cast<bool>(sample.frame) == (mode != 2), "Recording changed camera result");
            if (sample.frame) { require(sample.pixels_equal, "Recording changed tracking pixels"); }
        }
        require(sink.stages == 1 && sink.publications == (mode == 0 ? 1U : 0U),
                "Unconfirmed frame published");
        if (mode > 0 && mode < 4) { require(sink.cancellations == 1, "Pending frame not cancelled"); }
        if (mode == 0) {
            for (int row{}; row < 4; ++row) {
                for (int col{}; col < 8; ++col) {
                    require(sink.pixels[row * 8 + col] == (row + col + 7) % 251, "Y row padding copied");
                }
            }
            for (int index{32}; index < 48; ++index) { require(sink.pixels[index] == 255, "UV offset wrong"); }
            require(sink.identity.sequence == 7, "Wrong recording identity");
        }
        device.finish(); system->clean();
    }
    {
        auto system{std::make_shared<System>()};
        system->nv12 = true; system->width = 8; system->height = 4; system->stride = 16;
        system->mutate = [](v4l2_buffer &buffer) {
            const int times[]{100, 100, 117, 117, 134, 151, 168, 185, 202, 219, 236};
            buffer.timestamp.tv_sec = 0; buffer.timestamp.tv_usec = times[buffer.sequence];
        };
        auto value{config()}; value.width = 8; value.height = 4;
        Sc233hgsCameraDevice device{value, system};
        RecordingInput sink{};
        device.prepare(); device.start();
        for (int index{}; index < 10; ++index) { device.next(1, true, &sink); }
        require(sink.stages == 0, "Repeated observation copied pixels");
        const auto sample{device.next(1, true, &sink)};
        require(sample.frame && sink.publications == 1 && sink.identity.sequence == 10,
                "Recording started before final confirmation after repeated timestamps");
        require(sink.identity.timestamp_us == static_cast<std::int64_t>(sample.frame->timestampUs()),
                "Tracking and recording time identity differs");
        device.finish(); system->clean();
    }
    std::vector<std::uint8_t> source(96, 42), target(48, 17);
    CameraNv12View view{}; view.pixels = source.data(); view.available_bytes = 87;
    view.format.width = 8; view.format.height = 4; view.format.stride = 16;
    require(!copyNv12(view, target.data(), target.size()) && target.front() == 17,
            "Short UV buffer accepted or modified output");
    view.available_bytes = 88;
    require(copyNv12(view, target.data(), target.size()) && target.back() == 42,
            "Valid padded NV12 rejected");
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
        recordingConfigFailureKeepsCameraRunning();
        rejectsFormatMismatch();
        reportsCaptureOpenFailure();
        copiesYPlaneAndRestarts();
        preservesFrameAfterDeviceLifetime();
        rejectsRepeatedAndBackwardsTimestamps();
        queueDropsNewFramesAndCanRestart();
        warmupLifecycle();
        warmupFailurePaths();
        warmupDeadlineAndSequence();
        sharedRecordingSlots();
        recordingFinalConfirmation();
        factoryDispatchesExplicitModel();
        std::cout << "SC233HGS configuration tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
