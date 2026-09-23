#ifndef OSD_GEOMETRY_H
#define OSD_GEOMETRY_H

#include <optional>

#include "osd/presentation.h"

namespace osd {
struct Rectangle {
    int x{}, y{}, width{}, height{};
};
/** @brief 最终视频矩形；视频缩放与目标框必须共用这个结果。 */
struct VideoGeometry {
    int source_width{}, source_height{};
    Rectangle destination{};
    /** @brief 原图框先裁剪再映射，完全越界或非有限框返回空。 */
    std::optional<Rectangle> mapBox(const DisplayBox &box) const;
};
/** @brief 保持宽高比居中，尺寸和起点向内对齐；不依赖相机固定尺寸。 */
VideoGeometry fitVideo(int source_width, int source_height, int canvas_width, int canvas_height, int alignment = 2);
} // namespace osd
#endif // OSD_GEOMETRY_H
