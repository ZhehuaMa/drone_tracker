#include "ar0234_camera_driver_support.h"
#include "camera_test_support.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
namespace fs = std::filesystem;
using namespace camera_test;
using namespace rk3576;
std::string read(const fs::path &path) {
    std::ifstream input{path, std::ios::binary};
    std::ostringstream content{};
    content << input.rdbuf();
    require(input.good(), "Cannot read generated report");
    return content.str();
}
void reports(const fs::path &root) {
    auto system{std::make_shared<System>()};
    system->width = 8; system->height = 3; system->stride = 16;
    system->frame_limit = 20;
    ar0234_camera_driver::Options options{};
    options.config = config(); options.config.width = 8; options.config.height = 3;
    options.output = root / "normal";
    options.frames = 5; options.rounds = 2; options.instances = 2; options.compare = true;
    ar0234_camera_driver::run(options, dependencies(system));
    system->clean();
    const auto frames{read(options.output / "frames.csv")};
    require(frames.find("driver_monotonic") != std::string::npos && frames.find("dequeue_us") != std::string::npos,
            "Frame timing report missing");
    require(read(options.output / "resources.csv").find("destroyed") != std::string::npos,
            "Post-destruction resources missing");
    const auto rounds{read(options.output / "rounds.csv")};
    require(rounds.find("1,1,") != std::string::npos && rounds.find("Stopped by caller") != std::string::npos,
            "Instance/round report missing");
    for (int instance{0}; instance < 2; ++instance) {
        for (int round{0}; round < 2; ++round) {
            const auto bytes{read(options.output / ("frame-" + std::to_string(instance) + "-"
                + std::to_string(round) + "-0.pgm"))};
            require(bytes.substr(0, 11) == "P5\n8 3\n255\n" && bytes.size() == 35, "Lossless snapshot layout");
            for (int row{0}; row < 3; ++row) {
                for (int column{0}; column < 8; ++column) {
                    require(static_cast<unsigned char>(bytes[11 + row * 8 + column]) == row + column,
                            "Snapshot valid pixel mismatch");
                }
            }
        }
    }
    rejects([&] { ar0234_camera_driver::run(options, dependencies(system)); }, "already exists");
    require(read(options.output / "frames.csv") == frames, "Existing output overwritten");
    // /dev/full 可靠地让写入返回 ENOSPC，不要求真实相机或填满磁盘。
    std::ofstream full{"/dev/full"};
    auto injected{dependencies(system)};
    injected.observe = [&](const CameraSample &, const Ar0234CameraDiagnostic &) {
        full << "frame"; full.flush();
        if (!full) { throw std::runtime_error("injected output write failed"); }
    };
    Ar0234CameraFrameSource failing{options.config, injected};
    require(failing.start(), "Output failure source start");
    await([&] { return failing.status().state == FrameSourceState::Failed; });
    failing.stop(); system->clean();
    require(failing.status().reason.find("output write failed") != std::string::npos, "Output failure invisible");
}
void finiteReports(const fs::path &root) {
    for (int commands : {0, 1, 120}) {
        auto system{std::make_shared<System>()};
        system->width = 8; system->height = 3; system->stride = 16;
        system->frame_limit = commands == 120 ? 60 : commands;
        ar0234_camera_driver::Options options{};
        options.config = config(); options.config.width = 8; options.config.height = 3;
        options.config.trigger_mode = true; options.config.camera_trigger_rate_hz = 20;
        options.finite_commands = true; options.commands = commands; options.tail_ms = 10; options.snapshots = 0;
        options.output = root / ("finite-" + std::to_string(commands));
        auto injected{dependencies(system)};
        std::atomic<std::int64_t> clock{0};
        injected.trigger_now = [&] { return clock.load(); };
        injected.trigger_wait = [&](std::condition_variable &, std::unique_lock<std::mutex> &,
                                    std::int64_t due, const std::function<bool()> &) { clock = due; };
        ar0234_camera_driver::run(options, injected);
        system->clean();
        require(system->trigger_calls == static_cast<unsigned>(commands), "Finite command limit ignored");
        const auto rounds{read(options.output / "rounds.csv")};
        require(rounds.find("command_frame_difference") != std::string::npos
            && rounds.find("software_trigger,20,") != std::string::npos, "Missing difference diagnostics");
        if (commands == 120) {
            require(rounds.find(",60,0,1,\"\"") != std::string::npos,
                    "120 commands/60 frames not reported as unattributed difference");
        }
        const auto events{read(options.output / "triggers.csv")};
        require(std::count(events.begin(), events.end(), '\n') == commands + 1, "Missing command events");
    }
    auto system{std::make_shared<System>()};
    system->width = 8; system->height = 3; system->stride = 16;
    ar0234_camera_driver::Options options{};
    options.config = config(); options.config.width = 8; options.config.height = 3;
    options.config.trigger_mode = true; options.config.camera_trigger_rate_hz = 1;
    options.finite_commands = true; options.commands = 120; options.seconds = 1; options.snapshots = 0;
    options.output = root / "finite-timeout";
    rejects([&] { ar0234_camera_driver::run(options, dependencies(system)); }, "tool timeout");
    system->clean();
    require(read(options.output / "rounds.csv").find(",1,1,\"\"") != std::string::npos,
            "Timeout/restore result not saved");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Test output root required");
        const fs::path root{argv[1]};
        // 仅清理 CTest 专属测试目录，不接受用户工具的覆盖选项。
        fs::remove_all(root);
        fs::create_directories(root);
        reports(root);
        finiteReports(root);
        std::cout << "camera driver tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
