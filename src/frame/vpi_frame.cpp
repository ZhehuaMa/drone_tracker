#include "frame/vpi_frame.h"

#include <iostream>
#include <utility>

#include <vpi/OpenCVInterop.hpp>

struct VpiFrameState {
  VpiFrameState(VPIImage owned_image, int image_width, int image_height,
                FramePixelFormat image_format, std::uint64_t timestamp_us,
                cv::Mat owned_mat = {}) noexcept
      : image{owned_image}, width{image_width}, height{image_height},
        pixel_format{image_format}, timestamp{timestamp_us},
        backing_mat{std::move(owned_mat)} {}

  ~VpiFrameState() noexcept {
    if (image != nullptr) {
      vpiImageDestroy(image);
      image = nullptr;
    }
  }

  VPIImage image{nullptr};
  int width{0};
  int height{0};
  FramePixelFormat pixel_format{FramePixelFormat::Gray8};
  std::uint64_t timestamp{0};
  cv::Mat backing_mat;
  std::mutex access_mutex;
};

namespace {

bool toFramePixelFormat(VPIImageFormat vpi_format,
                        FramePixelFormat &frame_format) noexcept {
  if (vpi_format == VPI_IMAGE_FORMAT_U8) {
    frame_format = FramePixelFormat::Gray8;
    return true;
  }
  if (vpi_format == VPI_IMAGE_FORMAT_BGR8) {
    frame_format = FramePixelFormat::Bgr8;
    return true;
  }
  return false;
}

class VpiFrameReadView final : public FrameReadView {
public:
  VpiFrameReadView(std::shared_ptr<VpiFrameState> state,
                   std::unique_lock<std::mutex> access_lock,
                   VPIImageData image_data, cv::Mat mat) noexcept
      : state_{std::move(state)}, access_lock_{std::move(access_lock)},
        image_data_{image_data}, mat_{std::move(mat)} {}

  ~VpiFrameReadView() override {
    if (locked_) {
      const VPIStatus status = vpiImageUnlock(state_->image);
      if (status != VPI_SUCCESS) {
        std::cerr << "Failed to unlock VPI frame read view: "
                  << vpiStatusGetName(status) << std::endl;
      }
      locked_ = false;
    }
  }

  int width() const noexcept override { return state_->width; }

  int height() const noexcept override { return state_->height; }

  FramePixelFormat pixelFormat() const noexcept override {
    return state_->pixel_format;
  }

  std::size_t rowStrideBytes() const noexcept override { return mat_.step[0]; }

  const cv::Mat &mat() const noexcept override { return mat_; }

private:
  std::shared_ptr<VpiFrameState> state_;
  std::unique_lock<std::mutex> access_lock_;
  VPIImageData image_data_{};
  cv::Mat mat_;
  bool locked_{true};
};

} // namespace

VpiFrameReadAccess::VpiFrameReadAccess(std::shared_ptr<VpiFrameState> state,
                                       std::unique_lock<std::mutex> access_lock,
                                       VPIImage image) noexcept
    : state_{std::move(state)}, access_lock_{std::move(access_lock)},
      image_{image} {}

VPIImage VpiFrameReadAccess::image() const noexcept { return image_; }

std::shared_ptr<const VpiFrame>
VpiFrame::takeOwnership(VPIImage image, std::uint64_t timestamp_us) noexcept {
  if (image == nullptr) {
    std::cerr << "Cannot create VpiFrame from a null VPIImage" << std::endl;
    return nullptr;
  }

  int width = 0;
  int height = 0;
  VPIImageFormat vpi_format = VPI_IMAGE_FORMAT_INVALID;
  const VPIStatus size_status = vpiImageGetSize(image, &width, &height);
  const VPIStatus format_status = vpiImageGetFormat(image, &vpi_format);
  FramePixelFormat frame_format{FramePixelFormat::Gray8};
  if (size_status != VPI_SUCCESS || format_status != VPI_SUCCESS ||
      width <= 0 || height <= 0) {
    std::cerr
        << "Cannot create VpiFrame: failed to query valid VPI image metadata"
        << std::endl;
    vpiImageDestroy(image);
    return nullptr;
  }
  if (!toFramePixelFormat(vpi_format, frame_format)) {
    std::cerr << "Cannot create VpiFrame: unsupported VPI image format"
              << std::endl;
    vpiImageDestroy(image);
    return nullptr;
  }

  std::shared_ptr<VpiFrameState> state;
  try {
    state = std::make_shared<VpiFrameState>(image, width, height, frame_format,
                                            timestamp_us);
    return std::shared_ptr<const VpiFrame>{new VpiFrame{state}};
  } catch (const std::exception &error) {
    if (!state) {
      vpiImageDestroy(image);
    }
    std::cerr << "Cannot create VpiFrame: " << error.what() << std::endl;
    return nullptr;
  }
}

