#ifndef OSD_CONFIG_H
#define OSD_CONFIG_H

#include <string>

namespace osd {
/** @brief HDMI 启动配置，运行中不可更改。 */
struct DisplayConfig {
    bool timing_enabled{false};
    std::string drm_device{"/dev/dri/card0"};
    std::string connector{"HDMI-A-1"};
    int width{720}, height{480};
    double refresh_hz{60};
    std::string craft_name{"FPV"};
};
} // namespace osd
#endif // OSD_CONFIG_H
