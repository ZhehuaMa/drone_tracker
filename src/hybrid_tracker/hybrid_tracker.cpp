#include "hybrid_tracker.h"
#include <iostream>
#include <opencv2/opencv.hpp>

#include "yolo_detector.h"
#include "simple_kf.h"
#include "nano_tracker.h" 
class HybridTrackerImp : public HybridTracker
{
public:
    HybridTrackerImp(const TrackerSettings& settings);
    virtual ~HybridTrackerImp();

    /** @brief 初始化检测器线程和跟踪器 */
    virtual bool init() override;

    virtual void reset() override;

    /** @brief 处理单个视频帧 */
    virtual bool update(const std::shared_ptr<VPIImageWrapper>& frame) override;

    /** @brief 获取当前显示的边界框 */
    virtual BBoxRect getCurrentBox() const override;

    /** @brief 获取当前跟踪状态信息 */
    virtual std::string getStatus() const override;

    /** @brief 获取用于绘制的颜色 */
    virtual uint32_t getColor() const override;

private:
    /** @brief 使用给定的框初始化跟踪器和卡尔曼滤波器 */
    void initialize(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& box);

    /** @brief 在检测结果中找到与当前框最佳匹配的框 */
    cv::Rect2f findBestMatch(const std::vector<Detection>& detections);

    /** @brief 计算两个矩形的 IoU */
    float calculateIOU(const cv::Rect2f& boxA, const cv::Rect2f& boxB);

    // --- 状态 ---
    TrackerSettings m_settings;
    bool m_tracker_initialized;
    cv::Rect2f m_display_box;
    std::string m_display_info;
    cv::Scalar m_display_color;
    int m_frame_count;
    int m_yolo_error_count; // 跟踪器丢失且YOLO也未命中的次数

    // --- 组件 ---
    std::unique_ptr<YoloV8Detector> m_detector;
    std::unique_ptr<SimpleKalmanFilter> m_kalman_filter;
    
    // 跟踪器
    std::unique_ptr<NanoTracker> m_nanotracker;
};

std::shared_ptr<HybridTracker> HybridTracker::CreateInstance(const TrackerSettings& settings)
{
    return std::make_shared<HybridTrackerImp>(settings);
}

HybridTrackerImp::HybridTrackerImp(const TrackerSettings& settings)
    : m_settings(settings),
      m_tracker_initialized(false),
      m_display_box(0, 0, 0, 0),
      m_display_info("Detecting..."),
      m_display_color(255, 255, 0), // 黄色
      m_frame_count(0),
      m_yolo_error_count(0)
{
}

HybridTrackerImp::~HybridTrackerImp()
{
    // m_detector 将在其析构函数中自动停止其线程
}

bool HybridTrackerImp::init()
{
    // 初始化 YOLOv8 检测器
    m_detector = std::make_unique<YoloV8Detector>(
        m_settings.yolo_engine_path,
        m_settings.yolo_conf_threshold,
        m_settings.yolo_nms_threshold,
        m_settings.target_class_id
    );
    
    if (!m_detector || !m_detector->init())
    {
        std::cerr << "Error: Failed to initialize YoloV8Detector." << std::endl;
        return false;
    }

    // 准备跟踪器实例
    // if (m_settings.enable_nanotrack)
    // {
        m_nanotracker = std::make_unique<NanoTracker>(
            m_settings.nanotrack_backbone_path,
            m_settings.nanotrack_head_path
        );
        // NanoTracker 的实际TRT初始化推迟到
        // 第一次调用 initialize() 时
    // }
    // else
    // {
    //     // CSRT 将在 initialize() 中创建
    //     m_csrt_tracker = nullptr; 
    // }

    reset();
    
    return true;
}

void HybridTrackerImp::reset()
{
    if (!m_detector || !m_nanotracker)
        return;
    m_kalman_filter = std::make_unique<SimpleKalmanFilter>();
    m_tracker_initialized = false;
    m_frame_count = 0;
}

void HybridTrackerImp::initialize(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& box)
{
    m_nanotracker->init(frame, box);
    m_kalman_filter->initialize(box);
    m_display_box = box;
    m_tracker_initialized = true;
    m_yolo_error_count = 0;
    std::cout << "Tracker initialized with box: [" 
                << box.x << ", " << box.y << ", " 
                << box.width << ", " << box.height << "]" << std::endl;
}

