#include <linux/videodev2.h>
#include <dlfcn.h>
#include <unistd.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

// 仅由独立验收进程通过 LD_PRELOAD 加载；不安装到正式程序。
namespace {
std::mutex camera_mutex{};
timeval previous{};
bool have_previous{};
constexpr const char *REPEAT{
    "/home/hzhy/drone-recording-validation-20260923/repeat-timestamps"};
constexpr const char *BLOCK{
    "/home/hzhy/drone-recording-validation-20260923/block-video-sync"};
} // namespace

extern "C" int ioctl(int fd, unsigned long request, ...) noexcept {
    va_list arguments;
    va_start(arguments, request);
    void *argument{va_arg(arguments, void *)};
    va_end(arguments);
    using Function = int (*)(int, unsigned long, ...);
    static const auto original{reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "ioctl"))};
    const int result{original(fd, request, argument)};
    if (result != 0) { return result; }
    std::lock_guard<std::mutex> lock{camera_mutex};
    if (request == VIDIOC_STREAMON) { have_previous = false; }
    if (request == VIDIOC_DQBUF) {
        auto &buffer{*static_cast<v4l2_buffer *>(argument)};
        if (have_previous && ::access(REPEAT, F_OK) == 0) { buffer.timestamp = previous; }
        previous = buffer.timestamp;
        have_previous = true;
    }
    return result;
}

extern "C" int fdatasync(int fd) {
    using Function = int (*)(int);
    static const auto original{reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "fdatasync"))};
    if (::access(BLOCK, F_OK) == 0) {
        std::array<char, 64> descriptor{};
        std::array<char, 4096> path{};
        std::snprintf(descriptor.data(), descriptor.size(), "/proc/self/fd/%d", fd);
        const auto size{::readlink(descriptor.data(), path.data(), path.size() - 1)};
        if (size > 3 && std::strcmp(path.data() + size - 3, ".ts") == 0) {
            std::fprintf(stderr, "TEST blocking video fdatasync for 10 seconds\n");
            ::sleep(10);
        }
    }
    return original(fd);
}
