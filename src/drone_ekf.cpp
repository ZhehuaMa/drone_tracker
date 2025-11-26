#include "drone_ekf.h"
#include <iostream>
#include <cmath>

DroneEKF::DroneEKF(const CameraIntrinsics& intrinsics,
                   const Eigen::Vector4d& process_noise_std,
                   const Eigen::Vector2d& measurement_noise_std,
                   int imu_freq,
                   double detection_latency)
    : intrinsics_(intrinsics), 
      max_buffer_size_(static_cast<size_t>(imu_freq * detection_latency * 2)), 
      dt_(1.0 / imu_freq)
{
    x_.setZero();
    // v_imu_cache.setZero();

    double std_pos = process_noise_std(0);
    double std_vel = process_noise_std(1);
    double std_size = process_noise_std(2);
    double std_size_vel = process_noise_std(3);

    Eigen::Vector<double, 8> q_diag;
    q_diag << std_pos*std_pos, std_pos*std_pos, 
              std_size*std_size, std_size*std_size,
              std_vel*std_vel, std_vel*std_vel, 
              std_size_vel*std_size_vel, std_size_vel*std_size_vel;
    Q_ = q_diag.asDiagonal();

    P_ = Q_ * 10; // Initial covariance much larger than process noise

    double std_meas_pos = measurement_noise_std(0);
    double std_meas_size = measurement_noise_std(1);
    Eigen::Vector4d r_diag;
    r_diag << std_meas_pos*std_meas_pos, std_meas_pos*std_meas_pos,
              std_meas_size*std_meas_size, std_meas_size*std_meas_size;
    R_ = r_diag.asDiagonal();

    // Setting measurement matrix H
    H_.setZero();
    H_(0, 0) = 1;
    H_(1, 1) = 1;
    H_(2, 2) = 1;
    H_(3, 3) = 1;
}

void DroneEKF::initialize(const Eigen::Vector4d& initial_measurement) {
    x_.head<4>() = initial_measurement;
    x_.tail<4>().setZero();
    is_initialized_ = true;
    last_timestamp_ = 0;
    history_buffer_.clear();
    // std::cout << "EKF initialized" << std::endl;
}

void DroneEKF::predict(int64_t current_time_us, const Eigen::Vector3d& imu_omega) {
    if (!is_initialized_) return;

    if (current_time_us > 0) { 
        if (last_timestamp_ > 0) {
            dt_ = double(current_time_us - last_timestamp_) / 1e6;
        }
        last_timestamp_ = current_time_us;
    }

    // --- 1. State Prediction (Non-linear function f(x,u)) ---
    
    // Start with constant velocity prediction
    Eigen::Matrix<double, 8, 8> F_cv = Eigen::Matrix<double, 8, 8>::Identity();
    F_cv(0, 4) = dt_; 
    F_cv(1, 5) = dt_; 
    F_cv(2, 6) = dt_; 
    F_cv(3, 7) = dt_;
    Eigen::Vector<double, 8> x_pred = F_cv * x_;

    // Add IMU-based motion compensation
    double omega_x = imu_omega(0);
    double omega_y = imu_omega(1);
    double omega_z = imu_omega(2);
    double cx = x_(0);
    double cy = x_(1);
    double px = intrinsics_.px;
    double py = intrinsics_.py;
    double fx = intrinsics_.fx;
    double fy = intrinsics_.fy;

    // [Fix] Use the full, correct rotational flow model to calculate apparent velocity from ego-motion
    double v_imu_cx = (1/fy)*(cx-px)*(cy-py)*omega_x - (fx + (1/fx)*(cx-px)*(cx-px))*omega_y + (fx/fy)*(cy-py)*omega_z;
    double v_imu_cy = (fy + (1/fy)*(cy-py)*(cy-py))*omega_x - (1/fx)*(cx-px)*(cy-py)*omega_y - (fy/fx)*(cx-px)*omega_z;

    // v_imu_cache << v_imu_cx, v_imu_cy;
    
    // Compensate predicted position. The state `x` represents the target's motion,
    // so we add the apparent motion from the camera to get the final predicted measurement.
    x_pred(0) += v_imu_cx * dt_;
    x_pred(1) += v_imu_cy * dt_;
    // NOTE: We do NOT compensate velocity here for stability. The velocity state will be corrected by the measurement update.

    // --- 2. Covariance Prediction (P = F*P*F' + Q) ---

    // Calculate Jacobian of the full non-linear prediction function
    Eigen::Matrix<double, 8, 8> F = Eigen::Matrix<double, 8, 8>::Identity();
    // Part from constant velocity model
    F(0, 4) = dt_; 
    F(1, 5) = dt_; 
    F(2, 6) = dt_; 
    F(3, 7) = dt_;

    // [Fix] Part from Jacobian of the new, correct IMU compensation term
    // Partial derivatives of v_imu_c with respect to cx and cy
    double d_vc_dcx = (1/fy)*(cy-py)*omega_x - (2/fx)*(cx-px)*omega_y;
    double d_vc_dcy = (1/fy)*(cx-px)*omega_x + (fx/fy)*omega_z;
    double d_vcy_dcx = -(1/fx)*(cy-py)*omega_y - (fy/fx)*omega_z;
    double d_vcy_dcy = (2/fy)*(cy-py)*omega_x - (1/fx)*(cx-px)*omega_y;

    // Populate the Jacobian F
    // d(x_pred(0))/dx = d(x(0) + x(4)*dt + v_imu_cx*dt)/dx
    F(0, 0) += d_vc_dcx * dt_;
    F(0, 1) += d_vc_dcy * dt_;
    // d(x_pred(1))/dx = d(x(1) + x(5)*dt + v_imu_cy*dt)/dx
    F(1, 0) += d_vcy_dcx * dt_;
    F(1, 1) += d_vcy_dcy * dt_;
    
    // --- 3. Update state and covariance ---
    x_ = x_pred;
    P_ = F * P_ * F.transpose() + Q_;

    // Enforce symmetry to improve numerical stability
    P_ = 0.5 * (P_ + P_.transpose());

    if (current_time_us > 0) {
        history_buffer_.push_back({
            current_time_us,
            x_,
            P_,
            imu_omega
        });
        if (history_buffer_.size() > max_buffer_size_) {
            history_buffer_.pop_front();
        }
    }
}

