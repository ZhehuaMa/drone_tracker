#ifndef RK3576_CAMERA_TEST_SUPPORT_H
#define RK3576_CAMERA_TEST_SUPPORT_H

#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

#include "../camera_frame_source.h"

namespace camera_test {
inline void require(bool value, const std::string &reason) {
    if (!value) { throw std::runtime_error(reason); }
}
template <class Work> void rejects(Work work, const std::string &part) {
    try { work(); } catch (const std::exception &error) {
        require(std::string{error.what()}.find(part) != std::string::npos,
                "Wrong error: " + std::string{error.what()} + "; expected " + part);
        return;
    }
    throw std::runtime_error("Expected failure: " + part);
}
inline FrameSourceConfig config() {
    FrameSourceConfig value{};
    value.path = "/dev/test-video";
    value.camera_media_device = "/dev/test-media";
    value.camera_sensor_entity = "test sensor";
    value.width = 1920;
    value.height = 1200;
    value.fps = 60;
    value.trigger_mode = false;
    return value;
}
/** @brief 替身执行真实 CameraDevice 的调用流程；不打开任何相机设备。 */
class System : public rk3576::CameraSystem {
public:
    std::vector<std::string> calls{};
    std::map<std::string, int> counts{};
    std::string failure{};
    int occurrence{1};
    std::set<int> descriptors{};
    std::set<void *> mapped{};
    std::vector<std::vector<unsigned char>> pixels{};
    int next_fd{10};
    int width{1920};
    int height{1200};
    unsigned stride{2048};
    unsigned offset{13};
    unsigned buffer_count{4};
    unsigned sequence{};
    unsigned sequence_step{1};
    int produced{};
    int frame_limit{100};
    bool bad_sensor{};
    bool bad_interval{};
    bool bad_format{};
    bool bad_caps{};
    bool bad_query{};
    bool format_changes_after_buffers{};
    std::function<void(v4l2_buffer &)> mutate{};
    std::atomic<bool> waiting{false};
    bool wake_pending{};
    bool auto_timeout{};
    bool streaming{};
    bool allocated{};
    std::int64_t monotonic{10000000};
    std::int64_t realtime{1700000000000000};
    int realtime_reads{};
    std::mutex wait_mutex{};
    std::condition_variable cv{};
    bool hit(const std::string &name) {
        calls.push_back(name);
        const int count{++counts[name]};
        if (name == failure && count == occurrence) { errno = EIO; return true; }
        return false;
    }
    std::string execute(const std::vector<std::string> &args, int timeout) override {
        require(timeout == 3000 && args.size() == 5 && args[0] == "media-ctl", "Unsafe tool arguments");
        if (hit(args[3] == "-e" ? "entity" : "configure")) { throw std::runtime_error("injected tool failure"); }
        if (args[3] == "-e") { return "/dev/test-subdev\n"; }
        require(args[4].find("\"test sensor\":0[fmt:Y8_1X8/") == 0, "Sensor format arguments");
        return {};
    }
    int open(const std::string &path, int) override {
        if (hit(path == "/dev/test-subdev" ? "open_sensor" : "open_capture")) { return -1; }
        const int fd{next_fd++};
        descriptors.insert(fd);
        return fd;
    }
    int close(int fd) override {
        const bool failed{hit("close")};
        require(descriptors.erase(fd) == 1, "Double/unknown close");
        if (streaming) { streaming = false; allocated = false; }
        return failed ? -1 : 0;
    }
    int event() override {
        if (hit("event")) { return -1; }
        std::lock_guard<std::mutex> lock{wait_mutex};
        wake_pending = false;
        const int fd{next_fd++};
        descriptors.insert(fd);
        return fd;
    }
    int signal(int) override {
        std::lock_guard<std::mutex> lock{wait_mutex};
        wake_pending = true;
        cv.notify_all();
        return 0;
    }
    int poll(pollfd *fds, nfds_t count, int timeout) override {
        require(count == 2, "Missing stop poll fd");
        if (hit("poll")) { return -1; }
        if (produced < frame_limit) { fds[0].revents = POLLIN; return 1; }
        if (auto_timeout) { monotonic += 100000; return 0; }
        std::unique_lock<std::mutex> lock{wait_mutex};
        waiting.store(true);
        cv.wait_for(lock, std::chrono::milliseconds{timeout}, [&] { return wake_pending; });
        waiting.store(false);
        if (wake_pending) { fds[1].revents = POLLIN; return 1; }
        monotonic += 100000;
        return 0;
    }
    std::int64_t clockUs(clockid_t clock) override {
        if (hit("clock")) { throw std::runtime_error("injected clock failure"); }
        if (clock == CLOCK_REALTIME) { ++realtime_reads; return realtime; }
        return monotonic++;
    }
    void *map(int, std::size_t length, std::uint64_t index) override {
        if (hit("map")) { return MAP_FAILED; }
        require(index < pixels.size() && length == pixels[index].size(), "Wrong map length/offset");
        void *address{pixels[index].data()};
        require(mapped.insert(address).second, "Double mapping");
        return address;
    }
    int unmap(void *address, std::size_t) override {
        const bool failed{hit("unmap")};
        require(mapped.erase(address) == 1, "Double unmap");
        return failed ? -1 : 0;
    }
    int ioctl(int, unsigned long request, void *argument) override {
        if (request == VIDIOC_SUBDEV_G_FMT) {
            if (hit("sensor_format")) { return -1; }
            auto &value{*static_cast<v4l2_subdev_format *>(argument)};
            value.format.width = width + (bad_sensor ? 1 : 0);
            value.format.height = height;
            value.format.code = MEDIA_BUS_FMT_Y8_1X8;
            value.format.field = V4L2_FIELD_NONE;
        } else if (request == VIDIOC_SUBDEV_G_FRAME_INTERVAL) {
            if (hit("sensor_interval")) { return -1; }
            auto &value{*static_cast<v4l2_subdev_frame_interval *>(argument)};
            value.interval = {1, bad_interval ? 30U : 60U};
        } else if (request == VIDIOC_QUERYCAP) {
            if (hit("caps")) { return -1; }
            auto &value{*static_cast<v4l2_capability *>(argument)};
            value.capabilities = V4L2_CAP_DEVICE_CAPS;
            value.device_caps = bad_caps ? 0 : V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING;
        } else if (request == VIDIOC_S_FMT) {
            if (hit("set_format")) { return -1; }
        } else if (request == VIDIOC_G_FMT) {
            if (hit("get_format")) { return -1; }
            auto &value{static_cast<v4l2_format *>(argument)->fmt.pix_mp};
            value.width = width;
            value.height = height;
            value.pixelformat = bad_format || (allocated && format_changes_after_buffers)
                ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_GREY;
            value.num_planes = 1;
            value.field = V4L2_FIELD_NONE;
            value.plane_fmt[0].bytesperline = stride;
            value.plane_fmt[0].sizeimage = stride * height;
        } else if (request == VIDIOC_REQBUFS) {
            auto &value{*static_cast<v4l2_requestbuffers *>(argument)};
            if (hit(value.count ? "allocate" : "release")) { return -1; }
            allocated = value.count != 0;
            if (value.count) {
                value.count = buffer_count;
                pixels.assign(buffer_count, std::vector<unsigned char>(stride * height + offset, 255));
            }
        } else if (request == VIDIOC_QUERYBUF) {
            if (hit("query")) { return -1; }
            auto &value{*static_cast<v4l2_buffer *>(argument)};
            value.m.planes[0].length = bad_query ? 1 : pixels.at(value.index).size();
            value.m.planes[0].m.mem_offset = value.index;
        } else if (request == VIDIOC_QBUF) {
            if (hit(streaming ? "return" : "queue")) { return -1; }
        } else if (request == VIDIOC_STREAMON) {
            if (hit("stream_on")) { return -1; }
            require(counts["queue"] >= static_cast<int>(buffer_count), "STREAMON before buffers queued");
            streaming = true;
            produced = 0;
            sequence = 0;
        } else if (request == VIDIOC_STREAMOFF) {
            if (hit("stream_off")) { return -1; }
            streaming = false;
        } else if (request == VIDIOC_DQBUF) {
            if (hit("dequeue")) { return -1; }
            auto &value{*static_cast<v4l2_buffer *>(argument)};
            value.index = produced % buffer_count;
            value.sequence = sequence;
            value.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            value.timestamp.tv_sec = 10 + produced / 60;
            value.timestamp.tv_usec = (produced % 60) * 16000;
            auto &plane{value.m.planes[0]};
            plane.length = pixels.at(value.index).size();
            plane.bytesused = plane.length;
            plane.data_offset = offset;
            auto &data{pixels.at(value.index)};
            for (int row{0}; row < height; ++row) {
                for (int column{0}; column < width; ++column) {
                    data[offset + row * stride + column] = (row + column + produced) % 251;
                }
            }
            ++produced;
            sequence += sequence_step;
            if (mutate) { mutate(value); }
        } else {
            throw std::runtime_error("Unexpected ioctl");
        }
        return 0;
    }
    void clean() const {
        require(descriptors.empty() && mapped.empty() && !streaming, "Resource leak");
    }
};
inline rk3576::CameraDependencies dependencies(const std::shared_ptr<System> &system) {
    rk3576::CameraDependencies result{};
    result.system = system;
    return result;
}
template <class Predicate> void await(Predicate predicate) {
    const auto limit{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
    while (!predicate() && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    require(predicate(), "Timed out waiting for controlled worker");
}
} // namespace camera_test
#endif // RK3576_CAMERA_TEST_SUPPORT_H
