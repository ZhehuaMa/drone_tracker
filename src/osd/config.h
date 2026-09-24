#ifndef OSD_CONFIG_H
#define OSD_CONFIG_H

#include <stdexcept>
#include <string>

namespace osd {
/** @brief 启动时选择的显示实现；运行中不重建设备资源。 */
enum class RenderPath { CachedOverlay, Legacy };
inline RenderPath parseRenderPath(const std::string &value) {
    if (value == "cached-overlay") { return RenderPath::CachedOverlay; }
    if (value == "legacy") { return RenderPath::Legacy; }
    throw std::invalid_argument("osd.render_path must be cached-overlay or legacy: " + value);
}

/** @brief HDMI 启动配置，运行中不可更改。 */
struct DisplayConfig {
    RenderPath render_path{RenderPath::CachedOverlay};
    bool timing_enabled{false};
    std::string drm_device{"/dev/dri/card0"};
    std::string connector{"HDMI-A-1"};
    int width{720}, height{480};
    double refresh_hz{60};
    std::string craft_name{"FPV"};
};
} // namespace osd
#endif // OSD_CONFIG_H
