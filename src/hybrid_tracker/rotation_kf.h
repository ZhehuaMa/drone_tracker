#ifndef ROTATION_KF_H
#define ROTATION_KF_H

#include <deque>
#include <cstdint>
#include <opencv2/core.hpp>

/**
 * @brief airsim_tracker/kalman_filter.py 的 C++ 移植。
 *
 * 6 状态 [cx, cy, w, h, vx, vy]，4 测量 [cx, cy, w, h]。
 * - 相机旋转补偿：把目标像素射线旋转到新相机系（R_rel = R_cam_new^T * R_cam_old），
 *   抵消帧间相机转动带来的表观位置跳变。
 * - 回溯(OOSM)校正：测量带时间戳（可能延迟），从历史中找到基准时刻，在那里做
 *   约瑟夫形式校正，再把之后的历史条目逐个重传播。
 *
 * 本管线只使用 initialize / predictOnly / update（与 Python 端一致，
 * 不实现会修改状态并追加历史的 predict()）。
 */
class RotationKalmanFilter
{
public:
    struct Params
    {
        double fx = 0.0;            // 焦距（像素）
        double fy = 0.0;
        double cx0 = 0.0;           // 主点（图像中心）
        double cy0 = 0.0;
        double r_pos = 4.0;         // 位置测量噪声方差 (px^2)
        double r_size = 4.0;        // 尺寸测量噪声方差 (px^2)
        double q_a = 5e4;           // CWNA 加速度过程噪声谱密度
        double q_size = 1e3;        // 尺寸过程噪声
        int history_len = 60;       // 状态历史最大长度
    };

    explicit RotationKalmanFilter(const Params& params);

    /** @brief 用首个检测框初始化滤波器，清空历史 */
    void initialize(const cv::Vec4d& cxcywh, int64_t ts_us, const cv::Matx33d& R_cam);

    /** @brief 只读预测到指定时刻（不修改状态与历史），用于查询"目标现在在哪" */
    bool predictOnly(int64_t ts_us, const cv::Matx33d& R_cam, cv::Vec4d& out_box) const;

    /** @brief 用（可能延迟的）测量回溯校正历史并重传播，返回最新状态对应的框 */
    cv::Vec4d update(const cv::Vec4d& cxcywh, int64_t ts_us, const cv::Matx33d& R_cam);

    bool initialized() const { return initialized_; }
    void reset();

private:
    struct Entry
    {
        int64_t ts_us;
        cv::Vec6d x;
        cv::Matx66d P;
        cv::Matx33d R;
    };

    /** @brief 相机旋转补偿预测：旋转 (p + v*dt) 并回投影像素 */
    cv::Vec6d rotatePredict(const cv::Vec6d& x, const cv::Matx33d& R_rel, double dt) const;
    cv::Matx66d transitionMatrix(double dt) const;  // cx,cy 匀速；w,h 随机游走
    cv::Matx66d processNoise(double dt) const;      // 连续白噪声加速度模型
    static double dtBetween(int64_t ts_from_us, int64_t ts_to_us) { return (ts_to_us - ts_from_us) * 1e-6; }
    cv::Vec4d boxFromState(const cv::Vec6d& x) const { return cv::Vec4d(x[0], x[1], x[2], x[3]); }

    Params params_;
    cv::Matx44d R_;                // 测量噪声
    std::deque<Entry> history_;
    cv::Vec6d x_;
    cv::Matx66d P_;
    bool initialized_ = false;
};

#endif // ROTATION_KF_H
