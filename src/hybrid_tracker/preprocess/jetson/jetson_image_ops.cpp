#include "jetson_image_ops.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vpi/algo/ConvertImageFormat.h>

namespace jetson_preprocess {
void check(VPIStatus status, const char *operation) {
    if (status != VPI_SUCCESS) {
        throw std::runtime_error(std::string{operation} + ": " + vpiStatusGetName(status));
    }
}
Image::~Image() {
    if (image_) {
        vpiImageDestroy(image_);
    }
}
void Image::ensure(int width, int height, VPIImageFormat format) {
    if (image_ && width_ == width && height_ == height && format_ == format) {
        return;
    }
    if (image_) {
        vpiImageDestroy(image_);
        image_ = nullptr;
    }
    check(vpiImageCreate(width, height, format, flags, &image_), "create image");
    width_ = width;
    height_ = height;
    format_ = format;
}
Stream::Stream() { check(vpiStreamCreate(0, &stream_), "create stream"); }
Stream::~Stream() {
    if (stream_) {
        vpiStreamDestroy(stream_);
    }
}
void Stream::sync() {
    const auto status{vpiStreamSync(stream_)};
    if (status != VPI_SUCCESS) {
        // destroy 隐式同步；在 view 和源帧租约释放前结束失败 stream。
        vpiStreamDestroy(stream_);
        stream_ = nullptr;
        check(status, "synchronize image operations");
    }
}
void Stream::drain() noexcept {
    if (stream_ && vpiStreamSync(stream_) != VPI_SUCCESS) {
        vpiStreamDestroy(stream_);
        stream_ = nullptr;
    }
}
Batch::~Batch() { stream_.drain(); }
VPIImage Batch::view(VPIImage image, const cv::Rect &rect) {
    auto view{std::make_unique<Image>()};
    const VPIRectangleI roi{rect.x, rect.y, rect.width, rect.height};
    check(vpiImageCreateView(image, &roi, flags, view->address()), "create view");
    const auto handle{view->get()};
    views_.push_back(std::move(view));
    return handle;
}
void Batch::sync() { stream_.sync(); }
void fillGray(Image &image, int width, int height, unsigned char value) {
    image.ensure(width, height, VPI_IMAGE_FORMAT_U8);
    VPIImageData data{};
    check(vpiImageLockData(image.get(), VPI_LOCK_WRITE, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &data), "lock fill image");
    auto *row{static_cast<unsigned char *>(data.buffer.pitch.planes[0].data)};
    for (int y{0}; y < height; ++y) {
        std::memset(row + static_cast<std::size_t>(y) * data.buffer.pitch.planes[0].pitchBytes, value, width);
    }
    check(vpiImageUnlock(image.get()), "unlock fill image");
}
void convert(VPIStream stream, VPIImage source, VPIImage destination, float scale) {
    VPIConvertImageFormatParams params{};
    params.policy = VPI_CONVERSION_CLAMP;
    params.scale = scale;
    params.offset = 0.0f;
    params.chromaUpFilter = VPI_INTERP_NEAREST;
    params.chromaDownFilter = VPI_INTERP_NEAREST;
    check(vpiSubmitConvertImageFormat(stream, VPI_BACKEND_CUDA, source, destination, &params), "convert image");
}
void fillBands(Batch &batch, VPIStream stream, VPIImage fill, VPIImage canvas, const cv::Size &size,
               const cv::Rect &content, float scale) {
    // 四条边带彼此不相交，也不覆盖内容；保留 VPI 3.2 的重叠写规避规则。
    const std::array<cv::Rect, 4> bands{
        {{0, 0, size.width, content.y},
         {0, content.y + content.height, size.width, size.height - content.y - content.height},
         {0, content.y, content.x, content.height},
         {content.x + content.width, content.y, size.width - content.x - content.width, content.height}}};
    for (const auto &band : bands) {
        if (band.width > 0 && band.height > 0) {
            const auto source{batch.view(fill, band)};
            const auto destination{batch.view(canvas, band)};
            convert(stream, source, destination, scale);
        }
    }
}
void copyPlanes(VPIImage image, int width, int height, TensorBuffer &buffer) {
    VPIImageData data{};
    check(vpiImageLockData(image, VPI_LOCK_READ, VPI_IMAGE_BUFFER_CUDA_PITCH_LINEAR, &data), "lock float image");
    const std::size_t row_bytes{static_cast<std::size_t>(width) * sizeof(float)};
    const std::size_t plane_size{static_cast<std::size_t>(width) * height};
    cudaError_t status{cudaSuccess};
    for (int channel{0}; channel < 3 && status == cudaSuccess; ++channel) {
        status = cudaMemcpy2DAsync(static_cast<float *>(buffer.device_ptr) + plane_size * channel, row_bytes,
                                   data.buffer.pitch.planes[0].data, data.buffer.pitch.planes[0].pitchBytes, row_bytes,
                                   height, cudaMemcpyDeviceToDevice);
    }
    // 即使部分拷贝失败，也必须等待已提交的拷贝完成后再解锁图像。
    const auto sync_status{cudaStreamSynchronize(0)};
    const auto unlock_status{vpiImageUnlock(image)};
    if (status != cudaSuccess || sync_status != cudaSuccess) {
        throw std::runtime_error("Failed to copy or synchronize input planes");
    }
    check(unlock_status, "unlock float image");
}
} // namespace jetson_preprocess
