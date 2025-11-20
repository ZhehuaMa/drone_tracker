#include "drone_tracker_node.h"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cv_bridge/cv_bridge.h>

using namespace std::chrono_literals;

DroneTrackerNode::DroneTrackerNode()
: Node("drone_tracker_node")
{
    declare_parameters();
    setup();
}

DroneTrackerNode::~DroneTrackerNode()
{
    running = false;
    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }
}

void DroneTrackerNode::run()
{
    if (running) return;
    running = true;
    worker_thread_ = std::thread(&DroneTrackerNode::worker, this);
}

void DroneTrackerNode::setup()
{
    // Initialize publishers, subscribers, and services here
    this->get_parameter("visualized", enable_image_output_);

    int width_px, height_px, output_width_px, output_height_px, fps;
    float pixel_size_um, focal_length_mm, fov_deg_w, fov_deg_h;
    bool flip, mirror;
    this->get_parameter("mv_camera.width_px", width_px);
    this->get_parameter("mv_camera.height_px", height_px);
    this->get_parameter("mv_camera.flip", flip);
    this->get_parameter("mv_camera.mirror", mirror);
    this->get_parameter("mv_camera.focal_length_mm", focal_length_mm);
    this->get_parameter("mv_camera.pixel_size_um", pixel_size_um);
    this->get_parameter("mv_camera.fps", fps);
    this->get_parameter("mv_camera.output_width_px", output_width_px);
    this->get_parameter("mv_camera.output_height_px", output_height_px);
    // Assuming square pixels for simplicity
    float scale_ratio_w = static_cast<float>(output_width_px) / static_cast<float>(width_px);
    float scale_ratio_h = static_cast<float>(output_height_px) / static_cast<float>(height_px);

    cam_intrinsics_.fx = focal_length_mm * 1000.0 / pixel_size_um * scale_ratio_w;
    cam_intrinsics_.fy = focal_length_mm * 1000.0 / pixel_size_um * scale_ratio_h;
    cam_intrinsics_.px = output_width_px / 2.0;
    cam_intrinsics_.py = output_height_px / 2.0;

    fov_deg_w = std::atan2(output_width_px * 0.5, cam_intrinsics_.fx) * 180.0 / M_PI * 2.0;
    fov_deg_h = std::atan2(output_height_px * 0.5, cam_intrinsics_.fy) * 180.0 / M_PI * 2.0;

    std::string video_source;
    this->get_parameter("video_source", video_source);
    is_camera_ = (video_source.substr(0, 5) == "video" && video_source.length() == 6);

    if (is_camera_)
    {
        std::string device = "/dev/";
        mv_camera_ = std::make_unique<MVCamera>(device.append(video_source), 
            width_px, height_px, fps, true, output_width_px, output_height_px, flip, mirror);
    }
    else
    {
        cap_.open(video_source);
        if (!cap_.isOpened())
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to open video file: %s", video_source.c_str());
            return;
        }
        RCLCPP_INFO(this->get_logger(), "Video file %s opened successfully", video_source.c_str());
        width_px = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
        height_px = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
        
        fps = cap_.get(cv::CAP_PROP_FPS);
        interval_ms_ = (fps > 0) ? static_cast<int>(1000 / fps) : 1;
        float param_fov_deg_w;
        this->get_parameter<float>("video_fov_deg_w", param_fov_deg_w);
        if (param_fov_deg_w > 0.0f)
        {
            fov_deg_w = param_fov_deg_w;
            // Recalculate focal length based on provided FOV
            cam_intrinsics_.fx = width_px * 0.5 / tan(fov_deg_w * M_PI / 180.0 / 2.0);
            cam_intrinsics_.fy = cam_intrinsics_.fx;  // Assuming square pixels
            fov_deg_h = std::atan2(height_px * 0.5 ,cam_intrinsics_.fy) * 180.0 / M_PI * 2.0;
        }
        cam_intrinsics_.px = width_px * 0.5;
        cam_intrinsics_.py = height_px * 0.5;
    }

    RCLCPP_INFO(this->get_logger(), "Camera Intrinsics: fx=%.2f, fy=%.2f, px=%.2f, py=%.2f", 
        cam_intrinsics_.fx, cam_intrinsics_.fy, cam_intrinsics_.px, cam_intrinsics_.py);
    RCLCPP_INFO(this->get_logger(), "Camera FOV: horizontal=%.2f deg, vertical=%.2f deg", fov_deg_w, fov_deg_h);

    TrackerSettings tracker_settings;

    std::string share_dir = ament_index_cpp::get_package_share_directory(ROS_PACKAGE_NAME);
    std::cout << "Package share directory: " << share_dir << std::endl;

    this->get_parameter("tracker.yolo_engine", tracker_settings.yolo_engine_path);
    this->get_parameter("tracker.nano_backbone", tracker_settings.nanotrack_backbone_path);
    this->get_parameter("tracker.nano_head", tracker_settings.nanotrack_head_path);
    this->get_parameter("tracker.yolo_target_class_id", tracker_settings.target_class_id);
    this->get_parameter("tracker.tracking_conf_threshold", tracker_settings.tracking_conf_threshold);
    this->get_parameter("tracker.yolo_conf_threshold", tracker_settings.yolo_conf_threshold);
    this->get_parameter("tracker.yolo_nms_threshold", tracker_settings.yolo_nms_threshold);
    this->get_parameter("tracker.yolo_interval", tracker_settings.yolo_interval);
    this->get_parameter("tracker.iou_reset_threshold", tracker_settings.iou_reset_threshold);
    this->get_parameter("tracker.iou_calibrate_threshold", tracker_settings.iou_calibrate_threshold);

    tracker_settings.yolo_engine_path = share_dir + "/models/" + tracker_settings.yolo_engine_path;
    tracker_settings.nanotrack_backbone_path = share_dir + "/models/" + tracker_settings.nanotrack_backbone_path;
    tracker_settings.nanotrack_head_path = share_dir + "/models/" + tracker_settings.nanotrack_head_path;

    tracker_ = HybridTracker::CreateInstance(tracker_settings);
    if (!tracker_->init())
    {
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize HybridTracker");
        return;
    }
    int estimated_imu_freq;
    double estimated_latency;  // seconds
    std::vector<double> process_noise_std;
    std::vector<double> measurement_noise_std;
    this->get_parameter("ekf.estimated_imu_freq", estimated_imu_freq);
    this->get_parameter("ekf.estimated_latency", estimated_latency);
    this->get_parameter("ekf.process_noise_std", process_noise_std);
    this->get_parameter("ekf.measurement_noise_std", measurement_noise_std);

    Eigen::Vector4d process_noise_std_vec;
    process_noise_std_vec << process_noise_std[0], process_noise_std[1], process_noise_std[2], process_noise_std[3];
    Eigen::Vector2d measurement_noise_std_vec;
    measurement_noise_std_vec << measurement_noise_std[0], measurement_noise_std[1];

    drone_ekf_ = std::make_unique<DroneEKF>(cam_intrinsics_, 
        process_noise_std_vec, measurement_noise_std_vec,
        estimated_imu_freq, estimated_latency);

    imu_callback_handle_ = this->create_subscription<sensor_msgs::msg::Imu>(
        "/fpv/gimbal", rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
        std::bind(&DroneTrackerNode::imu_callback, this, std::placeholders::_1)
    );

    detection_pub_ = this->create_publisher<vision_msgs::msg::Detection2D>(
        "/fpv/target_bbox", 10);
    image_pub_ = this->create_publisher<sensor_msgs::msg::Image>(
        "/fpv/tracker_image", 10);
    
}

