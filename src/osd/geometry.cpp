#include "osd/geometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace osd {
VideoGeometry fitVideo(int source_width, int source_height, int canvas_width, int canvas_height, int alignment) {
    if (source_width <= 0 || source_height <= 0 || alignment <= 0
        || canvas_width < alignment || canvas_height < alignment) {
        throw std::invalid_argument("OSD invalid source, canvas or alignment");
    }
    const double scale{std::min(static_cast<double>(canvas_width) / source_width,
                                static_cast<double>(canvas_height) / source_height)};
    const int width{static_cast<int>(std::floor(source_width * scale)) / alignment * alignment};
    const int height{static_cast<int>(std::floor(source_height * scale)) / alignment * alignment};
    if (width == 0 || height == 0) {
        throw std::invalid_argument("OSD video aspect ratio cannot fit aligned canvas");
    }
    return {source_width, source_height,
            {(canvas_width - width) / 2 / alignment * alignment,
             (canvas_height - height) / 2 / alignment * alignment, width, height}};
}
std::optional<Rectangle> VideoGeometry::mapBox(const DisplayBox &box) const {
    if (source_width <= 0 || source_height <= 0 || destination.width <= 0 || destination.height <= 0
        || !std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width)
        || !std::isfinite(box.height) || box.width <= 0 || box.height <= 0) {
        return {};
    }
    const double left{std::clamp(box.x, 0.0, static_cast<double>(source_width))};
    const double top{std::clamp(box.y, 0.0, static_cast<double>(source_height))};
    const double right{std::clamp(box.x + box.width, 0.0, static_cast<double>(source_width))};
    const double bottom{std::clamp(box.y + box.height, 0.0, static_cast<double>(source_height))};
    if (right <= left || bottom <= top) {
        return {};
    }
    const int x{static_cast<int>(std::floor(left * destination.width / source_width))};
    const int y{static_cast<int>(std::floor(top * destination.height / source_height))};
    const int x2{static_cast<int>(std::ceil(right * destination.width / source_width))};
    const int y2{static_cast<int>(std::ceil(bottom * destination.height / source_height))};
    return Rectangle{destination.x + x, destination.y + y, x2 - x, y2 - y};
}
} // namespace osd
