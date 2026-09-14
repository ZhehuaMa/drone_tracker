#ifndef DCF_TRACKER_H
#define DCF_TRACKER_H

#include <cstdint>
#include <memory>
#include <string>
#include <opencv2/opencv.hpp>

#include "frame/frame.h"

class NanoTracker;

/**
 * @brief airsim_tracker/dcf_tracker.py GrayscaleDCF 的 C++ 移植。
 *
 * 手写灰度 MOSSE 风格相关滤波器，在固定 model_sz 分辨率上工作（patch 先 resize
 * 再滤波），学到的滤波器形状跨帧恒定、与目标尺度解耦（尺度交给 NanoTrack）。
 * 稳定性策略：固定模板（默认仅 init/重锚定时学习）、正则化 lambda、对称汉宁窗
 * + 抛物线亚像素拟合。
 */
class GrayscaleDCF
{
public:
    struct Params
    {
        int model_sz = 96;          // 滤波分辨率
        float pad = 1.0f;           // patch 相对目标尺寸的边距
        float sigma_factor = 0.1f;  // 高斯标签 sigma = model_sz * sigma_factor
        float learning_rate = 0.01f; // 仅 fixed_template=false 时使用
        float psr_thresh = 5.0f;    // 重锚定门限
        float lambda_reg = 1e-4f;   // 正则化 λ
        bool fixed_template = true; // 跟踪期间不更新滤波器
        float eps = 1e-5f;
    };

    explicit GrayscaleDCF(const Params& params);

    /** @brief 学习模板（单样本 MOSSE + 正则化） */
    void init(const cv::Mat& gray, const cv::Point2f& center, const cv::Size2f& size);

    /**
     * @brief 在以 search_center 为中心的搜索窗内定位目标中心
     * @return false 表示滤波器未训练（此时调用方应转而信任 NanoTrack）
     */
    bool update(const cv::Mat& gray, const cv::Point2f& search_center, const cv::Size2f& size,
                cv::Point2f& new_center, float& psr);

    void reset();
    float psrThreshold() const { return params_.psr_thresh; }

private:
    int nativeSize(const cv::Size2f& size) const;
    cv::Mat getPatch(const cv::Mat& gray, const cv::Point2f& center, int native_sz) const;
    void peak(const cv::Mat& R, int& ri, int& ci, double& rmax, double& cmax, float& psr) const;
    void recomputeFilter();

    Params params_;
    cv::Mat window_;   // 对称汉宁窗外积 (CV_64FC1)
    cv::Mat Y_;        // 高斯标签的频谱 (CV_64FC2)
    cv::Mat A_, B_, H_; // MOSSE 累积量与滤波器 (CV_64FC2)
    bool trained_ = false;
    double scale_ = 1.0;  // 模型像素 -> 原图像素
};

/**
 * @brief 轻量中心平滑卡尔曼滤波器（状态 [cx, cy, vx, vy]，匀速模型）。
 *
 * enable_dcf=false 时承担 DCF 的"稳心"角色：抑制 NanoTrack 中心的逐帧抖动。
 * 与主管线的旋转补偿回溯 KF 相互独立——本滤波器只做局部抖动抑制（帧时间戳
 * 驱动），不感知相机旋转；发散重锚定由 NanoDcfTracker 的门限负责。
 */
class CenterKalmanFilter
{
public:
    struct Params
    {
        double r_pos = 4.0;   // 中心测量噪声方差 (px^2)
        double q_a = 5e4;     // CWNA 加速度过程噪声谱密度
    };

    explicit CenterKalmanFilter(const Params& params);

    /** @brief 用首个中心测量初始化（速度置零，速度方差放大） */
    void initialize(const cv::Point2f& center, int64_t ts_us);

    /** @brief 只读预测到指定时刻（用于发散门限判断），未初始化返回 (0,0) */
    cv::Point2f predictOnly(int64_t ts_us) const;

    /** @brief 预测 + 用测量中心做约瑟夫形式校正，返回滤波后的中心 */
    cv::Point2f update(const cv::Point2f& meas_center, int64_t ts_us);

    bool initialized() const { return initialized_; }
    void reset();

private:
    cv::Matx44d transition(double dt) const;
    cv::Matx44d processNoise(double dt) const;

    Params params_;
    cv::Vec4d x_{};
    cv::Matx44d P_ = cv::Matx44d::eye() * 1000.0;
    int64_t last_ts_us_ = 0;
    bool initialized_ = false;
};

/**
 * @brief NanoTrack + 灰度 DCF 融合跟踪器（dcf_tracker.py NanoDcfTracker 移植）。
 *
 * - NanoTrack：每帧给出鲁棒的框与尺度（经由节点侧 YOLO 触发重初始化完成恢复）。
 * - DCF：在局部搜索窗内锁定低抖动的中心。
 * - 输出框 = (DCF 中心, NanoTrack 尺寸)；DCF 与 NanoTrack 中心偏离过大或峰值
 *   置信度不足时，信任 NanoTrack 并以其中心重新锚定 DCF。
 * - enable_dcf=false 时，由 CenterKalmanFilter 承担稳心角色（KF 平滑中心 +
 *   同样的发散重锚定门限），而不是直接输出原始 NanoTrack 框。
 * 对外暴露与 NanoTracker 相同的 init/update 协议，因此 catch-up / 跳变抑制 /
 * 卡尔曼滤波管线无需感知融合细节。
 */
class NanoDcfTracker
{
public:
    struct Params
    {
        std::string backbone_path;
        std::string head_path;
        bool enable_dcf = true;
        GrayscaleDCF::Params dcf;
        CenterKalmanFilter::Params center_kf;  // enable_dcf=false 时的中心平滑 KF
        float max_div_factor = 0.5f;           // 中心发散门限（× 平均边长），DCF 与 KF 路径共用
    };

    NanoDcfTracker(const Params& params, std::unique_ptr<NanoTracker> nano);
    ~NanoDcfTracker();

    /** @brief 用给定框初始化（TRT 引擎只加载一次，重复 init 仅重跑 nano.init） */
    bool init(const FrameRef& frame, const cv::Rect2f& bbox);

    /** @brief 融合更新；返回 (x, y, w, h) 左上角 + 尺寸。失败返回 false */
    bool update(const FrameRef& frame, cv::Rect2f& bbox);

    /** @brief 清空 DCF/中心状态（不销毁 TRT 引擎） */
    void reset();

    bool isInitialized() const;
    float lastScore() const { return last_score_; }
    /** @brief 清空单帧诊断，不改变模板、滤波或仲裁状态。 */
    void clearDiagnostics() { last_nano_box_ = {}; last_score_ = -1.0f; }
    /** @brief 诊断：本帧 NanoTrack 的原始输出框（融合/KF 平滑之前） */
    const cv::Rect2f& lastNanoBox() const { return last_nano_box_; }

private:
    Params params_;
    std::unique_ptr<NanoTracker> nano_;
    std::unique_ptr<GrayscaleDCF> dcf_;
    CenterKalmanFilter center_kf_;
    bool has_prev_center_ = false;
    cv::Point2f prev_center_;
    float last_score_ = 0.0f;
    cv::Rect2f last_nano_box_;
};

#endif // DCF_TRACKER_H
