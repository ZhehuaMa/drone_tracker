#ifndef OSD_LAYOUT_H
#define OSD_LAYOUT_H

#include <chrono>
#include <string>

#include "osd/geometry.h"

namespace osd {
/** @brief 两个独立的 BGRA 画布：信息层合入视频，目标层供显示控制器叠加。 */
struct DisplayLayers {
    cv::Mat hud{};
    cv::Mat target{};
};
/** @brief 使用固定布局绘制快照。每次生成完整独立画布，清框不会擦掉遥测。 */
class OsdLayout {
public:
    explicit OsdLayout(std::string craft_name = "FPV");
    DisplayLayers draw(const PresentationState &state, const VideoGeometry &geometry,
                       int width, int height, std::chrono::system_clock::time_point wall_time) const;
    static std::u32string statusText(DisplayStatus status, bool has_video);
private:
    std::string craft_name_;
};
} // namespace osd
#endif // OSD_LAYOUT_H
