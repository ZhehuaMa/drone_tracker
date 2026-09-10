#include "preprocess/image_preprocessor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void checkLetterbox(const cv::Size &source, const cv::Size &model, const cv::Size &resized, int left, int top,
                    int right, int bottom) {
    const auto result{calculateLetterboxTransform(source, model)};
    require(static_cast<bool>(result), "letterbox 意外失败");
    const auto &t{result.value};
    require(t.resized == resized, "letterbox 舍入尺寸错误");
    require(t.left == left && t.top == top && t.right == right && t.bottom == bottom, "letterbox 边距错误");
    require(t.resized.width + t.left + t.right == model.width, "横向未填满模型尺寸");
    require(t.resized.height + t.top + t.bottom == model.height, "纵向未填满模型尺寸");
    require(std::abs(t.scale_x - resized.width / static_cast<float>(source.width)) < 1e-7f, "横向实际比例错误");
    require(std::abs(t.scale_y - resized.height / static_cast<float>(source.height)) < 1e-7f, "纵向实际比例错误");
}

void checkCrop(const cv::Point2f &position, int size, const cv::Rect &crop, int left, int top, int right, int bottom) {
    const auto result{calculateNanoCropGeometry({100, 80}, position, size)};
    require(static_cast<bool>(result), "裁剪几何意外失败");
    const auto &g{result.value};
    require(g.crop == crop, "裁剪舍入公式错误");
    require(g.left == left && g.top == top && g.right == right && g.bottom == bottom, "裁剪边距错误");
}
} // namespace

int main() {
    checkLetterbox({640, 640}, {640, 640}, {640, 640}, 0, 0, 0, 0);
    checkLetterbox({1920, 1080}, {640, 640}, {640, 360}, 0, 140, 0, 140);
    checkLetterbox({1080, 1920}, {640, 640}, {360, 640}, 140, 0, 140, 0);
    checkLetterbox({5, 3}, {8, 8}, {8, 5}, 0, 1, 0, 2);
    checkLetterbox({3, 5}, {8, 8}, {5, 8}, 1, 0, 2, 0);
    checkLetterbox({1536, 864}, {640, 384}, {640, 360}, 0, 12, 0, 12);
    checkLetterbox({7, 5}, {7, 5}, {7, 5}, 0, 0, 0, 0);
    require(!calculateLetterboxTransform({0, 4}, {8, 8}), "接受了零宽度");
    require(!calculateLetterboxTransform({4, -1}, {8, 8}), "接受了负高度");
    require(!calculateLetterboxTransform({4, 4}, {0, 8}), "接受了零模型宽度");
    require(!calculateLetterboxTransform({4, 4}, {8, -1}), "接受了负模型高度");
    require(!calculateLetterboxTransform({1, 10000}, {1, 1}), "接受了退化的缩放尺寸");

    checkCrop({50, 40}, 10, {45, 35, 10, 10}, 0, 0, 0, 0);
    checkCrop({50, 40}, 9, {45, 35, 9, 9}, 0, 0, 0, 0);
    checkCrop({50.5f, 40.5f}, 9, {46, 36, 9, 9}, 0, 0, 0, 0);
    checkCrop({0, 40}, 10, {-5, 35, 10, 10}, 5, 0, 0, 0);
    checkCrop({50, 0}, 10, {45, -5, 10, 10}, 0, 5, 0, 0);
    checkCrop({100, 40}, 10, {95, 35, 10, 10}, 0, 0, 5, 0);
    checkCrop({50, 80}, 10, {45, 75, 10, 10}, 0, 0, 0, 5);
    checkCrop({0, 0}, 10, {-5, -5, 10, 10}, 5, 5, 0, 0);
    checkCrop({50, 40}, 200, {-50, -60, 200, 200}, 50, 60, 50, 60);
    require(!calculateNanoCropGeometry({0, 80}, {5, 5}, 10), "接受了空图像");
    require(!calculateNanoCropGeometry({100, 80}, {5, 5}, 0), "接受了零裁剪尺寸");
    require(!calculateNanoCropGeometry({100, 80}, {5, 5}, -1), "接受了负裁剪尺寸");
    require(!calculateNanoCropGeometry({100, 80}, {std::numeric_limits<float>::infinity(), 0}, 10), "接受了无穷中心");
    require(!calculateNanoCropGeometry({100, 80}, {std::numeric_limits<float>::quiet_NaN(), 0}, 10), "接受了 NaN 中心");
    require(!calculateNanoCropGeometry({100, 80}, {1e30f, 0}, 10), "接受了溢出中心");
    require(!calculateNanoCropGeometry({std::numeric_limits<int>::max(), 80}, {0, 0}, 10), "接受了溢出画布");

    const PreprocessError errors[]{
        PreprocessError::None,          PreprocessError::EmptyFrame,       PreprocessError::UnsupportedFormat,
        PreprocessError::InvalidRegion, PreprocessError::ContractMismatch, PreprocessError::PlatformTypeMismatch,
        PreprocessError::InputBusy,     PreprocessError::PlatformFailure};
    const char *names[]{"None",          "EmptyFrame",       "UnsupportedFormat",
                        "InvalidRegion", "ContractMismatch", "PlatformTypeMismatch",
                        "InputBusy",     "PlatformFailure"};
    for (std::size_t i{0}; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        require(std::string{preprocessErrorName(errors[i])} == names[i], "错误码名称发生变化");
    }
    std::cout << "公共预处理几何和错误名称测试通过\n";
}
