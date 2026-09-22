#include "rk3576_camera_capture_base.h"

#include <fcntl.h>
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
        if (config.camera_sensor_model == CameraSensorModel::Sc233hgs) {
            throw std::invalid_argument("SC233HGS supports continuous capture only; software trigger is unsupported");
        }
        if (config.camera_trigger_rate_hz < 1.0 || config.camera_trigger_rate_hz > config.fps) {
            throw std::invalid_argument("Camera software trigger requires camera_trigger_rate_hz in 1..fps");
        }
    } else if (config.camera_trigger_rate_hz != 0.0) {
        throw std::invalid_argument("Camera continuous mode requires camera_trigger_rate_hz=0");
    }
    if (config.camera_sensor_model != CameraSensorModel::Ar0234
        && config.camera_sensor_model != CameraSensorModel::Sc233hgs) {
        throw std::invalid_argument("Unsupported camera sensor model; supported values: ar0234, sc233hgs");
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
Rk3576CameraCaptureBase::Rk3576CameraCaptureBase(FrameSourceConfig config, std::shared_ptr<CameraSystem> system)
    : config_{std::move(config)}, system_{std::move(system)} {
    validateCameraConfig(config_);
    if (!system_) { throw std::invalid_argument("CameraSystem is required"); }
}
void Rk3576CameraCaptureBase::check(int result, const std::string &operation) const {
    if (result < 0) { throw std::runtime_error(systemError(config_.path + " " + operation)); }
}
void Rk3576CameraCaptureBase::trigger() {
    throw std::logic_error(config_.path + " sensor does not support software trigger");
}
std::vector<std::string> Rk3576CameraCaptureBase::unrestoredControls() const { return {}; }
void Rk3576CameraCaptureBase::checkFormat() {
    v4l2_format value{};
    value.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    check(system_->ioctl(fd_, VIDIOC_G_FMT, &value), "G_FMT");
    const auto &pixel{value.fmt.pix_mp};
    format_ = {pixel.width, pixel.height, pixel.plane_fmt[0].bytesperline, pixel.plane_fmt[0].sizeimage, 0};
    const auto expected_format{pixelFormat()};
    if (pixel.width != static_cast<unsigned>(config_.width) || pixel.height != static_cast<unsigned>(config_.height)
        || pixel.pixelformat != expected_format || pixel.num_planes != 1 || pixel.field != V4L2_FIELD_NONE
        || format_.stride < format_.width || format_.sizeimage < requiredBytes(format_)) {
        throw std::runtime_error("Capture negotiation mismatch: requested "
            + std::string{pixelFormatName()} + " " + std::to_string(config_.width)
            + "x" + std::to_string(config_.height) + "; actual " + std::to_string(pixel.width) + "x"
            + std::to_string(pixel.height) + " fourcc=" + std::to_string(pixel.pixelformat)
            + " planes=" + std::to_string(pixel.num_planes) + " field=" + std::to_string(pixel.field)
            + " stride=" + std::to_string(format_.stride) + " sizeimage=" + std::to_string(format_.sizeimage));
    }
}
void Rk3576CameraCaptureBase::prepare() {
    if (fd_ >= 0 || hasOpenSensorState() || !unrestoredControls().empty()) {
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
        format.fmt.pix_mp.pixelformat = pixelFormat();
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
void Rk3576CameraCaptureBase::start() {
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
CameraSample Rk3576CameraCaptureBase::next(int timeout_ms, bool compare_pixels) {
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
    // OpenCV 只借用映射图像头；copyGrayFrame 深拷贝有效像素后不再依赖设备，两种传感器共用同一份提取/复制逻辑：
    // 无论驱动协商的是 GREY 还是 NV12，取出的都只是紧邻缓冲区起始的 Y（亮度）平面字节。
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
std::string Rk3576CameraCaptureBase::wake() {
    std::lock_guard<std::mutex> lock{wake_mutex_};
    if (wake_fd_ >= 0 && system_->signal(wake_fd_) < 0) { return systemError("eventfd signal"); }
    return {};
}
std::string Rk3576CameraCaptureBase::finish() {
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
    errors += finishSensor();
    timing_.reset();
    return errors;
}
} // namespace rk3576
