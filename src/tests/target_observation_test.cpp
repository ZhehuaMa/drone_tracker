#include "target_observation.h"
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { throw std::runtime_error(message); }
}
void test() {
    TargetObservationBuilder builder{600, 600, {768, 480}, 0};
    const cv::Matx33d identity{cv::Matx33d::eye()};
    cv::Rect2d box{758.25, 465.125, 20.5, 30.25};
    vision_msgs::msg::Detection2D bbox{};
    geometry_msgs::msg::TwistStamped los{};
    require(builder.build(&box, 1000000001, &identity, false, bbox, los), "first observation suppressed");
    require(bbox.header.stamp == los.header.stamp && bbox.header.stamp.nanosec == 1, "paired ns lost");
    require(bbox.bbox.center.position.x == 768.5 && bbox.bbox.center.position.y == 480.25
        && bbox.bbox.size_x == 20.5, "fractional box lost");
    require(bbox.header.frame_id == "camera_frame" && los.header.frame_id == "camera_flu", "frame ids");
    require(los.twist.linear.x == 0 && los.twist.angular.x == 0 && los.twist.angular.y == 0, "LOS fields");
    box.x += 10;
    require(builder.build(&box, 1100000000, &identity, false, bbox, los)
        && los.twist.angular.z != 0, "moving target has no rate");
    require(!builder.build(&box, 1100000000, &identity, true, bbox, los), "duplicate reanchor published");
    box.x += 10;
    require(builder.build(&box, 1200000000, &identity, false, bbox, los)
        && los.twist.angular.z == 0, "duplicate reanchor failed to reset rate");
    require(builder.build(&box, 1300000000, nullptr, false, bbox, los)
        && !bbox.header.frame_id.empty() && los.header.frame_id.empty(), "missing rotation invalidated box");
    const cv::Matx33d bad{cv::Matx33d::zeros()};
    require(builder.build(&box, 1400000000, &bad, false, bbox, los)
        && !bbox.header.frame_id.empty() && los.header.frame_id.empty(), "invalid rotation accepted");
    require(builder.build(nullptr, 1500000000, &identity, false, bbox, los)
        && bbox.header.frame_id.empty() && los.header.frame_id.empty(), "lost target valid");
    require(builder.build(&box, 1600000000, &identity, false, bbox, los)
        && los.twist.angular.z == 0, "recovery retained stale rate");
    box.width = 0;
    require(builder.build(&box, 1700000000, &identity, false, bbox, los)
        && bbox.header.frame_id.empty() && los.header.frame_id.empty(), "empty clipped box valid");
    builder.reset();
    box.width = 20.5;
    require(builder.build(&box, 500000000, &identity, false, bbox, los), "new round inherited timestamp guard");
}
} // namespace
int main() {
    try { test(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
