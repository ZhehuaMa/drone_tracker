#include "rknn_runtime.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "host_frame.h"
#include "model/yolo_output_adapter.h"
#include "rknn_yolo_backend.h"
#include "yolo_detector.h"
#include "yolo_preprocessor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace {
struct Options {
    std::string model;
    std::string image;
    std::string video;
    std::string mode{"detect"};
    std::string dump;
    int target{-1};
    int constant{-1};
    cv::Rect roi{};
};
int integer(const std::string &text) {
    std::size_t consumed{0};
    const int value{std::stoi(text, &consumed)};
    if (consumed != text.size()) {
        throw std::invalid_argument("Invalid integer: " + text);
    }
    return value;
}
Options parse(int argc, char **argv) {
    Options options;
    for (int i{1}; i < argc; ++i) {
        const std::string key{argv[i]};
        if (++i >= argc) {
            throw std::invalid_argument("Missing value for " + key);
        }
        const std::string value{argv[i]};
        if (key == "--model") {
            options.model = value;
        } else if (key == "--image") {
            options.image = value;
        } else if (key == "--video") {
            options.video = value;
        } else if (key == "--mode") {
            options.mode = value;
        } else if (key == "--dump") {
            options.dump = value;
        } else if (key == "--target") {
            options.target = integer(value);
        } else if (key == "--constant") {
            options.constant = integer(value);
        } else if (key == "--roi") {
            if (i + 3 >= argc) {
                throw std::invalid_argument("--roi requires x y width height");
            }
            options.roi = {integer(value), integer(argv[i + 1]), integer(argv[i + 2]), integer(argv[i + 3])};
            i += 3;
            if (options.roi.x < 0 || options.roi.y < 0 || options.roi.width <= 0 || options.roi.height <= 0) {
                throw std::invalid_argument("Invalid ROI");
            }
        } else {
            throw std::invalid_argument("Unknown option: " + key);
        }
    }
    if (options.model.empty()) {
        throw std::invalid_argument("--model is required");
    }
    if (options.mode != "query" && options.mode != "region" && options.mode != "detect") {
        throw std::invalid_argument("--mode must be query, region or detect");
    }
    if (options.mode != "query") {
        const int sources{static_cast<int>(!options.image.empty()) + static_cast<int>(!options.video.empty()) +
                          static_cast<int>(options.constant >= 0)};
        if (sources != 1 || options.constant > 255) {
            throw std::invalid_argument("Select one image/video/constant 0..255");
        }
        if (options.mode == "region" && (!options.video.empty() || options.dump.empty())) {
            throw std::invalid_argument("region mode requires --image/--constant and --dump prefix");
        }
        if (options.mode == "detect" && options.target < 0) {
            throw std::invalid_argument("detect requires --target >= 0");
        }
    }
    return options;
}
cv::Mat grayImage(const cv::Mat &source) {
    if (source.empty()) {
        throw std::invalid_argument("Cannot read input image");
    }
    cv::Mat gray;
    if (source.type() == CV_8UC1) {
        gray = source;
    } else if (source.type() == CV_8UC3) {
        cv::cvtColor(source, gray, cv::COLOR_BGR2GRAY);
    } else {
        throw std::invalid_argument("Input must be Gray8 or BGR8");
    }
    return gray;
}
void writeBytes(const std::string &path, const void *data, std::size_t bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char *>(data), static_cast<std::streamsize>(bytes));
    if (!output) {
        throw std::runtime_error("Cannot write " + path);
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto options{parse(argc, argv)};
        auto runtime{rk3576::RknnRuntime::load(options.model, rk3576::nativeRknnApi())};
        if (!runtime) {
            throw std::runtime_error(model::describe(runtime.error));
        }
        std::cout << runtime.value->describe();
        if (options.mode == "query") {
            return 0;
        }
        cv::Mat image;
        cv::VideoCapture video;
        if (!options.video.empty()) {
            if (!video.open(options.video)) {
                throw std::runtime_error("Cannot open video");
            }
        } else {
            image = options.constant >= 0
                        ? cv::Mat(640, 640, CV_8UC1, cv::Scalar{static_cast<double>(options.constant)})
                        : cv::imread(options.image, cv::IMREAD_COLOR);
            image = grayImage(image);
        }
        if (options.mode == "region") {
            rk3576::YoloPreprocessor preprocessor;
            auto session{preprocessor.beginFrame(rk3576::copyGrayFrame(image), rk3576::yoloContract())};
            if (!session) {
                throw std::runtime_error(session.message);
            }
            auto region{session.value->prepare(options.roi)};
            if (!region) {
                throw std::runtime_error(region.message);
            }
            const auto input{std::dynamic_pointer_cast<const rk3576::ByteInput>(region.value.input)};
            auto raw{runtime.value->execute(input->bytes())};
            if (!raw) {
                throw std::runtime_error(model::describe(raw.error));
            }
            auto prediction{model::adaptYoloSixOutputs(raw.value, options.model)};
            if (!prediction) {
                throw std::runtime_error(model::describe(prediction.error));
            }
            writeBytes(options.dump + ".input.u8", input->bytes().data(), input->bytes().size());
            for (std::size_t i{0}; i < raw.value.size(); ++i) {
                writeBytes(options.dump + ".output" + std::to_string(i) + ".f32", raw.value[i].read().data(),
                           raw.value[i].descriptor.bytes);
            }
            writeBytes(options.dump + ".prediction.f32", prediction.value.predictions.read().data(),
                       prediction.value.predictions.descriptor.bytes);
            const auto &lb{region.value.transform};
            std::cout << "region resized=" << lb.resized << " padding=" << lb.left << ',' << lb.top << ',' << lb.right
                      << ',' << lb.bottom << " scale=" << lb.scale_x << ',' << lb.scale_y << '\n';
            return 0;
        }
        YoloV8Detector detector{std::make_unique<rk3576::RknnYoloBackend>(std::move(runtime.value)),
                                std::make_unique<rk3576::YoloPreprocessor>(),
                                0.5f,
                                0.5f,
                                options.target,
                                0.5f};
        if (!detector.init()) {
            throw std::runtime_error("Detector initialization failed");
        }
        std::uint64_t frame_id{0};
        do {
            if (video.isOpened()) {
                cv::Mat source;
                if (!video.read(source)) {
                    break;
                }
                image = grayImage(source);
            }
            ++frame_id;
            if (!detector.request(rk3576::copyGrayFrame(image), frame_id, options.roi)) {
                throw std::runtime_error("Detector rejected request");
            }
            YoloResult result;
            while (!detector.get_result(result)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            std::cout << "frame=" << result.frame_id << " hit=" << result.has_target;
            if (result.has_target) {
                std::cout << " class=" << result.best.classId << " score=" << result.best.score
                          << " box=" << result.best.box;
            }
            std::cout << '\n';
        } while (video.isOpened());
        detector.stop();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n用法：--model path --mode query|region|detect "
                  << "[--target id] [--image path|--video path|--constant 0..255] "
                  << "[--roi x y w h] [--dump prefix]\n";
        return 1;
    }
}
