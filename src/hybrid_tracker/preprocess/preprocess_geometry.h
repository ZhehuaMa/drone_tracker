#ifndef PREPROCESS_GEOMETRY_H
#define PREPROCESS_GEOMETRY_H

#include "model_input.h"

#include <opencv2/core/types.hpp>

/** @brief 按值保存 letterbox 的缩放尺寸、四侧填充及坐标还原比例。 */
struct LetterboxTransform {
    cv::Size resized{};
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    float scale_x{1.0f};
    float scale_y{1.0f};
};

/** @brief 按值保存原图坐标中的正方形裁剪窗及补齐画布所需边距。 */
struct NanoCropGeometry {
    cv::Rect crop{};
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
};

/** @brief 借用尺寸计算保持宽高比的变换；无效尺寸返回 InvalidRegion。 */
PreprocessResult<LetterboxTransform> calculateLetterboxTransform(const cv::Size &source, const cv::Size &model);
/** @brief 借用几何参数计算裁剪与填充；不访问像素，无效或溢出输入返回 InvalidRegion。 */
PreprocessResult<NanoCropGeometry> calculateNanoCropGeometry(const cv::Size &source, const cv::Point2f &position,
                                                             int original_size);

#endif // PREPROCESS_GEOMETRY_H
