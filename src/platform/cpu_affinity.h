#ifndef DRONE_TRACKER_CPU_AFFINITY_H
#define DRONE_TRACKER_CPU_AFFINITY_H
#include <cstdint>
#include <functional>
#include <exception>
#include <string>
#include <thread>
#include <vector>
namespace cpu_affinity {
enum class Role { Control, Tracking, Yolo, Display, Capture, Recording, RosTransport, TopicRecording };
enum class Mode { Inherit, RolePinned };
struct Policy {
    bool configured{};
    Mode mode{Mode::Inherit};
    std::vector<int> allowed_cpus{};
    int control{-1}, tracking{-1}, yolo{-1}, display{-1}, capture{-1}, recording{-1};
    int ros_transport{-1};
    int topic_recording{-1};
};
/** @brief Explicit restoration keeps errors out of destructors during exception unwinding. */
class Transition {
public:
    explicit Transition(std::function<void()> restore = {}) : restore_{std::move(restore)} {}
    Transition(Transition &&) = default;
    Transition(const Transition &) = delete;
    void restore();
    [[noreturn]] void restoreAfterFailure(std::exception_ptr failure);
    void release() noexcept { restore_ = {}; }
private:
    std::function<void()> restore_{};
};
Policy load(const std::string &path);
void initializeFromEnvironment();
void initializeControl();
const Policy &current();
const char *name(Role role) noexcept;
int core(const Policy &policy, Role role) noexcept;
/** @brief Missing optional topic policy preserves each caller's legacy role. */
Role topicRecordingRole(const Policy &policy, Role legacy_role) noexcept;
Role topicRecordingRole(Role legacy_role);
std::string apply(const Policy &policy, Role role, std::uint64_t round = 0);
void apply(Role role, std::uint64_t round = 0);
Transition enter(const Policy &policy, Role role);
Transition enter(Role role);
/** @brief Run resource creation on a role, restoring the caller on success and failure. */
void withRole(const Policy &policy, Role role, const std::function<void()> &work);
void withRole(Role role, const std::function<void()> &work);
std::thread start(const Policy &policy, Role role, std::function<void()> work,
    std::function<std::thread(std::function<void()>)> launcher = {}, std::uint64_t round = 0);
std::thread start(Role role, std::function<void()> work,
    std::function<std::thread(std::function<void()>)> launcher = {}, std::uint64_t round = 0);
} // namespace cpu_affinity
#endif // DRONE_TRACKER_CPU_AFFINITY_H
