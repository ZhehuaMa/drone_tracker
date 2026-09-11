#ifndef PREPROCESS_INPUT_VALIDATION_H
#define PREPROCESS_INPUT_VALIDATION_H

#include "image_preprocessor.h"

/** @brief 检查借用的帧元数据；不访问或持有像素。 */
inline PreprocessError validatePreprocessFrame(const FrameRef &frame) noexcept {
    if (!frame || frame->width() <= 0 || frame->height() <= 0) {
        return PreprocessError::EmptyFrame;
    }
    return frame->pixelFormat() == FramePixelFormat::Gray8 ? PreprocessError::None : PreprocessError::UnsupportedFormat;
}

/** @brief 检查当前灰度三通道模型契约；size=0 允许任意正的 YOLO 输入尺寸。 */
inline bool matchesImageContract(const ModelInputContract &c, float scale, int size = 0) noexcept {
    return c.element_type == TensorElementType::Float32 && c.layout == TensorLayout::Nchw && c.shape.size() == 4 &&
           c.shape[0] == 1 && c.shape[1] == 3 && c.shape[2] > 0 && c.shape[3] > 0 && c.scale == scale &&
           c.offset == 0.0f && (size == 0 || (c.shape[2] == size && c.shape[3] == size));
}

/** @brief YOLO 仅接受已定义的浮点 NCHW 或固定 640 方形字节 NHWC 提交格式。 */
inline bool matchesYoloContract(const ModelInputContract &c) noexcept {
    return matchesImageContract(c, 1.0f / 255.0f) ||
           (c.element_type == TensorElementType::Uint8 && c.layout == TensorLayout::Nhwc &&
            c.shape.size() == 4 && c.shape[0] == 1 && c.shape[1] == 640 && c.shape[2] == 640 &&
            c.shape[3] == 3 && c.scale == 1.0f && c.offset == 0.0f);
}

/** @brief 校验后按实际布局返回宽高；失败不提供可用尺寸。 */
inline PreprocessResult<cv::Size> yoloInputSize(const ModelInputContract &c) {
    if (!matchesYoloContract(c)) {
        return PreprocessResult<cv::Size>::failure(PreprocessError::ContractMismatch, "Invalid YOLO input contract");
    }
    return {c.layout == TensorLayout::Nhwc ? cv::Size{c.shape[2], c.shape[1]} : cv::Size{c.shape[3], c.shape[2]}};
}

/** @brief 解析借用区域；仅默认空矩形代表全图，使用减法避免坐标加法溢出。 */
inline PreprocessResult<cv::Rect> resolveYoloRegion(const cv::Size &size, const cv::Rect &region) {
    const cv::Rect resolved{region == cv::Rect{} ? cv::Rect{0, 0, size.width, size.height} : region};
    if (resolved.x < 0 || resolved.y < 0 || resolved.width <= 0 || resolved.height <= 0 || resolved.x > size.width ||
        resolved.y > size.height || resolved.width > size.width - resolved.x ||
        resolved.height > size.height - resolved.y) {
        return PreprocessResult<cv::Rect>::failure(PreprocessError::InvalidRegion, "区域必须完全位于帧内");
    }
    return {resolved};
}

#endif // PREPROCESS_INPUT_VALIDATION_H