bool HybridTrackerImp::update(const std::shared_ptr<VPIImageWrapper>& frame)
{
    if (!m_detector || !m_kalman_filter)
    {
        return false;
    }

    auto processed_frame = frame;

    m_frame_count++;

    if (!m_tracker_initialized)
    {
        // --- 状态：正在检测 (未初始化) ---
        m_display_info = "Detecting target...";
        m_display_color = cv::Scalar(255, 255, 0); // 黄色

        // 1. 请求检测
        m_detector->request(processed_frame);

        // 2. 检查结果
        std::vector<Detection> detections;
        if (m_detector->get_result(detections) && !detections.empty())
        {
            // 找到第一个检测框并初始化
            initialize(processed_frame, detections[0].box);
        }
        else
        {
            m_display_box = cv::Rect2f(0, 0, 0, 0); // 无效框
            return false; // 继续等待检测结果
        }
    }
    else
    {
        // --- 状态：正在跟踪 (已初始化) ---

        // 1. 卡尔曼预测
        m_display_box = m_kalman_filter->predict();
        m_display_info = "Predicting (Tracker Lost)";
        m_display_color = cv::Scalar(0, 165, 255); // 橙色

        // 2. 跟踪器更新
        cv::Rect2f tracker_box;
        float score = m_nanotracker->update(processed_frame, tracker_box);
        bool tracker_ok = (score > m_settings.tracking_conf_threshold); // 设定阈值判断跟踪器是否成功
        // 3. 卡尔曼校正 (如果跟踪器OK)
        if (tracker_ok)
        {
            m_kalman_filter->correct(tracker_box);
            // m_display_box = tracker_box; // 使用跟踪器的结果
            m_display_info = "Tracking (Kalman <- Tracker)";
            m_display_color = cv::Scalar(0, 255, 0); // 绿色
        }

        // 4. 定期请求YOLO检测
        if (m_frame_count % m_settings.yolo_interval == 0)
        {
            m_detector->request(processed_frame);
        }

        // 5. 检查YOLO检测结果以进行校准或重置
        std::vector<Detection> detections;
        if (m_detector->get_result(detections) && !detections.empty())
        {
            cv::Rect2f best_match_box = findBestMatch(detections);
            
            if (best_match_box.width > 0 && best_match_box.height > 0)
            {
                float iou = calculateIOU(m_display_box, best_match_box);
                
                if (tracker_ok)
                {
                    if (iou < m_settings.iou_reset_threshold)
                    {
                        m_yolo_error_count++;
                    }
                    else
                    {
                        m_yolo_error_count = 0; // 重置错误计数
                    }
                }

                // 条件：
                // 1. 跟踪器丢失 (e.g. CSRT返回false)
                // 2. 跟踪器连续3次与YOLO不匹配 (m_yolo_error_count >= 3)
                if (!tracker_ok || m_yolo_error_count >= 3)
                {
                    // 使用YOLO结果重置跟踪器
                    initialize(processed_frame, best_match_box);
                    m_display_box = best_match_box;
                    m_display_info = "Re-initialized (Detector)";
                    m_display_color = cv::Scalar(0, 255, 255); // 青色
                }
                // 条件：跟踪器OK，且YOLO结果与当前框匹配度高
                else if (iou > m_settings.iou_calibrate_threshold)
                {
                    // 使用YOLO结果校准卡尔曼滤波器
                    m_kalman_filter->correct(best_match_box);
                    // m_display_box = best_match_box; // 更新为更准的YOLO框
                    m_display_info = "Calibrated (Kalman <- Detector)";
                    m_display_color = cv::Scalar(255, 0, 255); // 品红
                }

                m_display_box = m_kalman_filter->predict();
            }
        }
    }
    
    return true;
}

cv::Rect2f HybridTrackerImp::findBestMatch(const std::vector<Detection>& detections)
{
    float max_iou = -1.0f;
    cv::Rect2f best_box(0, 0, 0, 0);
    
    for (const auto& det : detections)
    {
        float iou = calculateIOU(m_display_box, det.box);
        if (iou > max_iou)
        {
            max_iou = iou;
            best_box = det.box;
        }
    }
    return best_box;
}

float HybridTrackerImp::calculateIOU(const cv::Rect2f& boxA, const cv::Rect2f& boxB)
{
    float xA = std::max(boxA.x, boxB.x);
    float yA = std::max(boxA.y, boxB.y);
    float xB = std::min(boxA.x + boxA.width, boxB.x + boxB.width);
    float yB = std::min(boxA.y + boxA.height, boxB.y + boxB.height);

    float interArea = std::max(0.0f, xB - xA) * std::max(0.0f, yB - yA);
    float boxAArea = boxA.width * boxA.height;
    float boxBArea = boxB.width * boxB.height;

    float denominator = boxAArea + boxBArea - interArea;
    
    return (denominator == 0) ? 0.0f : (interArea / denominator);
}

BBoxRect HybridTrackerImp::getCurrentBox() const
{
    return { 
        static_cast<int>(m_display_box.x + m_display_box.width * 0.5f + 0.5f), 
        static_cast<int>(m_display_box.y + m_display_box.height * 0.5f + 0.5f),
        static_cast<int>(m_display_box.width + 0.5f), 
        static_cast<int>(m_display_box.height + 0.5f),
        m_tracker_initialized && (m_display_box.width > 0) && (m_display_box.height > 0)
    };
}

std::string HybridTrackerImp::getStatus() const
{
    return m_display_info;
}

uint32_t HybridTrackerImp::getColor() const
{
    return (uint32_t(m_display_color[2]) << 16) | (uint32_t(m_display_color[1]) << 8) | uint32_t(m_display_color[0]);
}
