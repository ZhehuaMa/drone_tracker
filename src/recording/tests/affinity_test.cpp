#include "recording/recording_manager.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>

#include <sched.h>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
cpu_set_t mask(pid_t tid = 0) {
    cpu_set_t result{};
    require(::sched_getaffinity(tid, sizeof(result), &result) == 0, "Cannot read thread affinity");
    return result;
}
bool onCore(const cpu_set_t &value, int core) {
    return CPU_COUNT(&value) == 1 && CPU_ISSET(core, &value);
}
struct PolicyRestore {
    cpu_affinity::Policy &value;
    cpu_affinity::Policy saved;
    ~PolicyRestore() { value = saved; }
};
void exercise(const std::filesystem::path &root, int topic_core, int other_core) {
    auto &policy{const_cast<cpu_affinity::Policy &>(cpu_affinity::current())};
    // Test-only fault injection models an OS rejection after startup validation.
    // All policy edits occur while no topic worker exists; production never mutates it.
    PolicyRestore restore{policy, policy};
    const auto caller{mask()};
    std::shared_ptr<rclcpp::Node> node{};
    cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [&] {
        node = std::make_shared<rclcpp::Node>("topic_affinity_test");
    });
    node->declare_parameter<std::string>("save_path", root.string());
    recording::declareParameters(*node);
    auto config{recording::readConfig(*node)};
    std::atomic<int> opens{0};
    auto hook{[&](const char *stage) {
        if (std::string{stage} == "open") {
            require(onCore(mask(), topic_core), "Writer worked before topic binding");
            ++opens;
        }
    }};
    std::ostringstream logs{};
    auto *previous{std::clog.rdbuf(logs.rdbuf())};
    try {
        {
            recording::RecordingManager manager{*node, config, hook, {}};
            manager.startExecutor();
            const std::regex binding{"CPU affinity pid=\\d+ tid=(\\d+) role=topic_recording requested="};
            std::smatch match{};
            const auto initial{logs.str()};
            require(std::regex_search(initial, match, binding), "Executor binding log missing at readiness");
            require(onCore(mask(static_cast<pid_t>(std::stoi(match[1]))), topic_core),
                "Executor did not retain topic mask");
            for (std::uint64_t round{1}; round <= 2; ++round) {
                manager.open(round, static_cast<std::int64_t>(round) * 1000000000);
                manager.seal("test_off");
                manager.close();
            }
            require(opens == 2, "Recreated writers did not open independently");
            manager.stopExecutor();
        }
        const auto actual{mask()};
        require(CPU_EQUAL(&caller, &actual), "Topic work changed caller affinity");
        require(cpu_affinity::core(policy, cpu_affinity::Role::Recording) == other_core
            && cpu_affinity::core(policy, cpu_affinity::Role::RosTransport) == other_core,
            "Topic role changed camera or transport policy");
        policy.topic_recording = CPU_SETSIZE - 1;
        {
            recording::RecordingManager manager{*node, config, hook, {}};
            bool failed{};
            try { manager.startExecutor(); }
            catch (const std::exception &error) {
                failed = std::string{error.what()}.find("topic_recording") != std::string::npos;
            }
            require(failed, "Executor binding rejection did not reach creator");
            manager.stopExecutor();
        }
        {
            recording::RecordingManager manager{*node, config, hook, {}};
            bool failed{};
            try { manager.open(99, 99000000000); }
            catch (const std::exception &error) {
                failed = std::string{error.what()}.find("topic_recording") != std::string::npos;
            }
            require(failed && !manager.capture() && opens == 2,
                "Rejected writer opened or failed to release the session");
        }
        const auto after_failure{mask()};
        require(CPU_EQUAL(&caller, &after_failure) && rclcpp::ok(),
            "Recorder binding failure damaged caller/ROS lifetime");
    } catch (...) {
        std::clog.rdbuf(previous);
        throw;
    }
    std::clog.rdbuf(previous);
    std::cout << logs.str();
}
} // namespace
int main(int argc, char **argv) {
    const auto original{mask()};
    int first{-1}, second{-1};
    for (int cpu{}; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &original)) {
            if (first < 0) { first = cpu; } else { second = cpu; break; }
        }
    }
    require(first >= 0, "No available core");
    if (second < 0) { second = first; }
    const auto root{std::filesystem::temp_directory_path()
        / ("topic-affinity-" + std::to_string(::getpid()))};
    std::filesystem::create_directories(root);
    const auto path{root / "policy.yaml"};
    {
        std::ofstream file{path};
        file << "version: 1\nmode: role-pinned\nallowed_cpus: [" << first;
        if (second != first) { file << ", " << second; }
        file << "]\nroles:\n  topic_recording: " << first << '\n';
        for (const auto *name : {"control", "tracking", "yolo", "display", "capture", "recording", "ros_transport"}) {
            file << "  " << name << ": " << second << '\n';
        }
    }
    ::setenv("DRONE_TRACKER_CPU_CONFIG", path.c_str(), 1);
    rclcpp::init(argc, argv);
    try {
        exercise(root / "bags", first, second);
        std::filesystem::remove_all(root);
    } catch (const std::exception &error) {
        std::cerr << error.what() << " evidence=" << root << '\n';
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
