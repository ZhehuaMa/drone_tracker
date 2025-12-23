#include "hybrid_tracker.h"
#include <iostream>
#include <unordered_map>
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
    virtual std::pair<TrackerState, std::string> getStateText() const override;

    /** @brief 获取用于绘制的颜色 */
    virtual uint32_t getColor() const override;

private:
    /** @brief 使用给定的框初始化跟踪器和卡尔曼滤波器 */
    void initialize(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& box);

    /** @brief 在检测结果中找到与当前框最佳匹配的框 */
    cv::Rect2f findBestMatch(const std::vector<Detection>& detections);

    /** @brief 计算两个矩形的 IoU */
    float calculateIOU(const cv::Rect2f& boxA, const cv::Rect2f& boxB);

    /** @brief 检测边界框是否发生跳变 */
    bool isBoxJump(const cv::Rect2f& prev_box, const cv::Rect2f& curr_box, float max_pos_jump, float max_size_ratio);

    /** @brief 计算ROI区域 */
    cv::Rect calculateROI(const cv::Rect2f& box, int img_w, int img_h, float scale_factor);

    // --- 状态 ---
    TrackerSettings m_settings;
    bool m_tracker_initialized;
    cv::Rect2f m_display_box;
    std::string m_display_info;
    cv::Scalar m_display_color;
    int m_frame_count;
    int m_yolo_lost_count; // YOLO连续未检测到目标的次数
    TrackerState m_state = TrackerState::Detecting;

    const std::unordered_map<TrackerState, std::string> state_text_map = {
        {TrackerState::Detecting, "Detecting..."},
        {TrackerState::TargetFound, "Target Found"},
        {TrackerState::Tracking, "Tracking..."},
        {TrackerState::TargetLost, "Target Lost"}
    };

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
      m_yolo_lost_count(0)
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
        m_settings.target_class_id,
        m_settings.yolo_recall_relax_ratio
    );
    
    if (!m_detector || !m_detector->init())
    {
        std::cerr << "Error: Failed to initialize YoloV8Detector." << std::endl;
        return false;
    }

    m_nanotracker = std::make_unique<NanoTracker>(
        m_settings.nanotrack_backbone_path,
        m_settings.nanotrack_head_path
    );

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
    m_yolo_lost_count = 0;
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
        m_state = TrackerState::Detecting;
        m_display_color = cv::Scalar(255, 255, 0); // 黄色

        // 1. 请求检测 (启用切片检测)
        m_detector->request(processed_frame, cv::Rect(), true);

        // 2. 检查结果
        std::priority_queue<Detection> detections;
        if (m_detector->get_result(detections) && !detections.empty())
        {
            // 找到第一个检测框并初始化
            initialize(processed_frame, detections.top().box);
            m_state = TrackerState::TargetFound;
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
        m_state = TrackerState::Tracking;
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
            m_display_info = "Tracking (Kalman <- Tracker)";
            m_display_color = cv::Scalar(0, 255, 0); // 绿色
        }

        // 4. 定期请求YOLO检测 (使用ROI)
        int frame_w, frame_h;
        vpiImageGetSize(processed_frame->get(), &frame_w, &frame_h);
        if (m_frame_count % m_settings.yolo_interval == 0)
        {
            cv::Rect roi = calculateROI(m_display_box, frame_w, frame_h, m_settings.roi_scale_factor);
            m_detector->request(processed_frame, roi, false);
        }

        // 5. 检查YOLO检测结果以进行校准或重置
        std::vector<Detection> detections;
        bool has_detections = m_detector->get_result(detections) && !detections.empty();
        
        if (has_detections)
        {
            cv::Rect2f best_match_box = findBestMatch(detections);
            
            if (best_match_box.width > 0 && best_match_box.height > 0)
            {
                // 检测跳变
                bool jump = isBoxJump(m_display_box, best_match_box, 
                    (frame_w + frame_h) * 0.5f * m_settings.max_position_jump_ratio, 
                    m_settings.max_size_jump_ratio);
                
                if (jump)
                {
                    // 使用YOLO结果重置跟踪器
                    initialize(processed_frame, best_match_box);
                    m_display_box = best_match_box;
                    m_display_info = "Re-initialized (Jump Detected)";
                    m_display_color = cv::Scalar(0, 255, 255); // 青色
                }
                else
                {
                    float iou = calculateIOU(m_display_box, best_match_box);
                    // 条件：跟踪器OK，且YOLO结果与当前框匹配度高
                    if (iou > m_settings.iou_calibrate_threshold)
                    {
                        // 使用YOLO结果校准卡尔曼滤波器
                        m_kalman_filter->correct(best_match_box);
                        m_display_info = "Calibrated (Kalman <- Detector)";
                        m_display_color = cv::Scalar(255, 0, 255); // 品红
                    }
                }
            }
            m_yolo_lost_count = 0;
        }
        else
        {
            m_yolo_lost_count++;
            if (m_yolo_lost_count > m_settings.lost_max_count)
            {
                m_tracker_initialized = false;
                m_display_info = "Lost (Timeout)";
                m_state = TrackerState::TargetLost;
                m_display_color = cv::Scalar(255, 0, 0); // 红色
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

cv::Rect HybridTrackerImp::calculateROI(const cv::Rect2f& box, int img_w, int img_h, float scale_factor)
{
    float cx = box.x + box.width * 0.5f;
    float cy = box.y + box.height * 0.5f;
    
    float roi_w = box.width * scale_factor;
    float roi_h = box.height * scale_factor;

    // Ensure ROI is at least the size of the model input
    if (m_detector) {
        auto [input_w, input_h] = m_detector->getInputSize();
        if (input_w > 0 && input_h > 0) {
            roi_w = std::max(roi_w, input_w * 0.5f);
            roi_h = std::max(roi_h, input_h * 0.5f);
        }
    }
    
    int roi_x = static_cast<int>(cx - roi_w * 0.5f);
    int roi_y = static_cast<int>(cy - roi_h * 0.5f);
    int roi_w_i = static_cast<int>(roi_w);
    int roi_h_i = static_cast<int>(roi_h);
    
    // Clamp to image
    roi_x = std::max(0, std::min(roi_x, img_w - roi_w_i));
    roi_y = std::max(0, std::min(roi_y, img_h - roi_h_i));
    roi_w_i = std::min(roi_w_i, img_w - roi_x);
    roi_h_i = std::min(roi_h_i, img_h - roi_y);
    
    return cv::Rect(roi_x, roi_y, roi_w_i, roi_h_i);
}

bool HybridTrackerImp::isBoxJump(const cv::Rect2f& prev_box, const cv::Rect2f& curr_box, float max_pos_jump, float max_size_ratio)
{
    if (prev_box.width <= 0 || prev_box.height <= 0 || curr_box.width <= 0 || curr_box.height <= 0)
        return false;

    // 计算中心点距离
    float prev_cx = prev_box.x + prev_box.width / 2.0f;
    float prev_cy = prev_box.y + prev_box.height / 2.0f;
    float curr_cx = curr_box.x + curr_box.width / 2.0f;
    float curr_cy = curr_box.y + curr_box.height / 2.0f;
    
    float distance = std::sqrt(std::pow(curr_cx - prev_cx, 2) + std::pow(curr_cy - prev_cy, 2));
    
    // 计算尺寸变化比例
    float size_ratio_w = curr_box.width / prev_box.width;
    float size_ratio_h = curr_box.height / prev_box.height;
    
    // 检测跳变
    bool pos_jump = distance > max_pos_jump;
    bool size_jump = (size_ratio_w > max_size_ratio || size_ratio_w < 1.0f/max_size_ratio ||
                      size_ratio_h > max_size_ratio || size_ratio_h < 1.0f/max_size_ratio);
    
    return pos_jump || size_jump;
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

std::pair<TrackerState, std::string> HybridTrackerImp::getStateText() const
{
    return {m_state, state_text_map.at(m_state)};
}

uint32_t HybridTrackerImp::getColor() const
{
    return (uint32_t(m_display_color[2]) << 16) | (uint32_t(m_display_color[1]) << 8) | uint32_t(m_display_color[0]);
}
