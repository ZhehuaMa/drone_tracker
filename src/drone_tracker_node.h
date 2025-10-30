#pragma once
#include <rclcpp/rclcpp.hpp>
#include <opencv2/opencv.hpp>

#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <vision_msgs/msg/detection2_d.hpp>



#include "hybrid_tracker.h"
#include "mv_camera.h"
#include "drone_ekf.h"

class DroneTrackerNode : public rclcpp::Node
{
public:
    DroneTrackerNode();
    ~DroneTrackerNode();

    void run();

private:

    void setup();
    void worker();

    void imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg);

    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_callback_handle_;
    rclcpp::Publisher<vision_msgs::msg::Detection2D>::SharedPtr detection_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;

    std::shared_ptr<HybridTracker> tracker_;
    std::unique_ptr<MVCamera> mv_camera_;
    bool is_camera_ = false;
    cv::VideoCapture cap_;

    int interval_ms_ = 1;
    bool enable_image_output_ = false;
    
    CameraIntrinsics cam_intrinsics_;

    std::mutex ekf_mtx_;
    std::unique_ptr<DroneEKF> drone_ekf_;
    bool detect_valid_ = false;

    std::thread worker_thread_;
    bool running = false;
};