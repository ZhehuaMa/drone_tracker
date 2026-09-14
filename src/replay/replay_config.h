#ifndef REPLAY_CONFIG_H
#define REPLAY_CONFIG_H

#include <filesystem>
#include "hybrid_tracker/hybrid_tracker.h"

namespace replay {
/** @brief 版本 1 单文件配置，所有运行字段必须显式提供。 */
struct ReplayConfig {
    std::filesystem::path file{};
    ModelPaths models{};
    std::filesystem::path video{};
    double fps{};
    int max_frames{};
    TrackerSettings tracker{};
    std::filesystem::path directory{};
    std::string source{};
};
ReplayConfig readConfig(const std::filesystem::path &path);
/** @brief 输出完整解析快照，路径均为绝对路径。 */
void writeConfig(cv::FileStorage &output, const ReplayConfig &config);
/** @brief 只接受一个配置路径；返回错误时不得加载模型或创建目录。 */
ReplayConfig commandConfig(int argc, const char *const *argv);
} // namespace replay
#endif // REPLAY_CONFIG_H
