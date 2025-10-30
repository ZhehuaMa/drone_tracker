#ifndef YOLOV8DETECTOR_H
#define YOLOV8DETECTOR_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
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
};

class YoloV8Detector
{
public:
    YoloV8Detector(const std::string& engine_path, float conf_thresh, float nms_thresh, int target_class_id);
    ~YoloV8Detector();

    /** @brief 初始化TRT引擎、分配缓冲区并启动工作线程 */
    bool init();

    /** @brief 请求对新帧进行检测
     * @param frame 要处理的图像
     * @return 如果成功提交检测，则为 true
     */
    bool request(const std::shared_ptr<VPIImageWrapper>& frame);

    /** @brief 检查是否有新的检测结果准备就绪
     * @return 如果结果准备就绪，则为 true
     */
    bool is_result_ready() const;

    /** @brief 获取最新的检测结果
     * @param detections [out] 用于存储检测结果的向量
     * @return 如果成功获取了新结果，则为 true
     */
    bool get_result(std::vector<Detection>& detections);

    /** @brief 停止工作线程 */
    void stop();

private:
    /** @brief 工作线程的主循环 */
    void workerThreadLoop();

    /** @brief 对图像进行预处理（letterbox, HWC->CHW, normalize）
     * @param img 输入图像
     * @param scale [out] 图像缩放比例
     * @param offset [out] letterbox的偏移量 (dw, dh)
     * @param buffer [out] 用于存储预处理后数据的 TensorBuffer
     * @return 成功则为 true
     */
    bool preprocess(VPIImage vpiIm, float &scale, cv::Point2f& offset, TensorBuffer& buffer);

    void update_resized_image(int new_width, int new_height, VPIImageFormat img_format, int flags);

    /** @brief 对模型输出进行后处理（解码, NMS）
     * @param output 模型的原始输出
     * @param scale 预处理中的缩放比例
     * @param offset 预处理中的偏移量
     */
    void postprocess(float* output, float scale, cv::Point2f offset);

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
    
    // --- 阈值 ---
    float m_conf_thresh;
    float m_nms_thresh;
    int m_target_class_id;

    // --- 线程通信 ---
    std::thread m_worker_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv_request;
    
    std::shared_ptr<VPIImageWrapper> vpi_frame_to_process;
    bool m_new_request = false;
    
    std::vector<Detection> m_last_detections;
    std::atomic<bool> m_result_ready;
    std::atomic<bool> m_stop_requested;
};

#endif // YOLOV8DETECTOR_H
