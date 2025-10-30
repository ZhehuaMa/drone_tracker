#include "simple_kf.h"

SimpleKalmanFilter::SimpleKalmanFilter() 
    : m_kf(6, 4, 0, CV_32F) // 6个状态, 4个测量
{
    // 状态转移矩阵 (A)
    // [ 1 0 0 0 1 0 ]
    // [ 0 1 0 0 0 1 ]
    // [ 0 0 1 0 0 0 ]
    // [ 0 0 0 1 0 0 ]
    // [ 0 0 0 0 1 0 ]
    // [ 0 0 0 0 0 1 ]
    cv::setIdentity(m_kf.transitionMatrix);
    m_kf.transitionMatrix.at<float>(0, 4) = 1.0f;
    m_kf.transitionMatrix.at<float>(1, 5) = 1.0f;

    // 测量矩阵 (H)
    // [ 1 0 0 0 0 0 ]
    // [ 0 1 0 0 0 0 ]
    // [ 0 0 1 0 0 0 ]
    // [ 0 0 0 1 0 0 ]
    m_kf.measurementMatrix = cv::Mat::zeros(4, 6, CV_32F);
    m_kf.measurementMatrix.at<float>(0, 0) = 1.0f;
    m_kf.measurementMatrix.at<float>(1, 1) = 1.0f;
    m_kf.measurementMatrix.at<float>(2, 2) = 1.0f;
    m_kf.measurementMatrix.at<float>(3, 3) = 1.0f;

    // 过程噪声协方差 (Q)
    cv::setIdentity(m_kf.processNoiseCov, cv::Scalar::all(1e-2));

    // 测量噪声协方差 (R)
    cv::setIdentity(m_kf.measurementNoiseCov, cv::Scalar::all(1e-1));

    // 初始后验误差协方差 (P)
    cv::setIdentity(m_kf.errorCovPost, cv::Scalar::all(1.0));
}

void SimpleKalmanFilter::initialize(const cv::Rect2f& box)
{
    cv::Mat measurement = rectToMeasurement(box);
    
    // 初始化状态 (x, y, w, h, 0, 0)
    m_kf.statePost.at<float>(0) = measurement.at<float>(0);
    m_kf.statePost.at<float>(1) = measurement.at<float>(1);
    m_kf.statePost.at<float>(2) = measurement.at<float>(2);
    m_kf.statePost.at<float>(3) = measurement.at<float>(3);
    m_kf.statePost.at<float>(4) = 0.0f;
    m_kf.statePost.at<float>(5) = 0.0f;
    
    // 同样设置 statePre
    m_kf.statePost.copyTo(m_kf.statePre);
}

cv::Rect2f SimpleKalmanFilter::predict()
{
    cv::Mat prediction = m_kf.predict();
    return stateToRect(prediction);
}

void SimpleKalmanFilter::correct(const cv::Rect2f& box)
{
    cv::Mat measurement = rectToMeasurement(box);
    m_kf.correct(measurement);
}

cv::Mat SimpleKalmanFilter::rectToMeasurement(const cv::Rect2f& box)
{
    cv::Mat measurement = cv::Mat(4, 1, CV_32F);
    measurement.at<float>(0) = box.x + box.width / 2.0f;
    measurement.at<float>(1) = box.y + box.height / 2.0f;
    measurement.at<float>(2) = box.width;
    measurement.at<float>(3) = box.height;
    return measurement;
}

cv::Rect2f SimpleKalmanFilter::stateToRect(const cv::Mat& state)
{
    float cx = state.at<float>(0);
    float cy = state.at<float>(1);
    float w = state.at<float>(2);
    float h = state.at<float>(3);
    
    // 确保 w 和 h 为正
    w = std::max(w, 1.0f);
    h = std::max(h, 1.0f);
    
    return {
        cx - w / 2.0f,
        cy - h / 2.0f,
        w, h};
}