std::shared_ptr<const VpiFrame>
VpiFrame::copyFrom(const cv::Mat &mat, std::uint64_t timestamp_us) noexcept {
  if (mat.empty()) {
    std::cerr << "Cannot create VpiFrame from an empty cv::Mat" << std::endl;
    return nullptr;
  }

  VPIImageFormat vpi_format = VPI_IMAGE_FORMAT_INVALID;
  FramePixelFormat frame_format{FramePixelFormat::Gray8};
  if (mat.type() == CV_8UC1) {
    vpi_format = VPI_IMAGE_FORMAT_U8;
    frame_format = FramePixelFormat::Gray8;
  } else if (mat.type() == CV_8UC3) {
    vpi_format = VPI_IMAGE_FORMAT_BGR8;
    frame_format = FramePixelFormat::Bgr8;
  } else {
    std::cerr
        << "Cannot create VpiFrame: cv::Mat format must be CV_8UC1 or CV_8UC3"
        << std::endl;
    return nullptr;
  }

  VPIImage image = nullptr;
  std::shared_ptr<VpiFrameState> state;
  try {
    cv::Mat backing_mat = mat.clone();
    const VPIStatus status =
        vpiImageCreateWrapperOpenCVMat(backing_mat, vpi_format, 0, &image);
    if (status != VPI_SUCCESS) {
      std::cerr << "Cannot create VpiFrame OpenCV wrapper: "
                << vpiStatusGetName(status) << std::endl;
      return nullptr;
    }

    state = std::make_shared<VpiFrameState>(
        image, backing_mat.cols, backing_mat.rows, frame_format, timestamp_us,
        std::move(backing_mat));
    return std::shared_ptr<const VpiFrame>{new VpiFrame{state}};
  } catch (const std::exception &error) {
    if (!state && image != nullptr) {
      vpiImageDestroy(image);
    }
    std::cerr << "Cannot copy cv::Mat into VpiFrame: " << error.what()
              << std::endl;
    return nullptr;
  }
}

VpiFrame::VpiFrame(std::shared_ptr<VpiFrameState> state) noexcept
    : state_{std::move(state)} {}

int VpiFrame::width() const noexcept { return state_->width; }

int VpiFrame::height() const noexcept { return state_->height; }

FramePixelFormat VpiFrame::pixelFormat() const noexcept {
  return state_->pixel_format;
}

std::uint64_t VpiFrame::timestampUs() const noexcept {
  return state_->timestamp;
}

std::unique_ptr<FrameReadView> VpiFrame::mapRead() const {
  std::unique_lock<std::mutex> access_lock{state_->access_mutex};
  VPIImageData image_data{};
  const VPIStatus lock_status =
      vpiImageLockData(state_->image, VPI_LOCK_READ,
                       VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &image_data);
  if (lock_status != VPI_SUCCESS) {
    std::cerr << "Failed to map VpiFrame for host reading: "
              << vpiStatusGetName(lock_status) << std::endl;
    return nullptr;
  }

  try {
    cv::Mat mat;
    const VPIStatus export_status =
        vpiImageDataExportOpenCVMat(image_data, &mat);
    if (export_status != VPI_SUCCESS) {
      std::cerr << "Failed to export VpiFrame host mapping: "
                << vpiStatusGetName(export_status) << std::endl;
      vpiImageUnlock(state_->image);
      return nullptr;
    }
    return std::make_unique<VpiFrameReadView>(state_, std::move(access_lock),
                                              image_data, std::move(mat));
  } catch (const std::exception &error) {
    vpiImageUnlock(state_->image);
    std::cerr << "Failed to create VpiFrame host read view: " << error.what()
              << std::endl;
    return nullptr;
  }
}

VpiFrameReadAccess VpiFrame::acquireVpiRead() const {
  std::unique_lock<std::mutex> access_lock{state_->access_mutex};
  return VpiFrameReadAccess{state_, std::move(access_lock), state_->image};
}
