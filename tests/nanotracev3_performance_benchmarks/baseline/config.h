#ifndef NANOTRACK_BASELINE_CONFIG_H
#define NANOTRACK_BASELINE_CONFIG_H
#include <array>
#include <filesystem>
#include <opencv2/core.hpp>
#include <string>
namespace baseline {
struct Config {
    std::filesystem::path source{}, video{}, output{}, evidence{};
    std::array<std::filesystem::path, 3> models{};
    cv::Rect2f bbox{};
    int warmup_updates{30}, max_updates{0}, repeats{3}, environment_interval_ms{1000};
    bool detailed{true};
    std::string board{};
};
Config loadConfig(const std::filesystem::path &path);
} // namespace baseline
#endif // NANOTRACK_BASELINE_CONFIG_H
