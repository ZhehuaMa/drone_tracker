#ifndef DRONE_TRACKER_TRACKING_GEOMETRY_H
#define DRONE_TRACKER_TRACKING_GEOMETRY_H

#include <algorithm>
#include <cmath>
#include <opencv2/core.hpp>

namespace tracking {
// Filter vectors use physical centers; all rectangle consumers use top-left coordinates.
inline cv::Rect2f filterRectangle(const cv::Vec4d &center) {
    return {static_cast<float>(center[0] - center[2] / 2),
        static_cast<float>(center[1] - center[3] / 2),
        static_cast<float>(center[2]), static_cast<float>(center[3])};
}
inline bool validRectangle(const cv::Rect2f &box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width)
        && std::isfinite(box.height) && box.width > 0 && box.height > 0;
}
inline cv::Rect searchRegion(const cv::Rect2f &box, int width, int height,
    float scale, const cv::Size &minimum) {
    const int w{std::min(width, std::max(static_cast<int>(box.width * scale), minimum.width))};
    const int h{std::min(height, std::max(static_cast<int>(box.height * scale), minimum.height))};
    const int x{std::clamp(static_cast<int>(box.x + box.width / 2 - w / 2.0f), 0, width - w)};
    const int y{std::clamp(static_cast<int>(box.y + box.height / 2 - h / 2.0f), 0, height - h)};
    return {x, y, w, h};
}
} // namespace tracking
#endif // DRONE_TRACKER_TRACKING_GEOMETRY_H
