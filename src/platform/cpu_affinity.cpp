#include "cpu_affinity.h"
#include <algorithm>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#ifdef DRONE_TRACKER_RK3576
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>
#endif
namespace cpu_affinity {
namespace {
Policy snapshot{};
std::once_flag initialized{};
constexpr Role roles[]{Role::Control, Role::Tracking, Role::Yolo, Role::Display, Role::Capture, Role::Recording};
#ifdef DRONE_TRACKER_RK3576
int readCore(const YAML::Node &node, const std::string &field) {
    if (!node || !node.IsScalar()) { throw std::invalid_argument("CPU policy missing " + field); }
    int value{};
    try { value = node.as<int>(); }
    catch (const YAML::Exception &) { throw std::invalid_argument("CPU policy invalid " + field); }
    if (value < 0 || value >= CPU_SETSIZE) { throw std::invalid_argument("CPU policy core out of range: " + field); }
    return value;
}
std::string maskText(const cpu_set_t &mask) {
    std::string result{};
    for (int i{}; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &mask)) { result += (result.empty() ? "" : ",") + std::to_string(i); }
    }
    return result;
}
#endif
} // namespace
const char *name(Role role) noexcept {
    switch (role) {
    case Role::Control: return "control";
    case Role::Tracking: return "tracking";
    case Role::Yolo: return "yolo";
    case Role::Display: return "display";
    case Role::Capture: return "capture";
    case Role::Recording: return "recording";
    }
    return "unknown";
}
int core(const Policy &p, Role role) noexcept {
    switch (role) {
    case Role::Control: return p.control;
    case Role::Tracking: return p.tracking;
    case Role::Yolo: return p.yolo;
    case Role::Display: return p.display;
    case Role::Capture: return p.capture;
    case Role::Recording: return p.recording;
    }
    return -1;
}
Policy load(const std::string &path) {
    Policy p{};
    if (path.empty()) { return p; }
#ifdef DRONE_TRACKER_RK3576
    if (path.front() != '/') { throw std::invalid_argument("CPU policy path must be absolute"); }
    YAML::Node root{};
    try { root = YAML::LoadFile(path); }
    catch (const YAML::Exception &e) { throw std::invalid_argument("Cannot load CPU policy " + path + ": " + e.what()); }
    if (!root.IsMap() || readCore(root["version"], "version") != 1) {
        throw std::invalid_argument("CPU policy version must be 1");
    }
    if (!root["mode"] || !root["mode"].IsScalar()) { throw std::invalid_argument("CPU policy missing mode"); }
    const std::string mode{root["mode"].as<std::string>()};
    if (mode == "role-pinned") { p.mode = Mode::RolePinned; }
    else if (mode == "inherit") { p.mode = Mode::Inherit; }
    else { throw std::invalid_argument("CPU policy unsupported mode: " + mode); }
    const auto allowed{root["allowed_cpus"]};
    if (!allowed || !allowed.IsSequence() || !allowed.size()) {
        throw std::invalid_argument("CPU policy allowed_cpus must be a nonempty list");
    }
    for (const auto &entry : allowed) {
        const int value{readCore(entry, "allowed_cpus")};
        if (std::find(p.allowed_cpus.begin(), p.allowed_cpus.end(), value) != p.allowed_cpus.end()) {
            throw std::invalid_argument("CPU policy duplicate allowed CPU: " + std::to_string(value));
        }
        p.allowed_cpus.push_back(value);
    }
    if (p.mode == Mode::RolePinned) {
        const auto map{root["roles"]};
        if (!map || !map.IsMap()) { throw std::invalid_argument("CPU policy missing roles"); }
        int *targets[]{&p.control, &p.tracking, &p.yolo, &p.display, &p.capture, &p.recording};
        for (std::size_t i{}; i < 6; ++i) {
            const int value{readCore(map[name(roles[i])], name(roles[i]))};
            if (std::find(p.allowed_cpus.begin(), p.allowed_cpus.end(), value) == p.allowed_cpus.end()) {
                throw std::invalid_argument(std::string{"CPU policy role outside allowed_cpus: "} + name(roles[i]));
            }
            *targets[i] = value;
        }
    }
    for (const int value : p.allowed_cpus) {
        const std::string dir{"/sys/devices/system/cpu/cpu" + std::to_string(value)};
        if (::access(dir.c_str(), F_OK)) { throw std::invalid_argument("CPU policy unavailable CPU: " + std::to_string(value)); }
        std::ifstream state{dir + "/online"};
        if (state) {
            int online{}; state >> online;
            if (online != 1) { throw std::invalid_argument("CPU policy offline CPU: " + std::to_string(value)); }
        }
    }
    p.configured = true;
    return p;
#else
    throw std::invalid_argument("CPU policy requires RK3576 build");
#endif
}
void initializeFromEnvironment() {
    std::call_once(initialized, [] {
        const char *path{std::getenv("DRONE_TRACKER_CPU_CONFIG")};
        snapshot = load(path ? path : "");
        if (snapshot.configured) {
            std::clog << "CPU policy loaded mode=" << (snapshot.mode == Mode::RolePinned ? "role-pinned" : "inherit") << '\n';
        }
    });
}
const Policy &current() { initializeFromEnvironment(); return snapshot; }
std::string apply(const Policy &p, Role role, std::uint64_t round) {
    if (!p.configured || p.mode == Mode::Inherit) { return "inherited"; }
#ifdef DRONE_TRACKER_RK3576
    const int requested{core(p, role)};
    cpu_set_t target{}; CPU_ZERO(&target); CPU_SET(requested, &target);
    if (::sched_setaffinity(0, sizeof(target), &target)) {
        throw std::runtime_error(std::string{"CPU affinity role="} + name(role) + " core="
            + std::to_string(requested) + ": " + std::strerror(errno));
    }
    cpu_set_t actual{};
    if (::sched_getaffinity(0, sizeof(actual), &actual)) {
        throw std::runtime_error(std::string{"CPU affinity readback failed role="} + name(role)
            + ": " + std::strerror(errno));
    }
    if (!CPU_EQUAL(&target, &actual)) {
        throw std::runtime_error(std::string{"CPU affinity readback mismatch role="} + name(role)
            + " requested=" + std::to_string(requested) + " actual=" + maskText(actual));
    }
    const std::string mask{maskText(actual)};
    std::clog << "CPU affinity pid=" << ::getpid() << " tid=" << ::syscall(SYS_gettid)
              << " role=" << name(role) << " requested=" << requested << " actual=" << mask
              << " round=" << round << '\n';
    return mask;
#else
    throw std::runtime_error("CPU affinity requires RK3576 build");
#endif
}
void apply(Role role, std::uint64_t round) { apply(current(), role, round); }
void initializeControl() {
    thread_local bool applied{};
    if (!applied) {
        apply(Role::Control);
        applied = true;
    }
}
std::thread start(const Policy &p, Role role, std::function<void()> work,
    std::function<std::thread(std::function<void()>)> launcher, std::uint64_t round) {
    if (!launcher) { launcher = [](std::function<void()> job) { return std::thread{std::move(job)}; }; }
    if (!p.configured || p.mode == Mode::Inherit) { return launcher(std::move(work)); }
    auto ready{std::make_shared<std::promise<void>>()};
    auto result{ready->get_future()};
    std::thread thread{launcher([p, role, round, ready, work = std::move(work)]() mutable {
        try { apply(p, role, round); ready->set_value(); }
        catch (...) { ready->set_exception(std::current_exception()); return; }
        work();
    })};
    if (!thread.joinable()) { throw std::runtime_error(std::string{"CPU affinity launcher returned no thread: "} + name(role)); }
    try { result.get(); } catch (...) { thread.join(); throw; }
    return thread;
}
std::thread start(Role role, std::function<void()> work,
    std::function<std::thread(std::function<void()>)> launcher, std::uint64_t round) {
    return start(current(), role, std::move(work), std::move(launcher), round);
}
} // namespace cpu_affinity
