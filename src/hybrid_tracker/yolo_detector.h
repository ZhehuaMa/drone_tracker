#ifndef YOLOV8DETECTOR_H
#define YOLOV8DETECTOR_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <queue>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <vpi/VPI.h>

#include "trt_engine.h"
#include "image_wrapper.h"

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

// letterbox 变换参数：内容实际写入的整数偏移与每轴缩放
// （与 Python 端一致：用真实整数 padding 反算，而不是浮点半边距）
struct LetterboxInfo
{
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    int left = 0;
    int top = 0;
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
    YoloV8Detector(const std::string& engine_path, float conf_thresh, float nms_thresh,
                   int target_class_id, float recall_relax_ratio, float max_box_area_ratio = 0.8f);
    ~YoloV8Detector();

    /** @brief 初始化TRT引擎、分配缓冲区并启动工作线程 */
    bool init();

    /** @brief 请求对新帧进行检测（ROI -> 全图 -> 自适应切片级联）
     * @param frame 要处理的图像
     * @param frame_id 帧号，随结果一并返回
     * @param roi 感兴趣区域 (如果为空则直接全图+切片)
     * @return 如果成功提交检测，则为 true
     */
    bool request(const std::shared_ptr<VPIImageWrapper>& frame, uint64_t frame_id,
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

    /** @brief 对图像进行预处理（letterbox, HWC->CHW, normalize）
     * @param img 输入图像
     * @param lb [out] letterbox 变换参数（每轴缩放 + 整数偏移）
     * @param buffer [out] 用于存储预处理后数据的 TensorBuffer
     * @return 成功则为 true
     */
    bool preprocess(VPIImage vpiIm, LetterboxInfo& lb, const cv::Size& image_size, TensorBuffer& buffer);

    /** @brief 在单个图像(或视图)上运行检测 */
    void detectOnImage(VPIImage image, const cv::Point2f& global_offset, std::vector<Detection>& results);

    void update_resized_image(int new_width, int new_height, VPIImageFormat img_format, int flags);

    /** @brief 对模型输出进行后处理（解码, NMS）
     * @param output 模型的原始输出
     * @param lb 预处理中的 letterbox 变换参数
     * @param global_offset 全局坐标偏移量 (用于切片或ROI)
     * @param results [out] 检测结果列表
     */
    void postprocess(float* output, const LetterboxInfo& lb, const cv::Size& image_size,
        const cv::Point2f& global_offset, std::vector<Detection>& results);

    // --- TrtEngine 实例 ---
    std::string m_engine_path;
    std::unique_ptr<TrtEngine> m_trt_engine;
    
    nvinfer1::Dims m_input_dims;
    nvinfer1::Dims m_output_dims;

    TensorBuffer input_buffer;
    TensorBuffer output_buffer;

    VPIStream vpi_stream = nullptr;
    VPIImage vpi_padded_image = nullptr;
    VPIImage vpi_resized_image = nullptr;
    VPIImage vpi_fill_image = nullptr;   // 恒为 114 的 U8 图，用于填充 letterbox 边距

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
    std::thread m_worker_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv_request;

    struct Request {
        std::shared_ptr<VPIImageWrapper> frame;
        uint64_t frame_id = 0;
        cv::Rect roi;
    } m_next_request;

    bool m_new_request = false;
    std::atomic<bool> m_result_ready;
    std::atomic<bool> m_stop_requested;
    YoloResult m_last_result;
};

#endif // YOLOV8DETECTOR_H
