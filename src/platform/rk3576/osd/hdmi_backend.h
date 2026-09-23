#ifndef RK3576_OSD_HDMI_BACKEND_H
#define RK3576_OSD_HDMI_BACKEND_H

#include <memory>

#include "osd/config.h"
#include "osd/presentation.h"

namespace rk3576 {
/** @brief 建立本仓库维护的 RGA/DRM 后端；设备在渲染线程 initialize 时打开。 */
std::unique_ptr<osd::DisplayBackend> createHdmiBackend(osd::DisplayConfig config);
} // namespace rk3576
#endif // RK3576_OSD_HDMI_BACKEND_H
