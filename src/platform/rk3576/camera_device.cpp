#include "camera_device.h"

#include <fcntl.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <spawn.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "host_frame.h"

extern char **environ;
namespace rk3576 {
namespace {
std::string systemError(const std::string &operation) {
    return operation + ": " + std::strerror(errno);
}
constexpr std::uint32_t TRIGGER_MODE{0x00981901};
constexpr std::uint32_t TRIGGER_SOURCE{0x00981902};
constexpr std::uint32_t SOFTWARE_TRIGGER{0x00981903};
constexpr std::uint32_t FRAME_RATE{0x00981904};
constexpr std::int64_t CONTROL_VERIFY_TIMEOUT_US{100000};
constexpr std::int64_t CONTROL_VERIFY_INTERVAL_US{5000};
struct PlaneBuffer {
    v4l2_plane plane{};
    v4l2_buffer buffer{};
    explicit PlaneBuffer(std::uint32_t index = 0) {
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.length = 1;
        buffer.index = index;
        buffer.m.planes = &plane;
    }
};
std::string metadata(const PlaneBuffer &value) {
    std::ostringstream out{};
    out << " index=" << value.buffer.index << " planes=" << value.buffer.length
        << " offset=" << value.plane.data_offset << " bytesused=" << value.plane.bytesused
        << " length=" << value.plane.length << " flags=" << value.buffer.flags
        << " sequence=" << value.buffer.sequence << " timestamp=" << value.buffer.timestamp.tv_sec
        << ':' << value.buffer.timestamp.tv_usec;
    return out.str();
}
std::uint64_t requiredBytes(const CameraFormat &format) {
    return static_cast<std::uint64_t>(format.height - 1) * format.stride + format.width;
}
} // namespace
int CameraSystem::open(const std::string &path, int flags) { return ::open(path.c_str(), flags); }
int CameraSystem::close(int fd) { return ::close(fd); }
int CameraSystem::ioctl(int fd, unsigned long request, void *argument) {
    int result{};
    do { result = ::ioctl(fd, request, argument); } while (result < 0 && errno == EINTR);
    return result;
}
void *CameraSystem::map(int fd, std::size_t length, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        errno = EOVERFLOW;
        return MAP_FAILED;
    }
    return ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, static_cast<off_t>(offset));
}
int CameraSystem::unmap(void *address, std::size_t length) { return ::munmap(address, length); }
int CameraSystem::poll(pollfd *fds, nfds_t count, int timeout_ms) { return ::poll(fds, count, timeout_ms); }
int CameraSystem::event() { return ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); }
int CameraSystem::signal(int fd) {
    const std::uint64_t value{1};
    ssize_t result{};
    do { result = ::write(fd, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    // 已有停止事件待读时，饱和也表示能唤醒。
    return result == sizeof(value) || (result < 0 && errno == EAGAIN) ? 0 : -1;
}
std::int64_t CameraSystem::clockUs(clockid_t clock) {
    timespec value{};
    if (::clock_gettime(clock, &value) < 0) {
        throw std::runtime_error(systemError("clock_gettime"));
    }
    return cameraTimevalUs(value.tv_sec, value.tv_nsec / 1000);
}
void CameraSystem::sleepUs(std::int64_t duration_us) {
    std::this_thread::sleep_for(std::chrono::microseconds{duration_us});
}
std::string CameraSystem::execute(const std::vector<std::string> &arguments, int timeout_ms) {
    if (arguments.empty() || timeout_ms <= 0) {
        throw std::invalid_argument("Invalid camera configuration command");
    }
    // 分别捕获 stdout/stderr；不经 shell，参数中的空格不会改变参数边界。
    std::array<int, 2> output{-1, -1};
    std::array<int, 2> error{-1, -1};
    pid_t child{-1};
    posix_spawn_file_actions_t actions{};
    bool actions_ready{false};
    auto cleanup{[&] {
        if (child > 0) {
            ::kill(child, SIGKILL);
            while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        }
        if (actions_ready) { ::posix_spawn_file_actions_destroy(&actions); }
        for (int fd : {output[0], output[1], error[0], error[1]}) {
            if (fd >= 0) { ::close(fd); }
        }
    }};
    try {
        if (::pipe2(output.data(), O_CLOEXEC) < 0 || ::pipe2(error.data(), O_CLOEXEC) < 0) {
            throw std::runtime_error(systemError("media-ctl pipe"));
        }
        auto spawnCheck{[](int code) {
            if (code != 0) { throw std::runtime_error("media-ctl spawn: " + std::string{std::strerror(code)}); }
        }};
        spawnCheck(::posix_spawn_file_actions_init(&actions));
        actions_ready = true;
        spawnCheck(::posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO));
        spawnCheck(::posix_spawn_file_actions_adddup2(&actions, error[1], STDERR_FILENO));
        std::vector<char *> argv{};
        for (const auto &arg : arguments) { argv.push_back(const_cast<char *>(arg.c_str())); }
        argv.push_back(nullptr);
        const int spawned{::posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ)};
        if (spawned != 0) {
            child = -1;
            throw std::runtime_error(std::string{spawned == ENOENT ? "media-ctl tool missing: " : "media-ctl spawn: "}
                                     + std::strerror(spawned));
        }
        ::close(output[1]); output[1] = -1;
        ::close(error[1]); error[1] = -1;
        if (::fcntl(output[0], F_SETFL, O_NONBLOCK) < 0 || ::fcntl(error[0], F_SETFL, O_NONBLOCK) < 0) {
            throw std::runtime_error(systemError("media-ctl pipe nonblocking"));
        }
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms}};
        std::string stdout_text{};
        std::string stderr_text{};
        auto drain{[](int fd, std::string &text) {
            std::array<char, 4096> bytes{};
            for (;;) {
                const auto count{::read(fd, bytes.data(), bytes.size())};
                if (count > 0) {
                    if (text.size() + static_cast<std::size_t>(count) > 65536) {
                        throw std::runtime_error("media-ctl output exceeds 64 KiB");
                    }
                    text.append(bytes.data(), static_cast<std::size_t>(count));
                } else if (count < 0 && errno == EINTR) {
                    continue;
                } else if (count == 0 || errno == EAGAIN) {
                    break;
                } else {
                    throw std::runtime_error(systemError("media-ctl output read"));
                }
            }
        }};
        int status{};
        for (;;) {
            drain(output[0], stdout_text);
            drain(error[0], stderr_text);
            const auto waited{::waitpid(child, &status, WNOHANG)};
            if (waited == child) { child = -1; break; }
            if (waited < 0 && errno != EINTR) { throw std::runtime_error(systemError("media-ctl waitpid")); }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("media-ctl timeout: " + stderr_text);
            }
            std::array<pollfd, 2> fds{{{output[0], POLLIN, 0}, {error[0], POLLIN, 0}}};
            if (::poll(fds.data(), fds.size(), 10) < 0 && errno != EINTR) {
                throw std::runtime_error(systemError("media-ctl poll"));
            }
        }
        drain(output[0], stdout_text);
        drain(error[0], stderr_text);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("media-ctl failed status=" + std::to_string(status) + ": " + stderr_text);
        }
        cleanup();
        return stdout_text;
    } catch (...) {
        cleanup();
        throw;
    }
}
void validateCameraConfig(const FrameSourceConfig &config) {
    if (config.type != FrameSourceType::Camera || config.path.empty() || config.camera_media_device.empty()
        || config.camera_sensor_entity.empty() || config.width <= 0 || config.height <= 0 || config.fps <= 0) {
        throw std::invalid_argument(
            "Camera requires video/media paths, sensor entity, positive width/height/integer fps");
    }
    for (const auto &text : {config.path, config.camera_media_device, config.camera_sensor_entity}) {
        for (unsigned char character : text) {
            if (character < 32 || character == 127) {
                throw std::invalid_argument("Camera paths/entity contain control characters");
            }
        }
    }
    if (config.camera_sensor_entity.find_first_of("\"'") != std::string::npos) {
        throw std::invalid_argument("Camera sensor entity contains quotes");
    }
    if (config.camera_buffer_count < 2 || config.camera_buffer_count > 32) {
        throw std::invalid_argument("Camera buffer count must be 2..32");
    }
    if (!std::isfinite(config.camera_trigger_rate_hz)) {
        throw std::invalid_argument("Camera camera_trigger_rate_hz must be finite");
    }
    if (config.trigger_mode) {
        if (config.camera_trigger_rate_hz < 1.0 || config.camera_trigger_rate_hz > config.fps) {
            throw std::invalid_argument("Camera software trigger requires camera_trigger_rate_hz in 1..fps");
        }
    } else if (config.camera_trigger_rate_hz != 0.0) {
        throw std::invalid_argument("Camera continuous mode requires camera_trigger_rate_hz=0");
    }
    if (config.flip || config.mirror || config.use_history_timestamp) {
        throw std::invalid_argument(
            "Camera flip/mirror/history timestamp unsupported");
    }
    if ((config.output_width != 0 && config.output_width != config.width)
        || (config.output_height != 0 && config.output_height != config.height)) {
        throw std::invalid_argument("Camera scaling unsupported: output must be native size or zero");
    }
}
CameraDevice::CameraDevice(FrameSourceConfig config, std::shared_ptr<CameraSystem> system)
    : config_{std::move(config)}, system_{std::move(system)} {
    validateCameraConfig(config_);
    if (!system_) { throw std::invalid_argument("CameraSystem is required"); }
}
CameraDevice::~CameraDevice() {
    try { finish(); } catch (...) {}
    if (sensor_fd_ >= 0) { system_->close(sensor_fd_); }
}
void CameraDevice::check(int result, const std::string &operation) {
    if (result < 0) { throw std::runtime_error(systemError(config_.path + " " + operation)); }
}
void CameraDevice::configureSensor() {
    auto subdev{system_->execute({"media-ctl", "-d", config_.camera_media_device,
                                 "-e", config_.camera_sensor_entity}, 3000)};
    while (!subdev.empty() && (subdev.back() == '\n' || subdev.back() == '\r')) { subdev.pop_back(); }
    if (subdev.rfind("/dev/", 0) != 0 || subdev.find_first_of("\n\r") != std::string::npos) {
        throw std::runtime_error("media-ctl returned invalid sensor device: " + subdev);
    }
    sensor_path_ = subdev;
    sensor_fd_ = system_->open(subdev, O_RDWR | O_CLOEXEC);
    check(sensor_fd_, "open sensor " + subdev);
    snapshotControls();
    const auto expression{"\"" + config_.camera_sensor_entity + "\":0[fmt:Y8_1X8/"
        + std::to_string(config_.width) + "x" + std::to_string(config_.height)
        + "@1/" + std::to_string(config_.fps) + " field:none]"};
    system_->execute({"media-ctl", "-d", config_.camera_media_device, "--set-v4l2", expression}, 3000);
    setControl(TRIGGER_MODE, config_.trigger_mode ? 1 : 0, "trigger_mode");
    setControl(TRIGGER_SOURCE, config_.trigger_mode ? 0 : 1, "trigger_src");
    setControl(FRAME_RATE, config_.fps, "frame_rate");
    {
        v4l2_subdev_format format{};
        format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        format.pad = 0;
        check(system_->ioctl(sensor_fd_, VIDIOC_SUBDEV_G_FMT, &format), "sensor G_FMT");
        v4l2_subdev_frame_interval interval{};
        interval.pad = 0;
        check(system_->ioctl(sensor_fd_, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &interval), "sensor G_FRAME_INTERVAL");
        if (format.format.width != static_cast<unsigned>(config_.width)
            || format.format.height != static_cast<unsigned>(config_.height)
            || format.format.code != MEDIA_BUS_FMT_Y8_1X8 || format.format.field != V4L2_FIELD_NONE
            || interval.interval.numerator == 0
            || static_cast<std::uint64_t>(interval.interval.numerator) * config_.fps != interval.interval.denominator) {
            throw std::runtime_error("Sensor negotiation mismatch: requested Y8_1X8 "
                + std::to_string(config_.width) + "x" + std::to_string(config_.height) + "@1/"
                + std::to_string(config_.fps) + ", actual code=" + std::to_string(format.format.code)
                + " " + std::to_string(format.format.width) + "x" + std::to_string(format.format.height)
                + " field=" + std::to_string(format.format.field) + " interval="
                + std::to_string(interval.interval.numerator) + "/" + std::to_string(interval.interval.denominator));
        }
    }
}
void CameraDevice::snapshotControls() {
    controls_.clear();
    for (const auto &entry : std::vector<SavedControl>{{TRIGGER_MODE, 0, "trigger_mode", false},
             {TRIGGER_SOURCE, 0, "trigger_src", false}, {FRAME_RATE, 0, "frame_rate", false}}) {
        v4l2_queryctrl query{};
        query.id = entry.id;
        check(system_->ioctl(sensor_fd_, VIDIOC_QUERYCTRL, &query), sensor_path_ + " query " + entry.name);
        const int desired{entry.id == FRAME_RATE ? config_.fps
            : (entry.id == TRIGGER_MODE ? (config_.trigger_mode ? 1 : 0) : (config_.trigger_mode ? 0 : 1))};
        if (query.type != V4L2_CTRL_TYPE_INTEGER || std::string{reinterpret_cast<char *>(query.name), strnlen(reinterpret_cast<char *>(query.name), sizeof(query.name))} != entry.name
            || (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_WRITE_ONLY))
            || query.step <= 0 || desired < query.minimum || desired > query.maximum
            || (desired - query.minimum) % query.step != 0) {
            throw std::runtime_error(sensor_path_ + " incompatible control " + entry.name);
        }
        v4l2_control value{};
        value.id = entry.id;
        check(system_->ioctl(sensor_fd_, VIDIOC_G_CTRL, &value), sensor_path_ + " read " + entry.name);
        controls_.push_back({entry.id, value.value, entry.name, false});
    }
    if (config_.trigger_mode) {
        v4l2_queryctrl query{};
        query.id = SOFTWARE_TRIGGER;
        check(system_->ioctl(sensor_fd_, VIDIOC_QUERYCTRL, &query), sensor_path_ + " query soft_trgone");
        if (query.type != V4L2_CTRL_TYPE_BUTTON || std::string{reinterpret_cast<char *>(query.name), strnlen(reinterpret_cast<char *>(query.name), sizeof(query.name))} != "soft_trgone"
            || (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY))) {
            throw std::runtime_error(sensor_path_ + " incompatible soft_trgone control");
        }
    }
    // media-ctl 设置帧间隔也可能修改 frame_rate；必须在它运行前保存并标记恢复责任。
    for (auto &entry : controls_) { entry.changed = true; }
}
void CameraDevice::setControl(std::uint32_t id, int value, const std::string &name) {
    std::lock_guard<std::mutex> lock{control_mutex_};
    writeAndVerifyControl(id, value, sensor_path_ + " set " + name);
}
void CameraDevice::writeAndVerifyControl(std::uint32_t id, int value, const std::string &operation) {
    v4l2_control control{};
    control.id = id;
    control.value = value;
    check(system_->ioctl(sensor_fd_, VIDIOC_S_CTRL, &control), operation);
    const auto begin{system_->clockUs(CLOCK_MONOTONIC)};
    for (;;) {
        check(system_->ioctl(sensor_fd_, VIDIOC_G_CTRL, &control), operation + " readback");
        const auto elapsed{system_->clockUs(CLOCK_MONOTONIC) - begin};
        if (control.value == value && elapsed <= CONTROL_VERIFY_TIMEOUT_US) { return; }
        if (elapsed >= CONTROL_VERIFY_TIMEOUT_US) {
            throw std::runtime_error(operation + " readback timeout expected=" + std::to_string(value)
                + " actual=" + std::to_string(control.value) + " elapsed_us=" + std::to_string(elapsed)
                + " timeout_us=" + std::to_string(CONTROL_VERIFY_TIMEOUT_US));
        }
        system_->sleepUs(std::min(CONTROL_VERIFY_INTERVAL_US, CONTROL_VERIFY_TIMEOUT_US - elapsed));
    }
}
void CameraDevice::trigger() {
    std::lock_guard<std::mutex> lock{control_mutex_};
    if (!streaming_ || !config_.trigger_mode || sensor_fd_ < 0) {
        throw std::logic_error(config_.path + " software trigger requires streaming software mode");
    }
    v4l2_control control{};
    control.id = SOFTWARE_TRIGGER;
    control.value = 1;
    check(system_->ioctl(sensor_fd_, VIDIOC_S_CTRL, &control), sensor_path_ + " soft_trgone");
}
std::string CameraDevice::restoreControls() {
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
std::vector<std::string> CameraDevice::unrestoredControls() const {
    std::lock_guard<std::mutex> lock{control_mutex_};
    std::vector<std::string> result{};
    for (const auto &entry : controls_) {
        if (entry.changed) { result.push_back(entry.name); }
    }
    return result;
}

void CameraDevice::checkFormat() {
    v4l2_format value{};
    value.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    check(system_->ioctl(fd_, VIDIOC_G_FMT, &value), "G_FMT");
    const auto &pixel{value.fmt.pix_mp};
    format_ = {pixel.width, pixel.height, pixel.plane_fmt[0].bytesperline, pixel.plane_fmt[0].sizeimage, 0};
    if (pixel.width != static_cast<unsigned>(config_.width) || pixel.height != static_cast<unsigned>(config_.height)
        || pixel.pixelformat != V4L2_PIX_FMT_GREY || pixel.num_planes != 1 || pixel.field != V4L2_FIELD_NONE
        || format_.stride < format_.width || format_.sizeimage < requiredBytes(format_)) {
        throw std::runtime_error("Capture negotiation mismatch: requested GREY " + std::to_string(config_.width)
            + "x" + std::to_string(config_.height) + "; actual " + std::to_string(pixel.width) + "x"
            + std::to_string(pixel.height) + " fourcc=" + std::to_string(pixel.pixelformat)
            + " planes=" + std::to_string(pixel.num_planes) + " field=" + std::to_string(pixel.field)
            + " stride=" + std::to_string(format_.stride) + " sizeimage=" + std::to_string(format_.sizeimage));
    }
}
void CameraDevice::prepare() {
    if (fd_ >= 0 || sensor_fd_ >= 0 || !unrestoredControls().empty()) {
        throw std::logic_error("Camera already prepared or controls still require restoration");
    }
    try {
        configureSensor();
        fd_ = system_->open(config_.path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        check(fd_, "open capture");
        v4l2_capability capability{};
        check(system_->ioctl(fd_, VIDIOC_QUERYCAP, &capability), "QUERYCAP");
        const auto caps{(capability.capabilities & V4L2_CAP_DEVICE_CAPS)
            ? capability.device_caps : capability.capabilities};
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(caps & V4L2_CAP_STREAMING)) {
            throw std::runtime_error("Capture requires capture-mplane and streaming capability");
        }
        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        format.fmt.pix_mp.width = config_.width;
        format.fmt.pix_mp.height = config_.height;
        format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_GREY;
        format.fmt.pix_mp.num_planes = 1;
        format.fmt.pix_mp.field = V4L2_FIELD_NONE;
        check(system_->ioctl(fd_, VIDIOC_S_FMT, &format), "S_FMT");
        checkFormat();
    } catch (const std::exception &error) {
        const std::string reason{error.what()};
        const auto cleanup{finish()};
        throw std::runtime_error(config_.path + ": " + reason + cleanup);
    }
}
void CameraDevice::start() {
    try {
        if (fd_ < 0 || allocated_) { throw std::logic_error("Camera start requires fresh prepared device"); }
        {
            std::lock_guard<std::mutex> lock{wake_mutex_};
            wake_fd_ = system_->event();
            check(wake_fd_, "eventfd");
        }
        v4l2_requestbuffers request{};
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        request.memory = V4L2_MEMORY_MMAP;
        request.count = config_.camera_buffer_count;
        check(system_->ioctl(fd_, VIDIOC_REQBUFS, &request), "REQBUFS");
        allocated_ = true;
        if (request.count < 2 || request.count > 32) { throw std::runtime_error("Driver buffer count outside 2..32"); }
        checkFormat();
        format_.buffers = request.count;
        mappings_.reserve(request.count);
        for (std::uint32_t index{0}; index < request.count; ++index) {
            PlaneBuffer query{index};
            check(system_->ioctl(fd_, VIDIOC_QUERYBUF, &query.buffer), "QUERYBUF");
            if (query.buffer.index != index || query.buffer.length != 1
                || query.plane.length < format_.sizeimage) {
                throw std::runtime_error("Invalid mapped buffer" + metadata(query));
            }
            void *address{system_->map(fd_, query.plane.length, query.plane.m.mem_offset)};
            if (address == MAP_FAILED) { check(-1, "mmap index=" + std::to_string(index)); }
            mappings_.push_back({address, query.plane.length});
            PlaneBuffer queued{index};
            check(system_->ioctl(fd_, VIDIOC_QBUF, &queued.buffer), "initial QBUF");
        }
        const auto before{system_->clockUs(CLOCK_MONOTONIC)};
        const auto realtime{system_->clockUs(CLOCK_REALTIME)};
        const auto after{system_->clockUs(CLOCK_MONOTONIC)};
        pair_ = cameraClockPair(before, realtime, after);
        timing_ = std::make_unique<CameraTiming>(pair_);
        auto type{V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
        check(system_->ioctl(fd_, VIDIOC_STREAMON, &type), "STREAMON");
        streaming_ = true;
    } catch (const std::exception &error) {
        const std::string reason{error.what()};
        throw std::runtime_error(reason + finish());
    }
}
CameraSample CameraDevice::next(int timeout_ms, bool compare_pixels) {
    std::array<pollfd, 2> fds{{{fd_, POLLIN, 0}, {wake_fd_, POLLIN, 0}}};
    const int ready{system_->poll(fds.data(), fds.size(), timeout_ms)};
    if (ready < 0 && errno == EINTR) { return {}; }
    check(ready, "poll");
    if (fds[1].revents & POLLIN) { return {}; }
    if ((fds[0].revents | fds[1].revents) & (POLLERR | POLLHUP | POLLNVAL)) {
        throw std::runtime_error("Camera poll device disconnected/error");
    }
    if (!ready || !(fds[0].revents & POLLIN)) { return {}; }
    PlaneBuffer dequeued{};
    const int result{system_->ioctl(fd_, VIDIOC_DQBUF, &dequeued.buffer)};
    if (result < 0 && errno == EAGAIN) { return {}; }
    check(result, "DQBUF");
    CameraSample sample{};
    sample.dequeue_us = system_->clockUs(CLOCK_MONOTONIC);
    // 仅保留当前时钟偏移与轮次固定偏移的差，不用它重标帧时间。
    const auto realtime{system_->clockUs(CLOCK_REALTIME)};
    const auto offset_delta{static_cast<__int128>(realtime) - sample.dequeue_us - pair_.offset_us};
    if (offset_delta < std::numeric_limits<std::int64_t>::min()
        || offset_delta > std::numeric_limits<std::int64_t>::max()) {
        throw std::runtime_error("Camera clock offset diagnostic overflow");
    }
    sample.clock_offset_delta_us = static_cast<std::int64_t>(offset_delta);
    const auto &plane{dequeued.plane};
    const auto &buffer{dequeued.buffer};
    if (buffer.index >= mappings_.size() || buffer.length != 1 || (buffer.flags & V4L2_BUF_FLAG_ERROR)
        || plane.data_offset > plane.bytesused || plane.bytesused > plane.length
        || plane.length > mappings_[buffer.index].length
        || static_cast<std::uint64_t>(plane.bytesused - plane.data_offset) < requiredBytes(format_)) {
        throw std::runtime_error("Invalid dequeued buffer" + metadata(dequeued));
    }
    sample.seconds = buffer.timestamp.tv_sec;
    sample.microseconds = buffer.timestamp.tv_usec;
    sample.flags = buffer.flags;
    sample.sequence = buffer.sequence;
    std::int64_t timestamp{};
    try {
        timestamp = timing_->map(sample.seconds, sample.microseconds, sample.flags, sample.dequeue_us);
    } catch (const std::exception &error) {
        throw std::runtime_error(std::string{error.what()} + metadata(dequeued));
    }
    sample.strategy = timing_->strategy();
    const auto *pixels{static_cast<const unsigned char *>(mappings_[buffer.index].address) + plane.data_offset};
    // OpenCV 只借用映射图像头；copyGrayFrame 深拷贝有效像素后不再依赖设备。
    const cv::Mat gray{config_.height, config_.width, CV_8UC1, const_cast<unsigned char *>(pixels), format_.stride};
    const auto before{system_->clockUs(CLOCK_MONOTONIC)};
    sample.frame = copyGrayFrame(gray, static_cast<std::uint64_t>(timestamp));
    sample.copy_us = system_->clockUs(CLOCK_MONOTONIC) - before;
    if (sample.copy_us < 0) { throw std::runtime_error("Copy clock moved backwards"); }
    if (compare_pixels) {
        const auto view{sample.frame->mapRead()};
        sample.pixels_checked = true;
        sample.pixels_equal = true;
        for (int row{0}; row < config_.height; ++row) {
            if (std::memcmp(view->mat().ptr(row), gray.ptr(row), config_.width) != 0) {
                sample.pixels_equal = false;
                break;
            }
        }
        if (!sample.pixels_equal) { throw std::runtime_error("Same-frame driver pixel comparison failed"); }
    }
    PlaneBuffer returned{buffer.index};
    check(system_->ioctl(fd_, VIDIOC_QBUF, &returned.buffer), "return QBUF" + metadata(dequeued));
    return sample;
}
std::string CameraDevice::wake() {
    std::lock_guard<std::mutex> lock{wake_mutex_};
    if (wake_fd_ >= 0 && system_->signal(wake_fd_) < 0) { return systemError("eventfd signal"); }
    return {};
}
std::string CameraDevice::finish() {
    std::string errors{};
    auto record{[&](int result, const char *operation) {
        if (result < 0) { errors += "; cleanup " + systemError(operation); }
    }};
    if (fd_ >= 0 && streaming_) {
        auto type{V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
        const int result{system_->ioctl(fd_, VIDIOC_STREAMOFF, &type)};
        record(result, "STREAMOFF");
        if (result < 0) {
            // 驱动没有确认停流时先关闭设备，再解除映射，避免驱动继续访问。
            record(system_->close(fd_), "close after STREAMOFF failure");
            fd_ = -1;
        }
    }
    streaming_ = false;
    for (auto it{mappings_.rbegin()}; it != mappings_.rend(); ++it) {
        record(system_->unmap(it->address, it->length), "munmap");
    }
    mappings_.clear();
    if (fd_ >= 0 && allocated_) {
        v4l2_requestbuffers request{};
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        request.memory = V4L2_MEMORY_MMAP;
        record(system_->ioctl(fd_, VIDIOC_REQBUFS, &request), "release REQBUFS");
    }
    allocated_ = false;
    if (fd_ >= 0) { record(system_->close(fd_), "close capture"); fd_ = -1; }
    {
        std::lock_guard<std::mutex> lock{wake_mutex_};
        if (wake_fd_ >= 0) { record(system_->close(wake_fd_), "close eventfd"); wake_fd_ = -1; }
    }
    if (sensor_fd_ < 0 && !unrestoredControls().empty()) {
        sensor_fd_ = system_->open(sensor_path_, O_RDWR | O_CLOEXEC);
        record(sensor_fd_, "reopen sensor for control restoration");
    }
    if (sensor_fd_ >= 0) {
        errors += restoreControls();
        // 本次尝试结束后释放句柄，未恢复项仍保留；重复 finish 可重新打开并重试。
        record(system_->close(sensor_fd_), "close sensor");
        sensor_fd_ = -1;
    }
    timing_.reset();
    return errors;
}
} // namespace rk3576
