#include "platform/cpu_affinity.h"
#include <atomic>
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sched.h>
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
    ::unlink(valid.c_str());
    assert(p.configured && p.mode == Mode::RolePinned);
    rejects("version: 1\nmode: role-pinned\nallowed_cpus: [" + std::to_string(first)
        + "]\nroles: {control: " + std::to_string(first) + "}\n");
    if (second >= 0) {
        cpu_set_t one{}; CPU_ZERO(&one); CPU_SET(first, &one);
        assert(::sched_setaffinity(0, sizeof(one), &one) == 0);
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
        Policy impossible{p};
        impossible.yolo = CPU_SETSIZE - 1;
        bool failed{};
        try { auto thread{start(impossible, Role::Yolo, [&] { worked = true; })}; thread.join(); }
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
