#include "preprocess_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

PreprocessResult<LetterboxTransform> calculateLetterboxTransform(const cv::Size &source, const cv::Size &model) {
    using Result = PreprocessResult<LetterboxTransform>;
    if (source.width <= 0 || source.height <= 0 || model.width <= 0 || model.height <= 0) {
        return Result::failure(PreprocessError::InvalidRegion, "源区域与模型尺寸必须为正数");
    }
    const float scale{
        std::min(static_cast<float>(model.height) / source.height, static_cast<float>(model.width) / source.width)};
    // 保留源实现的 float 乘法及最接近整数舍入，坐标还原使用实际整数尺寸。
    const auto rounded_width{std::llround(source.width * scale)};
    const auto rounded_height{std::llround(source.height * scale)};
    const int width{static_cast<int>(std::min<long long>(rounded_width, model.width))};
    const int height{static_cast<int>(std::min<long long>(rounded_height, model.height))};
    if (width <= 0 || height <= 0) {
        return Result::failure(PreprocessError::InvalidRegion, "缩放结果退化为零尺寸");
    }
    const int pad_width{model.width - width};
    const int pad_height{model.height - height};
    return {LetterboxTransform{{width, height},
                               pad_width / 2,
                               pad_height / 2,
                               pad_width - pad_width / 2,
                               pad_height - pad_height / 2,
                               width / static_cast<float>(source.width),
                               height / static_cast<float>(source.height)}};
}

PreprocessResult<NanoCropGeometry> calculateNanoCropGeometry(const cv::Size &source, const cv::Point2f &position,
                                                             int original_size) {
    using Result = PreprocessResult<NanoCropGeometry>;
    if (source.width <= 0 || source.height <= 0 || original_size <= 0 || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return Result::failure(PreprocessError::InvalidRegion, "帧尺寸、裁剪尺寸或中心无效");
    }
    const float half{(static_cast<float>(original_size) + 1) / 2.0f};
    const double x{std::floor(position.x - half + 0.5f)};
    const double y{std::floor(position.y - half + 0.5f)};
    constexpr auto limit{std::numeric_limits<int>::max()};
    if (x < -static_cast<double>(limit) || x > limit || y < -static_cast<double>(limit) || y > limit) {
        return Result::failure(PreprocessError::InvalidRegion, "裁剪坐标超出整数范围");
    }
    const auto left{std::max<std::int64_t>(0, -static_cast<std::int64_t>(x))};
    const auto top{std::max<std::int64_t>(0, -static_cast<std::int64_t>(y))};
    const auto right{std::max<std::int64_t>(0, static_cast<std::int64_t>(x) + original_size - source.width)};
    const auto bottom{std::max<std::int64_t>(0, static_cast<std::int64_t>(y) + original_size - source.height)};
    if (source.width + left + right > limit || source.height + top + bottom > limit) {
        return Result::failure(PreprocessError::InvalidRegion, "补齐画布尺寸超出整数范围");
    }
    return {NanoCropGeometry{{static_cast<int>(x), static_cast<int>(y), original_size, original_size},
                             static_cast<int>(left),
                             static_cast<int>(top),
                             static_cast<int>(right),
                             static_cast<int>(bottom)}};
}