void DroneEKF::update_internal(const Eigen::Vector4d& z)
{
    Eigen::Vector4d y = z - H_ * x_;
    Eigen::Matrix<double, 8, 4> PH_T = P_ * H_.transpose();
    Eigen::Matrix4d S = H_ * PH_T + R_;

    // Add robust check for singularity before inversion
    if (std::abs(S.determinant()) < 1e-12) {
        // Skip update if matrix is nearly singular to avoid numerical issues.
        return;
    }
    Eigen::Matrix<double, 8, 4> K = PH_T * S.inverse();

    x_ = x_ + K * y;

    // --- MODIFICATION: Use the numerically stable Joseph form for covariance update ---
    static const Eigen::Matrix<double, 8, 8> I = Eigen::Matrix<double, 8, 8>::Identity();
    Eigen::Matrix<double, 8, 8> I_KH = I - K * H_;
    P_ = I_KH * P_ * I_KH.transpose() + K * R_ * K.transpose();

    // Enforce symmetry again after update
    P_ = 0.5 * (P_ + P_.transpose());
}

void DroneEKF::update(int64_t ts_us, const Eigen::Vector4d& z) {
    if (!is_initialized_) {
        initialize(z);
        return;
    }
    auto it = std::min_element(history_buffer_.begin(), history_buffer_.end(), 
        [&](const StateHistory& a, const StateHistory& b) {
            return std::abs(a.timestamp - ts_us) < std::abs(b.timestamp - ts_us);
        });

    if (it != history_buffer_.end()) {
        // Update from the past
        x_ = it->state;
        P_ = it->covariance;
        update_internal(z);

        // Replay subsequent IMU inputs
        // From the corrected historical point, reapply subsequent IMU inputs
        for (auto replay_it = std::next(it); replay_it != history_buffer_.end(); ++replay_it) {
            dt_ = double(replay_it->timestamp - std::prev(replay_it)->timestamp) / 1e6;
            predict(-1, replay_it->imu_input); // -1 indicates no timestamp update
        }
    }
}

Eigen::Vector4d DroneEKF::getState() const {
    return x_.head<4>();
}

bool DroneEKF::isInitialized() const {
    return is_initialized_;
}

