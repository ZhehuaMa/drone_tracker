#include "../mjpeg_video_reader.h"
#include "video_test_support.h"

#include <iostream>

int main(int argc, char **argv) {
    using namespace video_test;
    using namespace rk3576;
    try {
        require(argc == 2, "Expected fixture directory");
        const std::string root{argv[1]};
        std::atomic<bool> stop{false};
        for (int sampling : {0, 420, 422, 444}) {
            const std::string stem{root + "/" + std::to_string(sampling)};
            MjpegVideoReader reader{stem + ".mov", stop};
            for (int i{0}; i < 3; ++i) {
                const auto result{reader.next()};
                require(result.status == VideoReadStatus::Frame, "Missing valid frame including first");
                const auto expected{reference(bytes(stem + "-" + std::to_string(i) + ".jpg"))};
                require(result.sample.gray.size() == cv::Size{641, 479}, "MCU padding exposed");
                require(cv::countNonZero(result.sample.gray != expected) == 0, "Luminance mismatch");
                require(result.sample.has_pts, "PTS missing");
            }
            require(reader.next().status == VideoReadStatus::Eof, "Not EOF after three video frames");
        }
        MjpegVideoReader bad{root + "/bad.mov", stop};
        require(bad.next().status == VideoReadStatus::Frame, "Bad fixture first frame");
        require(bad.next().status == VideoReadStatus::DecodeError, "Corrupt JPEG mistaken for EOF");
        MjpegVideoReader resized{root + "/resize.mov", stop};
        resized.next();
        require(resized.next().status == VideoReadStatus::SizeChanged, "Dimension change accepted");
        for (const auto &path : {root, root + "/missing", std::string{"https://invalid/video"},
                                root + "/non-mjpeg.mov"}) {
            rejects([&] { MjpegVideoReader invalid{path, stop}; }, "Invalid input accepted: " + path);
        }
        // 两个解码器交错读取，前一个实例的像素不能被覆盖。
        MjpegVideoReader a{root + "/420.mov", stop};
        MjpegVideoReader b{root + "/444.mov", stop};
        const auto held{a.next().sample.gray};
        const auto expected{held.clone()};
        b.next();
        a.next();
        require(cv::countNonZero(held != expected) == 0, "Cross-instance/shared buffer overwrite");
        std::cout << "MJPEG luminance and file tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
