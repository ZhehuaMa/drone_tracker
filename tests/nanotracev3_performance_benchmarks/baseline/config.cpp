#include "config.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace baseline {
namespace {
int integer(const cv::FileStorage &file, const char *key, int fallback, int minimum) {
    const auto node{file[key]};
    if (node.empty()) {
        return fallback;
    }
    if (!node.isInt() && !node.isReal()) {
        throw std::invalid_argument(std::string(key) + " 必须为整数");
    }
    const double value{static_cast<double>(node)};
    if (!std::isfinite(value) || value < minimum || value > std::numeric_limits<int>::max() ||
        std::floor(value) != value) {
        throw std::invalid_argument(std::string(key) + " 数值非法");
    }
    return static_cast<int>(value);
}
} // namespace
Config loadConfig(const std::filesystem::path &path) {
    Config config;
    config.source = std::filesystem::absolute(path).lexically_normal();
    cv::FileStorage file(config.source.string(), cv::FileStorage::READ);
    if (!file.isOpened()) {
        throw std::invalid_argument("配置无法打开");
    }
    if (integer(file, "schema_version", 0, 1) != 1) {
        throw std::invalid_argument("schema_version 必须为 1");
    }
    const auto resolve{[&](const char *key, bool input) {
        const auto node{file[key]};
        if (!node.isString() || node.string().empty()) {
            throw std::invalid_argument(std::string(key) + " 缺失");
        }
        std::filesystem::path result{node.string()};
        if (result.is_relative()) {
            result = config.source.parent_path() / result;
        }
        result = result.lexically_normal();
        if (input && !std::filesystem::is_regular_file(result)) {
            throw std::invalid_argument(std::string(key) + " 必须为本地普通文件: " + result.string());
        }
        return result;
    }};
    config.video = resolve("video", true);
    config.models = {resolve("template_model", true), resolve("search_model", true), resolve("head_model", true)};
    config.evidence = resolve("precision_evidence", false);
    config.output = resolve("output", false);
    if (std::filesystem::exists(config.output)) {
        throw std::invalid_argument("输出目录已存在");
    }
    const auto box{file["bbox"]};
    if (!box.isSeq() || box.size() != 4) {
        throw std::invalid_argument("bbox 必须为四个有限数值");
    }
    std::array<float, 4> values{};
    for (int i{0}; i < 4; ++i) {
        const auto node{box[i]};
        const double value{static_cast<double>(node)};
        if ((!node.isInt() && !node.isReal()) || !std::isfinite(value) ||
            std::abs(value) > std::numeric_limits<float>::max()) {
            throw std::invalid_argument("bbox 非法");
        }
        values[i] = static_cast<float>(value);
    }
    if (values[2] <= 0 || values[3] <= 0) {
        throw std::invalid_argument("bbox 宽高必须为正");
    }
    config.bbox = {values[0], values[1], values[2], values[3]};
    config.warmup_updates = integer(file, "warmup_updates", 30, 0);
    config.max_updates = integer(file, "max_updates", 0, 0);
    config.repeats = integer(file, "repeats", 3, 1);
    config.environment_interval_ms = integer(file, "environment_interval_ms", 1000, 1);
    config.detailed = integer(file, "detailed_observation", 1, 0) != 0;
    if (integer(file, "detailed_observation", 1, 0) > 1) {
        throw std::invalid_argument("detailed_observation 仅为 0/1");
    }
    config.board = file["board"].string();
    if (config.board.empty()) {
        throw std::invalid_argument("board 必须说明板卡编号");
    }
    return config;
}
} // namespace baseline
