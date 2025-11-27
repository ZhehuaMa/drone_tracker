#ifndef DRONE_EKF_H
#define DRONE_EKF_H

#include <Eigen/Dense>
#include <deque>

// 用于存储相机内参的结构体
struct CameraIntrinsics {
    double fx, fy, px, py;
};

// 用于存储历史状态的结构体
struct StateHistory {
    int64_t timestamp;
    Eigen::Vector<double, 8> state;
    Eigen::Matrix<double, 8, 8> covariance;
    Eigen::Vector3d imu_input;
};

class DroneEKF {
public:
    /**
     * @brief 构造函数
     * @param intrinsics 相机内参
     * @param process_noise_std 过程噪声标准差 [std_pos, std_vel, std_size, std_size_vel]
     * @param measurement_noise_std 测量噪声标准差 [std_meas_pos, std_meas_size]
     * @param initial_P_diag 初始协方差矩阵的对角线元素
     * @param imu_freq IMU数据频率 (Hz)
     * @param estimated_detection_latency 估计的检测延迟 (秒)
     */
    DroneEKF(const CameraIntrinsics& intrinsics,
             const Eigen::Vector4d& process_noise_std,
             const Eigen::Vector2d& measurement_noise_std,
             int imu_freq,
             double estimated_detection_latency);

    /**
     * @brief 使用第一个测量值初始化滤波器
     * @param initial_measurement [cx, cy, w, h]
     */
    void initialize(const Eigen::Vector4d& initial_measurement);

    void reset();

    /**
     * @brief EKF的预测步骤
     * @param current_time 当前时间戳 (秒)
     * @param imu_omega 相机角速度 [omega_x, omega_y, omega_z]
     */
    void predict(int64_t current_time_us, const Eigen::Vector3d& imu_omega);

    /**
     * @brief EKF的更新步骤
     * @param current_time 当前时间戳 (秒)
     * @param ts 测量值对应的时间戳 (秒)
     * @param z 测量值 [cx, cy, w, h]
     */
    void update(int64_t ts_us, const Eigen::Vector4d& z);

    /** @brief 获取当前[cx, cy, w, h]部分的状态 */
    Eigen::Vector4d getState() const;

    /** @brief 检查滤波器是否已初始化 */
    bool isInitialized() const;

private:
    // 状态向量 [cx, cy, w, h, vx, vy, vw, vh]
    Eigen::Vector<double, 8> x_;
    // 状态协方差矩阵
    Eigen::Matrix<double, 8, 8> P_;
    // 过程噪声协方差
    Eigen::Matrix<double, 8, 8> Q_;
    // 测量噪声协方差
    Eigen::Matrix4d R_;
    // 测量矩阵
    Eigen::Matrix<double, 4, 8> H_;

    // 相机参数
    CameraIntrinsics intrinsics_;

    std::deque<StateHistory> history_buffer_;
    size_t max_buffer_size_;
    
    // 初始化标志
    bool is_initialized_ = false;
    int64_t last_timestamp_;
    double dt_;

    // Eigen::Vector2d v_imu_cache;
    void update_internal(const Eigen::Vector4d& z);
};

#endif // DRONE_EKF_H

