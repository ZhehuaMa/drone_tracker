#include "camera_driver_support.h"
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
    camera_driver::Options options{};
    options.config = config(); options.config.width = 8; options.config.height = 3;
    options.output = root / "normal";
    options.frames = 5; options.rounds = 2; options.instances = 2; options.compare = true;
    camera_driver::run(options, dependencies(system));
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
    rejects([&] { camera_driver::run(options, dependencies(system)); }, "already exists");
    require(read(options.output / "frames.csv") == frames, "Existing output overwritten");
    // /dev/full 可靠地让写入返回 ENOSPC，不要求真实相机或填满磁盘。
    std::ofstream full{"/dev/full"};
    auto injected{dependencies(system)};
    injected.observe = [&](const CameraSample &, const CameraDiagnostic &) {
        full << "frame"; full.flush();
        if (!full) { throw std::runtime_error("injected output write failed"); }
    };
    Rk3576CameraFrameSource failing{options.config, injected};
    require(failing.start(), "Output failure source start");
    await([&] { return failing.status().state == FrameSourceState::Failed; });
    failing.stop(); system->clean();
    require(failing.status().reason.find("output write failed") != std::string::npos, "Output failure invisible");
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
        std::cout << "camera driver tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