void DroneTrackerNode::worker()
{
    if (is_camera_)
    { 
        bool need_save = false;
        this->get_parameter("video_save", need_save);
        std::string video_save_path;
        this->get_parameter("video_save_path", video_save_path);
        bool success = mv_camera_->run_async(need_save, video_save_path);
        if (!success)
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to open MV Camera ");
            return;
        }
        RCLCPP_INFO(this->get_logger(), "MV Camera opened successfully");
    }

    cv::Mat frame;

    int count = 0;
    double avg_time_ms = 0.0;

    // Main processing loop
    while (rclcpp::ok() && running)
    {
        // Process the frame with tracker_ and drone_ekf_
        rclcpp::Time start_time = rclcpp::Clock().now();
        std::shared_ptr<VPIImageWrapper> vpi_buffer;
        rclcpp::Time frame_ts;
        if (is_camera_)
        {
            if (mv_camera_->getImageQueue().wait_dequeue_timed(vpi_buffer, 1s)) {
                start_time = rclcpp::Clock().now();
                frame_ts = rclcpp::Time(vpi_buffer->get_timestamps() * 1000UL); // us to ns
                tracker_->update(vpi_buffer);
            }
            else {
                std::cout << "No frame received from camera." << std::endl;
                break;
            }
        }
        else
        {
            frame_ts = start_time;
            if (!cap_.read(frame))
            {
                std::cout << "Video ended." << std::endl;
                break;
            }

            cv::Mat mono;
            cv::cvtColor(frame, mono, cv::COLOR_BGR2GRAY);
            cv::merge(std::vector<cv::Mat>{mono, mono, mono}, frame);
            auto frame_wrapper = std::make_shared<VPIImageWrapper>(mono);
            tracker_->update(frame_wrapper);
        }

        // --- 获取结果并绘制 ---
        auto bbox = tracker_->getCurrentBox();
        {
            std::lock_guard<std::mutex> lock(ekf_mtx_);
            detect_valid_ = bbox.is_valid;
            if (detect_valid_)
            {
                drone_ekf_->update(
                    rclcpp::Clock().now(),
                    frame_ts,
                    Eigen::Vector4d(bbox.cx, bbox.cy, bbox.width, bbox.height)
                );
            }
        }
        
        auto end_time = rclcpp::Clock().now();
        auto elapsed = end_time - start_time;
        double cost_ms = elapsed.seconds() * 1000;
        
        if (enable_image_output_) 
        {
            cv::Rect display_box = {
                static_cast<int>(bbox.cx - bbox.width / 2), 
                static_cast<int>(bbox.cy - bbox.height / 2),
                static_cast<int>(bbox.width), 
                static_cast<int>(bbox.height) };
            std::string status = tracker_->getStatus();
            auto color_val = tracker_->getColor();
            cv::Scalar color = cv::Scalar((color_val & 0xFF), ((color_val >> 8) & 0xFF), ((color_val >> 16) & 0xFF));

            if (is_camera_)
            {
                cv::Mat mono;
                VPIImageData image_data;
                vpiImageLockData(vpi_buffer->get(), VPI_LOCK_READ, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &image_data);
                vpiImageDataExportOpenCVMat(image_data, &mono);
                vpiImageUnlock(vpi_buffer->get());
                cv::merge(std::vector<cv::Mat>{mono, mono, mono}, frame);
            }

            if (bbox.is_valid)
            {
                cv::rectangle(frame, display_box, color, 2, 1);
            }

            cv::putText(frame, "Cost: " + std::to_string(int(cost_ms)) + " ms", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(frame, status, cv::Point(20, 80), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);

            cv_bridge::CvImage cv_img;
            cv_img.header.stamp = builtin_interfaces::msg::Time(frame_ts);
            cv_img.header.frame_id = "camera_frame";
            cv_img.encoding = "bgr8";
            cv_img.image = frame;
            image_pub_->publish(*cv_img.toImageMsg());

            end_time = rclcpp::Clock().now();
            elapsed = end_time - start_time;
            cost_ms = elapsed.seconds() * 1000;
        }


        avg_time_ms = (avg_time_ms * count + cost_ms) / (count + 1);
        ++count;

        // cv::imshow("Hybrid Tracker C++", frame);

        // 计算剩余时间以保持帧率
        int remaining_time_us = static_cast<int>((interval_ms_ - cost_ms) * 1000);
        if (remaining_time_us <= 0) remaining_time_us = 1000; // 至少等待1ms

        // if (cv::waitKey(remaining_time) == 27) // ESC
        // {
        //     break;
        // }
        rclcpp::sleep_for(std::chrono::microseconds(remaining_time_us));
    }

    std::cout << "Average tracking time per frame: " << std::fixed << std::setprecision(2) << avg_time_ms << " ms" << std::endl;

    if (is_camera_)
    {
        mv_camera_->stop();
    }
    else
    {
        cap_.release();
    }
}

void DroneTrackerNode::imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg)
{
    if (!drone_ekf_->isInitialized())
        return;

    Eigen::Vector4d current_state;
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(ekf_mtx_);
        valid = detect_valid_;
        // IMU coordinates are in the FLU frame
        // EFK coordinates are in the RDF frame
        drone_ekf_->predict(rclcpp::Time(msg->header.stamp), 
            Eigen::Vector3d(
                -msg->angular_velocity.y,
                -msg->angular_velocity.z,
                 msg->angular_velocity.x));
        
        current_state = drone_ekf_->getState();
    }

    auto detection_msg = std::make_unique<vision_msgs::msg::Detection2D>();
    detection_msg->header.stamp = builtin_interfaces::msg::Time(rclcpp::Clock().now());
    detection_msg->header.frame_id = valid ? "camera_frame" : ""; // Empty frame_id indicates invalid detection
    detection_msg->bbox.center.position.x = current_state[0];
    detection_msg->bbox.center.position.y = current_state[1];
    detection_msg->bbox.size_x = current_state[2];
    detection_msg->bbox.size_y = current_state[3];
    detection_pub_->publish(std::move(detection_msg));
}

int main(int argc, char * argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<DroneTrackerNode>();
    node->run();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
