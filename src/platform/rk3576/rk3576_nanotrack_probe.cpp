#include "rknn_nanotrack_factory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "host_frame.h"
#include "nano_tracker.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace {
struct Options {
    std::string template_path{};
    std::string search_path{};
    std::string head_path{};
    std::string mode{"query"};
    std::string video{};
    cv::Rect2f bbox{};
    bool has_bbox{false};
};
Options parse(int argc, char **argv) {
    Options options;
    for (int i{1}; i < argc; ++i) {
        const std::string key{argv[i]};
        if (i + 1 >= argc) {
            throw std::invalid_argument("Missing value for " + key);
        }
        const std::string value{argv[++i]};
        if (key == "--template-model") {
            options.template_path = value;
        } else if (key == "--search-model") {
            options.search_path = value;
        } else if (key == "--head-model") {
            options.head_path = value;
        } else if (key == "--mode") {
            options.mode = value;
        } else if (key == "--video") {
            options.video = value;
        } else if (key == "--bbox") {
            std::istringstream in{value};
            char comma1{};
            char comma2{};
            char comma3{};
            auto &b{options.bbox};
            if (!(in >> b.x >> comma1 >> b.y >> comma2 >> b.width >> comma3 >> b.height) || comma1 != ',' ||
                comma2 != ',' || comma3 != ',' || !(in >> std::ws).eof() || !std::isfinite(b.x) ||
                !std::isfinite(b.y) || !std::isfinite(b.width) || !std::isfinite(b.height) || b.width <= 0 ||
                b.height <= 0) {
                throw std::invalid_argument("--bbox requires finite x,y,w,h with positive w,h");
            }
            options.has_bbox = true;
        } else {
            throw std::invalid_argument("Unknown option " + key);
        }
    }
    if (options.template_path.empty() || options.search_path.empty() || options.head_path.empty() ||
        (options.mode != "query" && options.mode != "replay") ||
        (options.mode == "replay" && (options.video.empty() || !options.has_bbox))) {
        throw std::invalid_argument("Use --template-model PATH --search-model PATH --head-model PATH "
                                    "--mode query|replay [--video PATH --bbox x,y,w,h]");
    }
    return options;
}
FrameRef grayFrame(const cv::Mat &image) {
    cv::Mat gray;
    if (image.type() == CV_8UC3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else if (image.type() == CV_8UC4) {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    } else if (image.type() == CV_8UC1) {
        gray = image;
    } else {
        throw std::runtime_error("Video must decode to Gray8/BGR8/BGRA8");
    }
    return rk3576::copyGrayFrame(gray);
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto options{parse(argc, argv)};
        auto components{rk3576::loadNanoTrack(options.template_path, options.search_path, options.head_path)};
        if (!components) {
            std::cerr << model::describe(components.error) << '\n';
            return 1;
        }
        if (options.mode == "query") {
            std::cout << components.value.description;
            return 0;
        }
        std::cerr << components.value.description << "replay input: explicit BGR/BGRA -> Gray8, Float32 0..255 crops\n";
        cv::VideoCapture video{options.video};
        cv::Mat image;
        if (!video.isOpened() || !video.read(image) || image.empty()) {
            throw std::runtime_error("Cannot read first video frame: " + options.video);
        }
        NanoTracker tracker{std::move(components.value.backend), std::move(components.value.preprocessor)};
        auto begin{std::chrono::steady_clock::now()};
        tracker.init(grayFrame(image), options.bbox);
        if (!tracker.isInitialized()) {
            throw std::runtime_error("NanoTracker initialization failed");
        }
        std::cout << "frame,success,x,y,w,h,confidence,elapsed_ms\n";
        const auto &b{options.bbox};
        std::cout << "0,1," << b.x << ',' << b.y << ',' << b.width << ',' << b.height << ",,"
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()
                  << '\n';
        std::size_t index{1};
        while (video.read(image) && !image.empty()) {
            begin = std::chrono::steady_clock::now();
            cv::Rect2f box;
            const float confidence{tracker.update(grayFrame(image), box)};
            const double elapsed{
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()};
            const bool success{std::isfinite(confidence) && confidence >= 0 && std::isfinite(box.x) &&
                               std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height) &&
                               box.width > 0 && box.height > 0};
            std::cout << index++ << ',' << (success ? 1 : 0) << ',';
            if (success) {
                std::cout << box.x << ',' << box.y << ',' << box.width << ',' << box.height << ',' << confidence << ',';
            } else {
                std::cout << ",,,,,";
            }
            std::cout << elapsed << '\n';
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
