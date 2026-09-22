#include "ar0234_camera_driver_support.h"

#include <iostream>

int main(int argc, char **argv) {
    try {
        const auto options{rk3576::ar0234_camera_driver::parse(argc, argv)};
        try { rk3576::ar0234_camera_driver::run(options); } catch (const std::exception &error) {
            std::cerr << "采集失败：" << error.what() << '\n';
            return 1;
        }
        std::cout << "采集结束，报告已保存。\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n用法：ar0234_camera_source_driver --video-device PATH --media-device PATH"
            " --sensor-entity NAME --width W --height H --fps N --output NEW_DIR"
            " (--frames N | --seconds N | --commands N) [--buffers 4] [--rounds 1] [--instances 1]"
            " [--mode continuous|software_trigger] [--trigger-hz HZ] [--tail-ms 500] [--slow-ms 0] [--snapshots 1] [--compare-pixels]\n";
        return 2;
    }
}
