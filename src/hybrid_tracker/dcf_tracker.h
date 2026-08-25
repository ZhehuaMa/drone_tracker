#ifndef DCF_TRACKER_H
#define DCF_TRACKER_H

#include <memory>
#include <opencv2/opencv.hpp>
#include <vpi/VPI.h>

#include "image_wrapper.h"
#include "nano_tracker.h"

/**
 * @brief VPIImage 的主机端只读灰度锁（RAII）。
 *
 * 导出的 cv::Mat 直接引用 VPI 锁定的内存，生命周期必须短于本对象；
 * 单通道直接返回，三通道现场转灰度。
 */
class VpiGrayLock
{
public:
    explicit VpiGrayLock(const std::shared_ptr<VPIImageWrapper>& frame);
    ~VpiGrayLock();
    VpiGrayLock(const VpiGrayLock&) = delete;
    VpiGrayLock& operator=(const VpiGrayLock&) = delete;

    bool valid() const { return locked_; }
    /** @brief 灰度图（CV_8UC1），仅在本对象存活期间有效 */
    const cv::Mat& gray() const { return gray_; }

private:
    VPIImage img_ = nullptr;
    VPIImageData data_{};
    bool locked_ = false;
    cv::Mat mat_;
    cv::Mat gray_;
};

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
 * @brief NanoTrack + 灰度 DCF 融合跟踪器（dcf_tracker.py NanoDcfTracker 移植）。
 *
 * - NanoTrack：每帧给出鲁棒的框与尺度（经由节点侧 YOLO 触发重初始化完成恢复）。
 * - DCF：在局部搜索窗内锁定低抖动的中心。
 * - 输出框 = (DCF 中心, NanoTrack 尺寸)；DCF 与 NanoTrack 中心偏离过大或峰值
 *   置信度不足时，信任 NanoTrack 并以其中心重新锚定 DCF。
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
        float max_div_factor = 0.5f; // DCF/Nano 中心发散门限（× 平均边长）
    };

    explicit NanoDcfTracker(const Params& params);

    /** @brief 用给定框初始化（TRT 引擎只加载一次，重复 init 仅重跑 nano.init） */
    bool init(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& bbox);

    /** @brief 融合更新；返回 (x, y, w, h) 左上角 + 尺寸。失败返回 false */
    bool update(const std::shared_ptr<VPIImageWrapper>& frame, cv::Rect2f& bbox);

    /** @brief 清空 DCF/中心状态（不销毁 TRT 引擎） */
    void reset();

    bool isInitialized() const;
    float lastScore() const { return last_score_; }

private:
    Params params_;
    std::unique_ptr<NanoTracker> nano_;
    std::unique_ptr<GrayscaleDCF> dcf_;
    bool has_prev_center_ = false;
    cv::Point2f prev_center_;
    float last_score_ = 0.0f;
};

#endif // DCF_TRACKER_H
