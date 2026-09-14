#ifndef RK3576_HOST_FRAME_H
#define RK3576_HOST_FRAME_H

#include "frame/frame.h"

namespace rk3576 {
/** @brief 深拷贝并保留 Gray8/BGR8 像素格式。 */
FrameRef copyHostFrame(const cv::Mat &image, std::uint64_t timestamp_us = 0);
/** @brief 后端私有真实存储类型检查，不依赖工厂身份。 */
bool isHostGrayFrame(const FrameRef &frame) noexcept;
/** @brief 复制 Gray8 图像创建自有只读帧；彩色转换必须由工具调用方显式完成。 */
FrameRef copyGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us = 0);
} // namespace rk3576
#endif // RK3576_HOST_FRAME_H
