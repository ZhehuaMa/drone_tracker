#ifndef YOLOV8DETECTOR_H
#define YOLOV8DETECTOR_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <cstdint>
#include <opencv2/opencv.hpp>

#include "preprocess/image_preprocessor.h"
#include "model/model_backend.h"
#include "frame/frame.h"
#include "detection_thread.h"
#include "preprocess/preprocess_geometry.h"


// 用于保存检测结果的结构体
struct Detection
{
    cv::Rect2f box;
    float score;
    int classId;

    bool operator<(const Detection& other) const
    {
        return score < other.score; // 用于优先队列，分数较高的优先
    }
};

// 检测结果：单框 + 它所对应的帧号（异步检测延迟的关键——主循环据此找到
// 历史帧做补帧初始化）
struct YoloResult
{
    uint64_t frame_id = 0;
    bool has_target = false;
    Detection best{};
};

class YoloV8Detector
{
public:
    YoloV8Detector(std::unique_ptr<model::YoloBackend> model,
                   std::unique_ptr<YoloPreprocessor> preprocessor, float conf_thresh, float nms_thresh,
                   int target_class_id, float recall_relax_ratio, float max_box_area_ratio = 0.8f,
                   DetectionThreadLauncher launcher = launchDetectionThread);
    ~YoloV8Detector();

    /** @brief 校验注入模型的输入契约并启动工作线程 */
    bool init();

    /** @brief 请求对新帧进行检测（ROI -> 全图 -> 自适应切片级联）
     * @param frame 要处理的图像
     * @param frame_id 帧号，随结果一并返回
     * @param roi 感兴趣区域 (如果为空则直接全图+切片)
     * @return 如果成功提交检测，则为 true
     */
    bool request(const FrameRef& frame, uint64_t frame_id,
                 const cv::Rect& roi = cv::Rect());

    /** @brief 工作线程是否还有未领取的请求（忙则跳过本帧的检测请求） */
    bool isBusy() const;

    /** @brief 获取最新的检测结果（可能为空目标，信号本身表示检测已完成）
     * @return 如果成功获取了新结果，则为 true
     */
    bool get_result(YoloResult& result);

    /** @brief 获取模型输入尺寸 */
    std::pair<int, int> getInputSize() const;

    /** @brief 停止工作线程 */
    void stop();

    /** @brief 自适应重叠切片（Python create_overlapping_tiles 移植）：
     * 短边固定 2 份，长边自适应最少份数(>=2)使每块尽量正方形 */
    static std::vector<cv::Rect> createOverlappingTiles(int width, int height, float overlap_ratio);

private:
    /** @brief 工作线程的主循环 */
    void workerThreadLoop();

    void detectOnImage(YoloFrameSession &session, const cv::Rect &region, std::vector<Detection> &results);

    /** @brief 对模型输出进行后处理（解码, NMS）
     * @param output 模型的原始输出
     * @param lb 预处理中的 letterbox 变换参数
     * @param global_offset 全局坐标偏移量 (用于切片或ROI)
     * @param results [out] 检测结果列表
     */
    void postprocess(const float* output, const TensorShape &output_dims, const LetterboxTransform& lb, const cv::Size& image_size,
        const cv::Point2f& global_offset, std::vector<Detection>& results);

    std::unique_ptr<model::YoloBackend> model_;
    std::unique_ptr<YoloPreprocessor> preprocessor_;
    ModelInputContract input_contract_{};

    int class_count_{0};
    int input_w = 0;
    int input_h = 0;
    
    // --- 阈值 ---
    float m_conf_thresh;
    float m_conf_thresh_current;
    float m_relax_ratio;
    float m_nms_thresh;
    float m_max_box_area_ratio;
    int m_target_class_id;

    // --- 线程通信 ---
    DetectionThreadLauncher m_launcher;
    std::thread m_worker_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv_request;

    struct Request {
        FrameRef frame;
        uint64_t frame_id = 0;
        cv::Rect roi;
    } m_next_request;

    bool m_new_request = false;
    std::atomic<bool> m_result_ready;
    std::atomic<bool> m_stop_requested;
    YoloResult m_last_result;
};

#endif // YOLOV8DETECTOR_H
