#ifndef RK3576_HOST_FRAME_H
#define RK3576_HOST_FRAME_H

#include "frame/frame.h"

namespace rk3576 {
/** @brief 复制 Gray8 图像创建自有只读帧；彩色转换必须由工具调用方显式完成。 */
FrameRef copyGrayFrame(const cv::Mat &gray, std::uint64_t timestamp_us = 0);
} // namespace rk3576
#endif // RK3576_HOST_FRAME_H
