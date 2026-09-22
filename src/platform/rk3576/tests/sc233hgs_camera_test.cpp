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
            require(view->mat().at<unsigned char>(row, column) == (row + column) % 251,
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
            if (value.sequence > 0) {
                value.timestamp.tv_sec = seconds;
                value.timestamp.tv_usec = 0;
            }
        };
        auto value{config()};
        value.width = system->width;
        value.height = system->height;
        Sc233hgsCameraDevice device{value, system};
        device.prepare();
        device.start();
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
        await([&] { return source.status().read == 100; });
        const auto status{source.status()};
        require(status.dropped > 0 && status.enqueued + status.dropped == 100,
                "SC233HGS queue did not drop new frames");
        FrameRef frame{};
        require(source.tryFrame(frame) && frame, "SC233HGS queue lost oldest frame");
        require(source.stop(), "SC233HGS stop failed");
        system->clean();
        require(!source.tryFrame(frame) && !frame, "SC233HGS retained old frame after stop");
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
        factoryDispatchesExplicitModel();
        std::cout << "SC233HGS configuration tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
