#include "target_los.h"
#include "rotation_history.h"
#include <iostream>
#include <limits>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
void near(double a, double b, double tolerance = 1e-9) {
    require(std::abs(a - b) < tolerance, "LOS numerical mismatch");
}
cv::Matx33d rotation(double roll, double pitch, double yaw) {
    const cv::Matx33d rx{1, 0, 0, 0, std::cos(roll), -std::sin(roll), 0, std::sin(roll), std::cos(roll)};
    const cv::Matx33d ry{std::cos(pitch), 0, std::sin(pitch), 0, 1, 0, -std::sin(pitch), 0, std::cos(pitch)};
    const cv::Matx33d rz{std::cos(yaw), -std::sin(yaw), 0, std::sin(yaw), std::cos(yaw), 0, 0, 0, 1};
    return rz * ry * rx;
}
void geometry() {
    TargetLosEstimator estimator{600, 300, {768, 480}, 0};
    const auto first{estimator.update({828, 510}, 1000000, cv::Matx33d::eye())};
    near(first.angles[0], std::atan2(.1, std::hypot(1.0, .1)));
    near(first.angles[1], std::atan2(-.1, 1));
    near(cv::norm(first.rates), 0);
    const auto second{estimator.update({798, 510}, 1100000, cv::Matx33d::eye())};
    near(second.rates[1], (std::atan2(-.05, 1) - first.angles[1]) / .1);
    const auto gap{estimator.update({838, 520}, 1400001, cv::Matx33d::eye())};
    near(cv::norm(gap.rates), 0);
    const auto boundary{estimator.update({848, 520}, 1700001, cv::Matx33d::eye())};
    require(std::abs(boundary.rates[1]) > 0, "300 ms boundary incorrectly reset");
    TargetLosEstimator smooth{600, 600, {768, 480}, .12};
    smooth.update({768, 480}, 1000000, cv::Matx33d::eye());
    const auto filtered{smooth.update({828, 480}, 1100000, cv::Matx33d::eye())};
    near(filtered.rates[1], -std::expm1(-.1 / .12) * std::atan2(-.1, 1) / .1);
    for (const auto bad : {cv::Matx33d::zeros(), cv::Matx33d{1, 0, 0, 0, 1, 0, 0, 0, -1}}) {
        bool rejected{};
        try { estimator.update({800, 480}, 2000000, bad); } catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "invalid rotation accepted");
    }
    bool rejected{};
    try { estimator.update({848, 520}, 1700001, cv::Matx33d::eye()); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "duplicate time accepted");
    rejected = false;
    try { TargetLosEstimator invalid{600, 300, {0, 0}, -1}; }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "negative tau accepted");
}
void compoundRotation() {
    TargetLosEstimator estimator{600, 300, {768, 480}, .12};
    const cv::Vec3d world{1, -.1, -.05};
    for (int index{}; index < 20; ++index) {
        const auto camera{rotation(index * .008, index * -.006, index * .01)};
        const auto ray{camera.t() * world};
        const auto result{estimator.update({768 - 600 * ray[1] / ray[0], 480 - 300 * ray[2] / ray[0]},
            1000000 + index * 10000, camera)};
        near(cv::norm(result.rates), 0, 1e-8);
    }
}
void references() {
    RotationHistory history{};
    cv::Matx33d selected{};
    std::int64_t stamp{};
    bool pose{};
    require(!history.reference(1000000, 1300000, true, stamp, selected, pose) && stamp == 1000000,
        "empty history reference");
    history.push(1030000, cv::Matx33d::eye(), true);
    history.reference(1000000, 1330000, true, stamp, selected, pose);
    require(pose && stamp == 1030000, "fresh boundary rejected");
    history.reference(1000000, 1330001, true, stamp, selected, pose);
    require(!pose && stamp == 1000000, "stale pose relabeled");
    history.reference(1000000, 1029999, true, stamp, selected, pose);
    require(!pose && stamp == 1000000, "future pose accepted");
    history.reference(1030001, 1100000, true, stamp, selected, pose);
    require(!pose && stamp == 1030001, "pose earlier than frame accepted");
    history.reference(1000000, 1100000, false, stamp, selected, pose);
    require(!pose && stamp == 1000000, "capture default changed");
    history.push(1030000, cv::Matx33d::eye(), true);
    require(history.size() == 1 && history.generation() == 0, "duplicate pose not ignored");
    history.push(500000, cv::Matx33d::eye(), true);
    require(history.size() == 1 && history.generation() == 1, "clock rollback not reset");
    RotationHistory legacy{};
    legacy.push(1030000, cv::Matx33d::eye());
    legacy.push(1030000, cv::Matx33d::eye());
    legacy.push(500000, cv::Matx33d::eye());
    RotationSample latest{};
    require(legacy.size() == 1 && legacy.generation() == 0 && legacy.latest(latest)
        && latest.ts_us == 1030000, "Jetson rollback policy changed");
}
} // namespace
int main() {
    try { geometry(); compoundRotation(); references(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
