#ifndef REPLAY_RUNNER_H
#define REPLAY_RUNNER_H
#include "replay_clock.h"
#include "replay_report.h"
namespace replay {
/** @brief 测试只注入外部资源和时钟，始终使用真实 HybridTracker。 */
struct ReplayDependencies {
    std::function<std::unique_ptr<PlatformFactory>()> factory{};
    std::function<std::unique_ptr<VideoSource>(const std::string &)> video{openVideo};
    ClockSource clock{steadyClock()};
    DetectionThreadLauncher launch{launchDetectionThread};
    std::function<bool()> interrupted{[] { return false; }};
    std::function<void()> check_diagnostics{[] {}};
};
int runReplay(const ReplayConfig &config, ReplayReport &report, const ReplayDependencies &dependencies);
} // namespace replay
#endif // REPLAY_RUNNER_H
