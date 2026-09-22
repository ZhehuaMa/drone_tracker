#include "ar0234_camera_device.h"

#include <fcntl.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace rk3576 {
namespace {
constexpr std::uint32_t TRIGGER_MODE{0x00981901};
constexpr std::uint32_t TRIGGER_SOURCE{0x00981902};
constexpr std::uint32_t SOFTWARE_TRIGGER{0x00981903};
constexpr std::uint32_t FRAME_RATE{0x00981904};
constexpr std::int64_t CONTROL_VERIFY_TIMEOUT_US{100000};
constexpr std::int64_t CONTROL_VERIFY_INTERVAL_US{5000};
std::string systemError(const std::string &operation) {
    return operation + ": " + std::strerror(errno);
}
} // namespace
Ar0234CameraDevice::Ar0234CameraDevice(FrameSourceConfig config, std::shared_ptr<CameraSystem> system)
    : Rk3576CameraCaptureBase{std::move(config), std::move(system)} {}
Ar0234CameraDevice::~Ar0234CameraDevice() {
    try { finish(); } catch (...) {}
    if (sensor_fd_ >= 0) { system().close(sensor_fd_); }
}
std::uint32_t Ar0234CameraDevice::pixelFormat() const { return V4L2_PIX_FMT_GREY; }
const char *Ar0234CameraDevice::pixelFormatName() const { return "GREY"; }
bool Ar0234CameraDevice::hasOpenSensorState() const { return sensor_fd_ >= 0; }
void Ar0234CameraDevice::configureSensor() {
    auto subdev{system().execute({"media-ctl", "-d", config().camera_media_device,
                                 "-e", config().camera_sensor_entity}, 3000)};
    while (!subdev.empty() && (subdev.back() == '\n' || subdev.back() == '\r')) { subdev.pop_back(); }
    if (subdev.rfind("/dev/", 0) != 0 || subdev.find_first_of("\n\r") != std::string::npos) {
        throw std::runtime_error("media-ctl returned invalid sensor device: " + subdev);
    }
    sensor_path_ = subdev;
    sensor_fd_ = system().open(subdev, O_RDWR | O_CLOEXEC);
    check(sensor_fd_, "open sensor " + subdev);
    snapshotControls();
    const auto expression{"\"" + config().camera_sensor_entity + "\":0[fmt:Y8_1X8/"
        + std::to_string(config().width) + "x" + std::to_string(config().height)
        + "@1/" + std::to_string(config().fps) + " field:none]"};
    system().execute({"media-ctl", "-d", config().camera_media_device, "--set-v4l2", expression}, 3000);
    setControl(TRIGGER_MODE, config().trigger_mode ? 1 : 0, "trigger_mode");
    setControl(TRIGGER_SOURCE, config().trigger_mode ? 0 : 1, "trigger_src");
    setControl(FRAME_RATE, config().fps, "frame_rate");
    {
        v4l2_subdev_format format{};
        format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        format.pad = 0;
        check(system().ioctl(sensor_fd_, VIDIOC_SUBDEV_G_FMT, &format), "sensor G_FMT");
        v4l2_subdev_frame_interval interval{};
        interval.pad = 0;
        check(system().ioctl(sensor_fd_, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &interval), "sensor G_FRAME_INTERVAL");
        if (format.format.width != static_cast<unsigned>(config().width)
            || format.format.height != static_cast<unsigned>(config().height)
            || format.format.code != MEDIA_BUS_FMT_Y8_1X8 || format.format.field != V4L2_FIELD_NONE
            || interval.interval.numerator == 0
            || static_cast<std::uint64_t>(interval.interval.numerator) * config().fps != interval.interval.denominator) {
            throw std::runtime_error("Sensor negotiation mismatch: requested Y8_1X8 "
                + std::to_string(config().width) + "x" + std::to_string(config().height) + "@1/"
                + std::to_string(config().fps) + ", actual code=" + std::to_string(format.format.code)
                + " " + std::to_string(format.format.width) + "x" + std::to_string(format.format.height)
                + " field=" + std::to_string(format.format.field) + " interval="
                + std::to_string(interval.interval.numerator) + "/" + std::to_string(interval.interval.denominator));
        }
    }
}
void Ar0234CameraDevice::snapshotControls() {
    controls_.clear();
    for (const auto &entry : std::vector<SavedControl>{{TRIGGER_MODE, 0, "trigger_mode", false},
             {TRIGGER_SOURCE, 0, "trigger_src", false}, {FRAME_RATE, 0, "frame_rate", false}}) {
        v4l2_queryctrl query{};
        query.id = entry.id;
        check(system().ioctl(sensor_fd_, VIDIOC_QUERYCTRL, &query), sensor_path_ + " query " + entry.name);
        const int desired{entry.id == FRAME_RATE ? config().fps
            : (entry.id == TRIGGER_MODE ? (config().trigger_mode ? 1 : 0) : (config().trigger_mode ? 0 : 1))};
        if (query.type != V4L2_CTRL_TYPE_INTEGER || std::string{reinterpret_cast<char *>(query.name), strnlen(reinterpret_cast<char *>(query.name), sizeof(query.name))} != entry.name
            || (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY))
            || query.step <= 0 || desired < query.minimum || desired > query.maximum
            || (desired - query.minimum) % query.step != 0) {
            throw std::runtime_error(sensor_path_ + " incompatible control " + entry.name);
        }
        v4l2_control value{};
        value.id = entry.id;
        check(system().ioctl(sensor_fd_, VIDIOC_G_CTRL, &value), sensor_path_ + " read " + entry.name);
        controls_.push_back({entry.id, value.value, entry.name, false});
    }
    if (config().trigger_mode) {
        v4l2_queryctrl query{};
        query.id = SOFTWARE_TRIGGER;
        check(system().ioctl(sensor_fd_, VIDIOC_QUERYCTRL, &query), sensor_path_ + " query soft_trgone");
        if (query.type != V4L2_CTRL_TYPE_BUTTON || std::string{reinterpret_cast<char *>(query.name), strnlen(reinterpret_cast<char *>(query.name), sizeof(query.name))} != "soft_trgone"
            || (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY))) {
            throw std::runtime_error(sensor_path_ + " incompatible soft_trgone control");
        }
    }
    // media-ctl 设置帧间隔也可能修改 frame_rate；必须在它运行前保存并标记恢复责任。
    for (auto &entry : controls_) { entry.changed = true; }
}
void Ar0234CameraDevice::setControl(std::uint32_t id, int value, const std::string &name) {
    std::lock_guard<std::mutex> lock{control_mutex_};
    writeAndVerifyControl(id, value, sensor_path_ + " set " + name);
}
void Ar0234CameraDevice::writeAndVerifyControl(std::uint32_t id, int value, const std::string &operation) {
    v4l2_control control{};
    control.id = id;
    control.value = value;
    check(system().ioctl(sensor_fd_, VIDIOC_S_CTRL, &control), operation);
    const auto begin{system().clockUs(CLOCK_MONOTONIC)};
    for (;;) {
        check(system().ioctl(sensor_fd_, VIDIOC_G_CTRL, &control), operation + " readback");
        const auto elapsed{system().clockUs(CLOCK_MONOTONIC) - begin};
        if (control.value == value && elapsed <= CONTROL_VERIFY_TIMEOUT_US) { return; }
        if (elapsed >= CONTROL_VERIFY_TIMEOUT_US) {
            throw std::runtime_error(operation + " readback timeout expected=" + std::to_string(value)
                + " actual=" + std::to_string(control.value) + " elapsed_us=" + std::to_string(elapsed)
                + " timeout_us=" + std::to_string(CONTROL_VERIFY_TIMEOUT_US));
        }
        system().sleepUs(std::min(CONTROL_VERIFY_INTERVAL_US, CONTROL_VERIFY_TIMEOUT_US - elapsed));
    }
}
void Ar0234CameraDevice::trigger() {
    std::lock_guard<std::mutex> lock{control_mutex_};
    if (!streaming() || !config().trigger_mode || sensor_fd_ < 0) {
        throw std::logic_error(config().path + " software trigger requires streaming software mode");
    }
    v4l2_control control{};
    control.id = SOFTWARE_TRIGGER;
    control.value = 1;
    check(system().ioctl(sensor_fd_, VIDIOC_S_CTRL, &control), sensor_path_ + " soft_trgone");
}
std::string Ar0234CameraDevice::restoreControls() {
    std::lock_guard<std::mutex> lock{control_mutex_};
    std::string errors{};
    for (auto it{controls_.rbegin()}; it != controls_.rend(); ++it) {
        if (!it->changed) { continue; }
        try {
            writeAndVerifyControl(it->id, it->value, sensor_path_ + " restore " + it->name);
            it->changed = false;
        } catch (const std::exception &error) {
            errors += "; " + std::string{error.what()};
        }
    }
    return errors;
}
std::vector<std::string> Ar0234CameraDevice::unrestoredControls() const {
    std::lock_guard<std::mutex> lock{control_mutex_};
    std::vector<std::string> result{};
    for (const auto &entry : controls_) {
        if (entry.changed) { result.push_back(entry.name); }
    }
    return result;
}
std::string Ar0234CameraDevice::finishSensor() {
    std::string errors{};
    auto record{[&](int result, const char *operation) {
        if (result < 0) { errors += "; cleanup " + systemError(operation); }
    }};
    if (sensor_fd_ < 0 && !unrestoredControls().empty()) {
        sensor_fd_ = system().open(sensor_path_, O_RDWR | O_CLOEXEC);
        record(sensor_fd_, "reopen sensor for control restoration");
    }
    if (sensor_fd_ >= 0) {
        errors += restoreControls();
        // 本次尝试结束后释放句柄，未恢复项仍保留；重复 finish 可重新打开并重试。
        record(system().close(sensor_fd_), "close sensor");
        sensor_fd_ = -1;
    }
    return errors;
}
} // namespace rk3576
