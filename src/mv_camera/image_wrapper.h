#pragma once
#include <vpi/VPI.h>
#include <vpi/OpenCVInterop.hpp>

class VPIImageWrapper {
public:
    VPIImageWrapper() = default;
    VPIImageWrapper(VPIImage img, uint64_t ts = 0) : vpi_image(img), timestamp(ts) {}
    VPIImageWrapper(const cv::Mat& mat, uint64_t ts = 0) : timestamp(ts) {
        VPIImageFormat format = VPI_IMAGE_FORMAT_INVALID;
        if (mat.type() == CV_8UC3) {
            format = VPI_IMAGE_FORMAT_BGR8;
        } else if (mat.type() == CV_8UC1) {
            format = VPI_IMAGE_FORMAT_U8;
        } else {
            throw std::runtime_error("Unsupported cv::Mat type for VPIImageWrapper");
        }
        cv_mat = mat.clone();
        VPIStatus status = vpiImageCreateWrapperOpenCVMat(cv_mat, format, 0, &vpi_image);
        if (status != VPI_SUCCESS) {
            throw std::runtime_error("Failed to create VPI image from cv::Mat");
        }
    }
    ~VPIImageWrapper() {
        if (vpi_image) {
            vpiImageDestroy(vpi_image);
            vpi_image = nullptr;
        }
    }
    VPIImage get() const { return vpi_image; }
    uint64_t get_timestamps() const { return timestamp; }
private:
    VPIImage vpi_image = nullptr;
    cv::Mat cv_mat;
    uint64_t timestamp = 0;
};
