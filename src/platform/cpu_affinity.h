#ifndef DRONE_TRACKER_CPU_AFFINITY_H
#define DRONE_TRACKER_CPU_AFFINITY_H
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>
namespace cpu_affinity {
enum class Role { Control, Tracking, Yolo, Display, Capture, Recording };
enum class Mode { Inherit, RolePinned };
struct Policy {
    bool configured{};
    Mode mode{Mode::Inherit};
    std::vector<int> allowed_cpus{};
    int control{-1}, tracking{-1}, yolo{-1}, display{-1}, capture{-1}, recording{-1};
};
Policy load(const std::string &path);
void initializeFromEnvironment();
void initializeControl();
const Policy &current();
const char *name(Role role) noexcept;
int core(const Policy &policy, Role role) noexcept;
std::string apply(const Policy &policy, Role role, std::uint64_t round = 0);
void apply(Role role, std::uint64_t round = 0);
std::thread start(const Policy &policy, Role role, std::function<void()> work,
    std::function<std::thread(std::function<void()>)> launcher = {}, std::uint64_t round = 0);
std::thread start(Role role, std::function<void()> work,
    std::function<std::thread(std::function<void()>)> launcher = {}, std::uint64_t round = 0);
} // namespace cpu_affinity
#endif // DRONE_TRACKER_CPU_AFFINITY_H
