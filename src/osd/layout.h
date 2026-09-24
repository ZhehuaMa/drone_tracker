#ifndef OSD_LAYOUT_H
#define OSD_LAYOUT_H

#include <chrono>
#include <memory>
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
/** @brief 缓存画布的可观测工作量，用于验证稳定帧不重复分配或栅格化。 */
struct LayoutWork {
    std::uint64_t rebuilds{}, glyphs{}, fields{}, dirty_pixels{};
};
/** @brief 单渲染线程持有的覆盖层；返回的画布仅借用到下一次 draw。 */
class CachedOsdLayout {
public:
    explicit CachedOsdLayout(std::string craft_name = "FPV");
    ~CachedOsdLayout();
    CachedOsdLayout(const CachedOsdLayout &) = delete;
    CachedOsdLayout &operator=(const CachedOsdLayout &) = delete;
    void setCraftName(std::string craft_name);
    const cv::Mat &draw(const PresentationState &state, const VideoGeometry &geometry,
                        int width, int height, std::chrono::system_clock::time_point wall_time);
    LayoutWork work() const;
private:
    struct Cache;
    std::unique_ptr<Cache> cache_;
};
} // namespace osd
#endif // OSD_LAYOUT_H
