#include "platform/cpu_affinity.h"
#include <atomic>
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
std::string writePolicy(const std::string &body) {
    char path[]{"/tmp/drone-cpu-policy-XXXXXX"};
    const int fd{::mkstemp(path)};
    assert(fd >= 0);
    ::close(fd);
    std::ofstream{path} << body;
    return path;
}
void rejects(const std::string &body) {
    const auto path{writePolicy(body)};
    bool failed{};
    try { cpu_affinity::load(path); } catch (const std::exception &) { failed = true; }
    ::unlink(path.c_str());
    assert(failed);
}
cpu_set_t mask() {
    cpu_set_t result{};
    assert(::sched_getaffinity(0, sizeof(result), &result) == 0);
    return result;
}
} // namespace
int main() {
    using namespace cpu_affinity;
    assert(!load("").configured);
    rejects("version: 2\nmode: inherit\nallowed_cpus: [0]\n");
    rejects("version: 1\nmode: other\nallowed_cpus: [0]\n");
    rejects("version: 1\nmode: inherit\nallowed_cpus: []\n");
    rejects("version: 1\nmode: inherit\nallowed_cpus: [0, 0]\n");
    rejects("version: 1\nmode: inherit\nallowed_cpus: [-1]\n");
    rejects("version: 1\nmode: inherit\nallowed_cpus: [99999]\n");
    const auto original{mask()};
    int first{-1}, second{-1};
    for (int i{}; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &original)) {
            if (first < 0) { first = i; } else { second = i; break; }
        }
    }
    assert(first >= 0);
    const auto valid{writePolicy("version: 1\nmode: role-pinned\nallowed_cpus: ["
        + std::to_string(first) + (second >= 0 ? ", " + std::to_string(second) : "")
        + "]\nroles:\n  control: " + std::to_string(first)
        + "\n  tracking: " + std::to_string(first)
        + "\n  yolo: " + std::to_string(first)
        + "\n  display: " + std::to_string(second >= 0 ? second : first)
        + "\n  capture: " + std::to_string(first)
        + "\n  recording: " + std::to_string(second >= 0 ? second : first) + "\n")};
    Policy p{load(valid)};
    std::ifstream legacy_input{valid};
    const std::string legacy{std::istreambuf_iterator<char>{legacy_input}, {}};
    assert(p.ros_transport == first);
    for (const auto &invalid : {"null", "true", "1.5", "'0'", "-1", "99999"}) {
        rejects(legacy + "  ros_transport: " + invalid + "\n");
    }
    const auto explicit_policy{writePolicy(legacy + "  ros_transport: "
        + std::to_string(second >= 0 ? second : first) + "\n")};
    const auto explicit_value{load(explicit_policy)};
    ::unlink(explicit_policy.c_str());
    assert(explicit_value.ros_transport == (second >= 0 ? second : first));
    ::unlink(valid.c_str());
    assert(p.configured && p.mode == Mode::RolePinned);
    rejects("version: 1\nmode: role-pinned\nallowed_cpus: [" + std::to_string(first)
        + "]\nroles: {control: " + std::to_string(first) + "}\n");
    if (second >= 0) {
        cpu_set_t one{}; CPU_ZERO(&one); CPU_SET(first, &one);
        assert(::sched_setaffinity(0, sizeof(one), &one) == 0);
        p.ros_transport = second;
        const int priority{::getpriority(PRIO_PROCESS, 0)};
        withRole(p, Role::RosTransport, [&] {
            const auto actual{mask()};
            assert(CPU_COUNT(&actual) == 1 && CPU_ISSET(second, &actual));
            withRole(p, Role::Control, [&] {
                const auto nested{mask()}; assert(CPU_COUNT(&nested) == 1 && CPU_ISSET(first, &nested));
            });
            const auto restored{mask()}; assert(CPU_COUNT(&restored) == 1 && CPU_ISSET(second, &restored));
        });
        auto restored{mask()}; assert(CPU_COUNT(&restored) == 1 && CPU_ISSET(first, &restored));
        bool work_failed{};
        try { withRole(p, Role::RosTransport, [] { throw std::runtime_error("creation failed"); }); }
        catch (const std::runtime_error &error) { work_failed = std::string{error.what()} == "creation failed"; }
        assert(work_failed);
        restored = mask(); assert(CPU_COUNT(&restored) == 1 && CPU_ISSET(first, &restored));
        bool restore_failed{};
        Transition failing{[] { throw std::runtime_error("injected restore failure"); }};
        try { failing.restoreAfterFailure(std::make_exception_ptr(std::runtime_error{"creation failed"})); }
        catch (const std::runtime_error &error) {
            const std::string text{error.what()};
            restore_failed = text.find("injected restore failure") != std::string::npos
                && text.find("creation failed") != std::string::npos;
        }
        assert(restore_failed && ::getpriority(PRIO_PROCESS, 0) == priority);
        std::atomic<bool> worked{false};
        for (int i{}; i < 2; ++i) {
            auto thread{start(p, Role::Display, [&] {
                const auto actual{mask()};
                worked = CPU_ISSET(second, &actual) && CPU_COUNT(&actual) == 1;
            })};
            thread.join();
            assert(worked);
            worked = false;
        }
        auto dispatcher{start(p, Role::Capture, [&] {
            const auto actual{mask()};
            worked = CPU_ISSET(first, &actual) && CPU_COUNT(&actual) == 1;
        })};
        dispatcher.join();
        assert(worked);
        worked = false;
        Policy impossible{p};
        impossible.capture = CPU_SETSIZE - 1;
        bool failed{};
        try { auto thread{start(impossible, Role::Capture, [&] { worked = true; })}; thread.join(); }
        catch (const std::exception &) { failed = true; }
        assert(failed && !worked);
        apply(p, Role::Recording, 3);
        std::thread writer{[&] {
            const auto actual{mask()};
            worked = CPU_ISSET(second, &actual) && CPU_COUNT(&actual) == 1;
        }};
        writer.join();
        assert(worked);
        assert(::sched_setaffinity(0, sizeof(original), &original) == 0);
    }
}
