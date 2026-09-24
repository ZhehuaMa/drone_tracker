#include "session.h"

#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include <yaml-cpp/yaml.h>

namespace {
using namespace rk3576;
using namespace rk3576::recording;
void require(bool value, const char *reason) {
    if (!value) { throw std::runtime_error(reason); }
}
void waitMilliseconds(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds{milliseconds});
}
int fakeWorker() {
    const auto parent{::getppid()};
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) || ::getppid() != parent) { return 2; }
    std::array<char, 131072> text{};
    const auto size{::read(4, text.data(), text.size())};
    if (size <= 0) { return 3; }
    const auto config{YAML::Load(std::string{text.data(), static_cast<std::size_t>(size)})};
    RecordingSlots slots{3, config["width"].as<unsigned>(), config["height"].as<unsigned>(),
        config["round"].as<std::uint64_t>()};
    auto &h{slots.header()};
    const auto mode{config["identity"].as<std::string>()};
    if (mode == "prepare_timeout") {
        ::signal(SIGTERM, SIG_IGN);
        for (;;) { ::pause(); }
    }
    const auto identity{std::to_string(::getpid())};
    std::copy(identity.begin(), identity.end(), h.session_path.begin());
    h.ready.store(true, std::memory_order_release);
    if (mode == "sync_timeout") {
        h.sync_started_us.store(RecordingSlots::monotonicUs() - 5000001);
        for (;;) { ::pause(); }
    }
    if (mode == "shutdown_timeout") { ::signal(SIGTERM, SIG_IGN); }
    while (!h.stopping.load(std::memory_order_acquire)) {
        if (mode == "persist_timeout") { h.last_idr_mono_us.store(RecordingSlots::monotonicUs()); }
        waitMilliseconds(2);
    }
    if (mode == "shutdown_timeout") { for (;;) { ::pause(); } }
    if (!h.summary_size.load(std::memory_order_acquire)) { return 4; }
    h.finished.store(true, std::memory_order_release);
    return 0;
}
FrameSourceConfig configuration(const std::string &directory, const std::string &mode) {
    FrameSourceConfig config{};
    config.camera_sensor_model = CameraSensorModel::Sc233hgs;
    config.camera_recording.enabled = true;
    config.camera_recording.directory = directory;
    config.camera_recording.identity = mode;
    return config;
}
std::size_t descriptorCount() {
    std::size_t count{};
    for (const auto &entry : std::filesystem::directory_iterator{"/proc/self/fd"}) {
        (void)entry; ++count;
    }
    return count;
}
void lifecycle(const std::string &directory, const std::string &executable) {
    CameraFormat format{}; format.width = 8; format.height = 4;
    const auto descriptors{descriptorCount()};
    for (int round{}; round < 20; ++round) {
        Session session{configuration(directory, "normal"), executable};
        session.start(format);
        require(session.sink() != nullptr, "Worker handshake failed");
        require(!session.status().synced_known, "Warmup falsely established sync watermark");
        auto *slots{dynamic_cast<RecordingSlots *>(session.sink())};
        require(slots != nullptr, "Shared recording sink missing");
        // 四种槽状态均可能随进程退出；旧映射不能跨轮重用。
        const auto state{static_cast<RecordingSlots::State>(round % 4)};
        slots->header().slots[0].state.store(state);
        if (round % 2) {
            const auto child{std::stoi(session.status().session)};
            require(child > 0, "Worker PID missing");
            ::kill(child, SIGKILL);
            for (int retry{}; retry < 100 && session.status().phase != "failed"; ++retry) { waitMilliseconds(10); }
            require(session.status().phase == "failed", "Worker death not reported");
        } else {
            session.stop({});
            require(session.status().phase == "finished", "Zero-frame normal stop failed");
        }
        session.start(format);
        require(session.sink() != nullptr && session.status().round == 2, "Next round failed after reclamation");
        session.stop({});
    }
    require(descriptorCount() == descriptors, "Session descriptors grew across rounds");
    for (const std::string mode : {"prepare_timeout", "shutdown_timeout", "sync_timeout"}) {
        Session session{configuration(directory, mode), executable};
        const auto begin{RecordingSlots::monotonicUs()};
        session.start(format);
        if (mode == "sync_timeout") { waitMilliseconds(100); }
        session.stop({});
        require(session.status().phase == "failed", "Timeout not reported");
        require(RecordingSlots::monotonicUs() - begin < 6500000, "Parent exceeded bounded prepare/stop waits");
        waitMilliseconds(700);
    }
    int status{};
    require(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD,
        "Worker left unreaped after timeout");
    require(descriptorCount() == descriptors, "Timeout resources leaked");
    {
        auto permit_reap{std::make_shared<std::atomic<bool>>(false)};
        Session session{configuration(directory, "normal"), executable,
            [permit_reap](pid_t pid, int *status, int options) {
                return permit_reap->load() ? ::waitpid(pid, status, options) : 0;
            }};
        session.start(format);
        const auto round{session.status().round};
        session.stop({});
        session.start(format);
        require(session.status().round == round && session.sink() == nullptr,
            "New round reused an unreclaimed process mapping");
        permit_reap->store(true);
        waitMilliseconds(100);
        session.start(format);
        require(session.sink() != nullptr && session.status().round == round + 1,
            "Reclaimed process did not allow a new round");
        session.stop({});
    }
    require(descriptorCount() == descriptors, "Isolated process resources leaked");
    {
        auto permit_reap{std::make_shared<std::atomic<bool>>(false)};
        {
            Session old{configuration(directory, "normal"), executable,
                [permit_reap](pid_t pid, int *status, int options) {
                    return permit_reap->load() ? ::waitpid(pid, status, options) : 0;
                }};
            old.start(format);
            old.stop({});
        }
        Session next{configuration(directory, "normal"), executable};
        next.start(format);
        require(next.sink() == nullptr && next.status().round == 0,
            "Destroyed session released isolation before orphan reclamation");
        permit_reap->store(true);
        waitMilliseconds(100);
        next.start(format);
        require(next.sink() != nullptr, "Detached reaper did not release next-session guard");
        next.stop({});
    }
    require(descriptorCount() == descriptors, "Detached reaper retained resources after reclamation");
    for (const std::string mode : {"normal", "persist_timeout"}) {
        Session session{configuration(directory, mode), executable};
        session.start(format);
        waitMilliseconds(2100);
        require(session.status().phase == "waiting_for_qualified_input" && !session.status().synced_known,
            "Warmup started video no-progress deadline");
        auto *slots{dynamic_cast<RecordingSlots *>(session.sink())};
        require(slots != nullptr, "Deadline test sink unavailable");
        slots->header().backpressure.store(true);
        std::array<std::uint8_t, 48> pixels{};
        CameraNv12View view{};
        view.pixels = pixels.data(); view.available_bytes = pixels.size();
        view.format.width = 8; view.format.height = 4; view.format.stride = 8;
        const auto begin{RecordingSlots::monotonicUs()};
        while (session.status().phase != "failed" && RecordingSlots::monotonicUs() - begin < 6000000) {
            view.timestamp_us = RecordingSlots::monotonicUs(); ++view.sequence;
            slots->stage(view); slots->publish();
            waitMilliseconds(50);
        }
        const auto result{session.status()};
        require(result.phase == "failed" && result.accepted == 0 && result.attempted > 0,
            "Rejected input renewed no-progress deadline");
        require(result.reason.find(mode == "normal" ? "IDR" : "persistence") != std::string::npos,
            "Wrong no-progress failure classification");
        session.stop({});
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 1) { return fakeWorker(); }
        if (argc != 2) { throw std::invalid_argument("session_test requires an existing ext4 directory"); }
        std::array<char, 4096> path{};
        const auto size{::readlink("/proc/self/exe", path.data(), path.size() - 1)};
        require(size > 0, "Test executable unavailable");
        lifecycle(argv[1], std::string{path.data(), static_cast<std::size_t>(size)});
        std::cout << "20 lifecycle rounds, slot states, kill, recovery and bounded timeout tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
