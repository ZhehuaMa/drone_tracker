#ifndef NANOTRACKER_H
#define NANOTRACKER_H

#include <opencv2/opencv.hpp>
#include <memory>
#include "preprocess/image_preprocessor.h"
#include "model/model_backend.h"
#include "frame/frame.h"



class NanoTracker {
public:
    /** @brief 独占接收模型调用和预处理实现；模拟实现可运行真实状态逻辑。 */
    NanoTracker(std::unique_ptr<model::NanoTrackBackend> model,
                std::unique_ptr<NanoTrackPreprocessor> preprocessor);
    ~NanoTracker();
    void init(const FrameRef& frame, const cv::Rect2f& bbox);
    float update(const FrameRef& frame, cv::Rect2f& output_bbox);
    void reset();
    bool isInitialized() const { return m_initialized; }

private:
    friend struct NanoTrackerTestAccess;
    // Parameters
    const int score_size = 15;
    const int min_wh = 5;
    const int template_size = 127;
    const int instance_size = 255;
    const float window_influence = 0.455f;
    const float track_lr = 0.348f;
    const float track_penalty_k = 0.138f;
    const int stride = 16;
    
    // State
    cv::Point2f center_pos;
    cv::Point2f size;
    cv::Mat window;
    cv::Mat points;

    std::unique_ptr<model::NanoTrackBackend> model_;
    std::unique_ptr<NanoTrackPreprocessor> preprocessor_;

    void generate_points();
    cv::Mat convert_bbox(const cv::Mat& delta, const cv::Mat& points);
    cv::Rect2f bbox_clip(const cv::Point2f& center, const cv::Point2f& size, const cv::Size& boundary);

    bool m_initialized = false;
};

#endif // NANOTRACKER_H
