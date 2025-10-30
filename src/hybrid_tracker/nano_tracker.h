#ifndef NANOTRACKER_H
#define NANOTRACKER_H

#include <opencv2/opencv.hpp>
#include <memory>
#include "trt_engine.h"
#include "image_wrapper.h"

#include <vpi/VPI.h>

class NanoTracker {
public:
    NanoTracker(const std::string& backbone_path, const std::string& head_path);
    ~NanoTracker();
    void init(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& bbox);
    float update(const std::shared_ptr<VPIImageWrapper>& frame, cv::Rect2f& output_bbox);

private:
    // Parameters
    const int score_size = 15;
    const int min_wh = 5;
    const float context_ratio = 0.5f;
    const int template_size = 127;
    const int instance_size = 255;
    const float window_influence = 0.455f;
    const float track_lr = 0.348f;
    const float track_penalty_k = 0.138f;
    const int stride = 16;
    
    // State
    cv::Point2f center_pos;
    cv::Point2f size;
    cv::Scalar channel_average;
    TensorBuffer z_f; // template features (now a TensorBuffer)
    cv::Mat window;
    cv::Mat points;

    VPIStream vpi_stream = nullptr;
    VPIImage vpi_padded_image = nullptr;
    std::unordered_map<int, VPIImage> vpi_model_images;

    // Input buffers for inference
    TensorBuffer z_crop_buffer;
    TensorBuffer x_crop_buffer;

    // Inference engines
    std::unique_ptr<TrtEngine> session_backbone;
    std::unique_ptr<TrtEngine> session_head;

    // Helper functions
    void generate_points();
    cv::Size get_subwindow_buffer(VPIImage vpiIm, const cv::Point2f& pos, int model_sz, int original_sz, TensorBuffer& buffer);
    void update_padded_image(int new_width, int new_height, int flags);
    cv::Mat convert_bbox(const cv::Mat& delta, const cv::Mat& points);
    cv::Rect2f bbox_clip(const cv::Point2f& center, const cv::Point2f& size, const cv::Size& boundary);
};

#endif // NANOTRACKER_H
