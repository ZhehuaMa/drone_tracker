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
    void declare_parameters() {
        this->declare_parameter<std::string>("video_source", "0");
        this->declare_parameter<bool>("video_save", false);
        this->declare_parameter<std::string>("video_save_path", ".");
        this->declare_parameter<bool>("visualized", false);
        
        this->declare_parameter<std::string>("tracker.yolo_engine", "drone_int8.engine");
        this->declare_parameter<std::string>("tracker.nano_backbone", "nanotrackv3_backbone_int8.engine");
        this->declare_parameter<std::string>("tracker.nano_head", "nanotrack_head_int8.engine");

        this->declare_parameter<int>("tracker.yolo_target_class_id", 0);
        this->declare_parameter<float>("tracker.yolo_conf_threshold", 0.3f);
        this->declare_parameter<float>("tracker.yolo_nms_threshold", 0.45f);
        this->declare_parameter<int>("tracker.yolo_interval", 3);
        this->declare_parameter<float>("tracker.iou_reset_threshold", 0.3f);
        this->declare_parameter<float>("tracker.iou_calibrate_threshold", 0.6f);
        this->declare_parameter<float>("tracker.tracking_conf_threshold", 0.8f);

        this->declare_parameter<int>("mv_camera.width_px", 1920);
        this->declare_parameter<int>("mv_camera.height_px", 1920);
        this->declare_parameter<bool>("mv_camera.flip", false);
        this->declare_parameter<bool>("mv_camera.mirror", false);
        this->declare_parameter<float>("mv_camera.pixel_size_um", 3.45f);
        this->declare_parameter<int>("mv_camera.fps", 30);
        this->declare_parameter<int>("mv_camera.output_width_px", 640);
        this->declare_parameter<int>("mv_camera.output_height_px", 640);
        this->declare_parameter<float>("mv_camera.focal_length_mm", 12.0f);
        this->declare_parameter<float>("video_fov_deg_w", 90.0f);

        this->declare_parameter<int>("ekf.estimated_imu_freq", 100);
        this->declare_parameter<double>("ekf.estimated_latency", 0.08);
        this->declare_parameter<std::vector<double>>("ekf.process_noise_std", {0.5, 0.5, 1.0, 1.0});
        this->declare_parameter<std::vector<double>>("ekf.measurement_noise_std", {5.0, 5.0});
    }

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