/** @brief 仅通过隔离 worker 的 LD_PRELOAD 注入错误，不链接生产程序。 */
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>

namespace {
bool selected(int fd, const char *fault) {
    const char *mode{std::getenv("DRONE_DIAGNOSTIC_TEST_FAULT")};
    if (!mode || std::strcmp(mode, fault) != 0) { return false; }
    const char *armed{std::getenv("DRONE_DIAGNOSTIC_FAULT_ARM_PATH")};
    if (armed && ::access(armed, F_OK) != 0) { return false; }
    char path[64]{}, target[1024]{};
    std::snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    const ssize_t length{::readlink(path, target, sizeof(target) - 1)};
    return length > 0 && std::strstr(target, "/drone-diagnostic-acceptance-20261012/evidence/")
        && std::strstr(target, ".dtd");
}
} // namespace
extern "C" ssize_t write(int fd, const void *data, size_t count) {
    using Function = ssize_t (*)(int, const void *, size_t);
    const auto original{reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "write"))};
    if (selected(fd, "write")) { errno = EIO; return -1; }
    return original(fd, data, count);
}
extern "C" int fdatasync(int fd) {
    using Function = int (*)(int);
    const auto original{reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "fdatasync"))};
    if (selected(fd, "sync")) { errno = EIO; return -1; }
    return original(fd);
}
