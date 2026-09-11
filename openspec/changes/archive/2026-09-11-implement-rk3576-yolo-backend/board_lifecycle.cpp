#include "rknn_runtime.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "host_frame.h"
#include "rknn_yolo_backend.h"
#include "yolo_detector.h"
#include "yolo_preprocessor.h"

#include <opencv2/imgcodecs.hpp>

// 实机验收辅助程序。包装真实 SDK 仅记录进入 run 的时刻，不替换推理结果。
namespace {
std::atomic<bool> entered{false};
int observedRun(rknn_context context, rknn_run_extend *extend) {
    entered.store(true);
    return rk3576::nativeRknnApi().run(context, extend);
}
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void resources(int cycle) {
    std::ifstream status{"/proc/self/status"};
    std::string line;
    std::cout << "cycle=" << cycle;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0 || line.rfind("Threads:", 0) == 0) {
            std::cout << ' ' << line;
        }
    }
    std::size_t descriptors{0};
    for (const auto &entry : std::filesystem::directory_iterator{"/proc/self/fd"}) {
        (void)entry;
        ++descriptors;
    }
    std::cout << " fd=" << descriptors << '\n';
}
void awaitRun() {
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
    while (!entered.load()) {
        require(std::chrono::steady_clock::now() < deadline, "run entry timeout");
        std::this_thread::yield();
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: board_lifecycle MODEL IMAGE");
        cv::setNumThreads(1);
        const cv::Mat image{cv::imread(argv[2], cv::IMREAD_GRAYSCALE)};
        require(!image.empty(), "image unavailable");
        auto frame{rk3576::copyGrayFrame(image)};
        rk3576::YoloPreprocessor preprocessor;
        auto session{preprocessor.beginFrame(frame, rk3576::yoloContract())};
        require(static_cast<bool>(session), "snapshot failed");
        auto prepared{session.value->prepare({})};
        require(static_cast<bool>(prepared), "prepare failed");
        const auto input{std::dynamic_pointer_cast<const rk3576::ByteInput>(prepared.value.input)};
        require(static_cast<bool>(input), "byte input missing");
        resources(0);
        for (int cycle{1}; cycle <= 30; ++cycle) {
            {
                auto runtime{rk3576::RknnRuntime::load(argv[1], rk3576::nativeRknnApi())};
                require(static_cast<bool>(runtime), "load failed");
                require(!runtime.value->execute({}), "invalid input accepted");
                auto saved{runtime.value->execute(input->bytes())};
                require(static_cast<bool>(saved), "recovery failed");
                const float first{saved.value.front().read().data()[0]};
                for (int iteration{0}; iteration < 9; ++iteration) {
                    require(static_cast<bool>(runtime.value->execute(input->bytes())), "inference failed");
                }
                runtime.value.reset();
                require(saved.value.front().read().data()[0] == first, "old output changed");
            }
            resources(cycle);
        }
        for (int cycle{0}; cycle < 10; ++cycle) {
            {
                auto api{rk3576::nativeRknnApi()};
                api.run = observedRun;
                auto runtime{rk3576::RknnRuntime::load(argv[1], api)};
                require(static_cast<bool>(runtime), "detector model load failed");
                YoloV8Detector detector{std::make_unique<rk3576::RknnYoloBackend>(std::move(runtime.value)),
                    std::make_unique<rk3576::YoloPreprocessor>(), 0.5f, 0.5f, 2, 0.5f};
                require(detector.init(), "detector init failed");
                entered.store(false);
                require(detector.request(frame, 1, {}), "request failed");
                awaitRun();
                detector.stop();
                detector.stop();
            }
            resources(31 + cycle);
        }
        std::cout << "PASS loads=40 serial_inferences=300 invalid_inputs=30 stop_after_run_entry=10 repeated_stops=10\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
