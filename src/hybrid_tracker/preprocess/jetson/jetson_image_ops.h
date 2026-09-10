#ifndef JETSON_IMAGE_OPS_H
#define JETSON_IMAGE_OPS_H

#include <memory>
#include <vector>
#include <vpi/VPI.h>

#include "trt_engine.h"
#include <opencv2/core/types.hpp>

namespace jetson_preprocess {
constexpr int flags{VPI_BACKEND_CUDA | VPI_BACKEND_CPU};
/** @brief 内部独占图像句柄，创建失败不会留下悬空成员。 */
class Image {
  public:
    Image() = default;
    ~Image();
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    void ensure(int width, int height, VPIImageFormat format);
    VPIImage get() const noexcept { return image_; }
    VPIImage *address() noexcept { return &image_; }

  private:
    VPIImage image_{nullptr};
    int width_{0};
    int height_{0};
    VPIImageFormat format_{};
};
class Stream {
  public:
    Stream();
    ~Stream();
    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    VPIStream get() const noexcept { return stream_; }
    void sync();
    void drain() noexcept;

  private:
    VPIStream stream_{nullptr};
};
/** @brief 持有本批 view，所有正常和异常路径都先同步再销毁 view。源帧租约必须先于本对象声明。 */
class Batch {
  public:
    explicit Batch(Stream &stream) : stream_{stream} {}
    ~Batch();
    VPIImage view(VPIImage image, const cv::Rect &rect);
    void sync();

  private:
    Stream &stream_;
    std::vector<std::unique_ptr<Image>> views_{};
};
void check(VPIStatus status, const char *operation);
void fillGray(Image &image, int width, int height, unsigned char value);
void convert(VPIStream stream, VPIImage source, VPIImage destination, float scale);
void fillBands(Batch &batch, VPIStream stream, VPIImage fill, VPIImage canvas, const cv::Size &canvas_size,
               const cv::Rect &content, float scale);
void copyPlanes(VPIImage image, int width, int height, TensorBuffer &buffer);
} // namespace jetson_preprocess

#endif // JETSON_IMAGE_OPS_H
