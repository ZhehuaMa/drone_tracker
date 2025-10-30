#ifndef SIMPLEKALMANFILTER_H
#define SIMPLEKALMANFILTER_H

#include <opencv2/video/tracking.hpp>
#include <opencv2/opencv.hpp>

/**
 * @class SimpleKalmanFilter
 * @brief 围绕 cv::KalmanFilter 的一个简单封装器，用于跟踪边界框。
 * * 状态 (6D): [x, y, w, h, vx, vy] (x,y是中心点)
 * 测量 (4D): [x, y, w, h] (x,y是中心点)
 */
class SimpleKalmanFilter
{
public:
    SimpleKalmanFilter();

    /** @brief 使用第一个检测框初始化滤波器 */
    void initialize(const cv::Rect2f& box);

    /** @brief 预测下一个状态 */
    cv::Rect2f predict();

    /** @brief 使用新的测量值校正滤波器 */
    void correct(const cv::Rect2f& box);

private:
    cv::KalmanFilter m_kf;
    
    /** @brief 将 cv::Rect2f (x, y, w, h) 转换为测量向量 (cx, cy, w, h) */
    cv::Mat rectToMeasurement(const cv::Rect2f& box);
    
    /** @brief 将状态向量 (cx, cy, w, h, vx, vy) 转换为 cv::Rect2f (x, y, w, h) */
    cv::Rect2f stateToRect(const cv::Mat& state);
};

#endif // SIMPLEKALMANFILTER_H
