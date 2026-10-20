#ifndef RK3576_HOST_FRAME_H
#define RK3576_HOST_FRAME_H

#include "frame/frame.h"

namespace rk3576 {
/** @brief 深拷贝并保留 Gray8/BGR8 像素格式。 */
FrameRef copyHostFrame(const cv::Mat &image, std::uint64_t timestamp_us = 0, diagnostics::FrameIdentity identity = {});
/** @brief 后端私有真实存储类型检查，不依赖工厂身份。 */
bool isHostGrayFrame(const FrameRef &frame) noexcept;
/** @brief 复制 Gray8 图像创建自有只读帧；彩色转换必须由工具调用方显式完成。 */
FrameRef copyGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us = 0, diagnostics::FrameIdentity identity = {});
/**
 * @brief Retain reference-counted Gray8 storage without copying its pixels.
 * The caller must keep every alias immutable while a frame or read view survives.
 * Borrowed external buffers are rejected because their lifetime cannot be retained.
 */
FrameRef retainGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us = 0,
                         diagnostics::FrameIdentity identity = {});
} // namespace rk3576
#endif // RK3576_HOST_FRAME_H
