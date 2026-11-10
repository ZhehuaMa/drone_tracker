#include "hybrid_tracker.h"
#include "../../tests/ros_test_support.h"
#include <cmath>
#include <iostream>

int main() {
    try {
        auto trace{std::make_shared<ros_test::Trace>()};
        trace->target = true;
        TrackerSettings settings{};
        settings.enable_dcf = false;
        settings.yolo_interval = 1000;
        settings.roi_scale_factor = 100;
        settings.kf_fx = 100;
        settings.kf_fy = 110;
        settings.kf_cx0 = 50;
        settings.kf_cy0 = 40;
        auto tracker{HybridTracker::CreateInstance(settings, ros_test::components(trace))};
        model_test::require(tracker->init(), "observation tracker initialization");
        std::int64_t index{};
        auto update{[&](const cv::Matx33d &prediction_rotation) {
            FrameContext context{};
            const auto capture{1000000 + ++index * 10000};
            context.frame = std::make_shared<ros_test::TestFrame<1>>(
                cv::Mat{80, 100, CV_8UC1, cv::Scalar{42}}, capture);
            context.now_us = capture + 30000;
            context.evidence = {tracking::ClockMapping::StandaloneAdmissionV1, tracking::monotonicNow(), true};
            context.R_cam_now = prediction_rotation;
            return tracker->update(context);
        }};
        TrackerOutput output{};
        for (int attempt{}; attempt < 40 && !output.tracking; ++attempt) {
            output = update(cv::Matx33d::eye());
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        model_test::require(output.tracking && output.reset_los, "successful recovery did not mark LOS reset");
        model_test::require(output.observation_box.width > 0 && output.observation_box.height > 0,
            "valid floating prediction missing");
        output = update(cv::Matx33d::eye());
        model_test::require(output.observation_box.width != std::floor(output.observation_box.width),
            "KF prediction lost fractional width");
        const double yaw{1.2};
        const cv::Matx33d turned{std::cos(yaw), -std::sin(yaw), 0,
            std::sin(yaw), std::cos(yaw), 0, 0, 0, 1};
        output = update(turned);
        model_test::require(!output.tracking && !output.display_box.is_valid,
            "offscreen geometry failed to revoke output permission");
        model_test::require(output.observation_box.empty(), "fully offscreen prediction remained valid");
        tracker->finishRound();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
    return 0;
}
