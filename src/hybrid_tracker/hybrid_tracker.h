#ifndef HYBRIDTRACKER_H
#define HYBRIDTRACKER_H

#include <string>
#include <memory>

// 用于配置 HybridTracker 的结构体
struct TrackerSettings
{
    std::string yolo_engine_path = "drone_int8.engine";
    std::string nanotrack_backbone_path = "nanotrackv3_backbone_int8.engine";
    std::string nanotrack_head_path = "nanotrack_head_int8.engine";
    int target_class_id = 0;
    float tracking_conf_threshold = 0.8f;
    float yolo_conf_threshold = 0.3f;
    float yolo_nms_threshold = 0.45f;
    int yolo_interval = 3;         // 每N帧运行一次检测
    float iou_reset_threshold = 0.3f;   // IoU低于此值时重置
    float iou_calibrate_threshold = 0.6f; // IoU高于此值时校准
};


class VPIImageWrapper;

struct BBoxRect
{
    int cx; //!< x coordinate of the top-left corner
    int cy; //!< y coordinate of the top-left corner
    int width; //!< width of the rectangle
    int height; //!< height of the rectangle
    bool is_valid;
};

/**
 * @class HybridTracker
 * @brief 结合了YOLOv8检测、跟踪器（NanoTrack）和卡尔曼滤波的主跟踪类。
 */
class HybridTracker
{
public:
    static std::shared_ptr<HybridTracker> CreateInstance(const TrackerSettings& settings);
    virtual ~HybridTracker() = default;

    /** @brief 初始化检测器线程和跟踪器 */
    virtual bool init() = 0;

    /** @brief 处理单个视频帧 */
    virtual bool update(const std::shared_ptr<VPIImageWrapper>& frame) = 0;

    /** @brief 获取当前显示的边界框 */
    virtual BBoxRect getCurrentBox() const = 0;

    /** @brief 获取当前跟踪状态信息 */
    virtual std::string getStatus() const = 0;
    /** @brief 获取用于绘制的颜色 */
    virtual uint32_t getColor() const = 0;
};


#endif // HYBRIDTRACKER_H
