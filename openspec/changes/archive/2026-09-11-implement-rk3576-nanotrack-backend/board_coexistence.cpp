#include "rknn_nanotrack_factory.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "host_frame.h"
#include "nano_tracker.h"
#include "rknn_runtime.h"
#include "rknn_yolo_backend.h"
#include "yolo_detector.h"
#include "yolo_preprocessor.h"

#include <opencv2/imgcodecs.hpp>

namespace {
std::atomic<bool> g_entered{false};
std::atomic<bool> g_running{false};
int observedRun(rknn_context context, rknn_run_extend *extend) {
    g_running.store(true);
    g_entered.store(true);
    const int result{rk3576::nativeRknnApi().run(context, extend)};
    g_running.store(false);
    return result;
}
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void resources(int cycle, const std::chrono::steady_clock::time_point &start) {
    std::ifstream input{"/proc/self/status"};
    std::string line;
    std::cout << "cycle=" << cycle << " elapsed_ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    while (std::getline(input, line)) {
        if (line.rfind("VmRSS:", 0) == 0 || line.rfind("Threads:", 0) == 0) {
            std::cout << ' ' << line;
        }
    }
    std::size_t count{0};
    for (const auto &entry : std::filesystem::directory_iterator{"/proc/self/fd"}) {
        (void)entry;
        ++count;
    }
    std::cout << " fd=" << count << std::endl;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 6, "usage: board_coexistence TEMPLATE SEARCH HEAD YOLO IMAGE");
        cv::setNumThreads(1);
        const cv::Mat image{cv::imread(argv[5], cv::IMREAD_GRAYSCALE)};
        require(!image.empty(), "missing image");
        const auto frame{rk3576::copyGrayFrame(image)};
        const auto start{std::chrono::steady_clock::now()};
        resources(0, start);
        for (int cycle{1}; cycle <= 20; ++cycle) {
            {
                auto nano{rk3576::loadNanoTrack(argv[1], argv[2], argv[3])};
                require(static_cast<bool>(nano), "NanoTrack load failed");
                auto api{rk3576::nativeRknnApi()};
                api.run = observedRun;
                auto yolo{rk3576::RknnRuntime::load(argv[4], api)};
                require(static_cast<bool>(yolo), "YOLO load failed");
                NanoTracker tracker{std::move(nano.value.backend), std::move(nano.value.preprocessor)};
                YoloV8Detector detector{std::make_unique<rk3576::RknnYoloBackend>(std::move(yolo.value)),
                    std::make_unique<rk3576::YoloPreprocessor>(), 0.5f, 0.5f, 2, 0.5f};
                require(detector.init(), "YOLO init failed");
                for (int step{0}; step < 10; ++step) {
                    tracker.reset();
                    tracker.reset();
                    tracker.init(frame, {461.99968f, 275.00032f, 21.00032f, 19.999744f});
                    require(tracker.isInitialized(), "template failed");
                    require(detector.request(frame, step + 1, {}), "YOLO request failed");
                    cv::Rect2f box;
                    require(tracker.update(frame, box) >= 0, "NanoTrack search failed");
                    YoloResult result;
                    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{20}};
                    while (!detector.get_result(result)) {
                        require(std::chrono::steady_clock::now() < deadline, "YOLO result timeout");
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    require(result.frame_id == static_cast<std::uint64_t>(step + 1), "wrong frame result");
                }
                g_entered.store(false);
                require(detector.request(frame, 11, {}), "final request failed");
                const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{20}};
                while (!g_entered.load()) {
                    require(std::chrono::steady_clock::now() < deadline, "run entry timeout");
                    std::this_thread::yield();
                }
                detector.stop();
                detector.stop();
                require(!g_running.load(), "stop returned before run ended");
            }
            resources(cycle, start);
        }
        std::cout << "PASS cycles=20 nano_templates=200 nano_searches=200 yolo_results=200 stop_after_entry=20\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
