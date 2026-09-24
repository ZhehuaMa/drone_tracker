#ifndef RK3576_OSD_HDMI_BACKEND_H
#define RK3576_OSD_HDMI_BACKEND_H

#include <memory>

#include "osd/config.h"
#include "osd/presentation.h"

namespace rk3576 {
/** @brief 建立本仓库维护的 RGA/DRM 后端；设备在渲染线程 initialize 时打开。 */
std::unique_ptr<osd::DisplayBackend> createHdmiBackend(osd::DisplayConfig config);
/** @brief 测试用已确认扫描缓冲快照；Mat 仅在回调执行期间有效，不可保存借用。 */
struct HdmiInspection {
    cv::Mat primary{}, overlay{};
    int primary_index{}, overlay_index{};
    std::uint64_t generation{}, video_operations{}, repacked_frames{}, background_initializations{};
};
using HdmiInspector = std::function<void(const HdmiInspection &)>;
/** @brief 验证工具入口，正式节点不安装检查回调。 */
std::unique_ptr<osd::DisplayBackend> createInspectedHdmiBackend(osd::DisplayConfig config, HdmiInspector inspector);
} // namespace rk3576
#endif // RK3576_OSD_HDMI_BACKEND_H
