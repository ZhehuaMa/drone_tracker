#include "drone_tracker_node.h"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cv_bridge/cv_bridge.h>

using namespace std::chrono_literals;

static const std::string TOPIC_GIMBAL = "/fpv/gimbal";
static const std::string TOPIC_ODOM = "/fpv/odom";
static const std::string TOPIC_TARGET_BBOX = "/fpv/target_bbox";
static const std::string TOPIC_TRACKER_IMAGE = "/fpv/tracker_image";
static const std::string TOPIC_TRACKER_STATE =  "/fpv/tracker_state";
static const std::string TOPIC_TRACKER_TRIGGER = "/fpv/tracker_trigger";

DroneTrackerNode::DroneTrackerNode()
: Node("drone_tracker_node")
{
    declare_parameters();
    setup();
}

DroneTrackerNode::~DroneTrackerNode()
{
    stop();
    if (rosbag_writer_)
        rosbag_writer_->close();
}

void DroneTrackerNode::run()
{
    if (running) return;
    running = true;
    tracker_->reset();
    drone_ekf_->reset();
    detect_valid_ = false;
    last_tracker_state = -1;
    worker_thread_ = std::thread(&DroneTrackerNode::worker, this);
}

void DroneTrackerNode::stop()
{
    running = false;
    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }
}

void DroneTrackerNode::setup()
{
    // Initialize publishers, subscribers, and services here
    this->get_parameter("visualized_result", enable_image_output_);
    this->get_parameter("use_history_timestamp", use_history_timestamp_);

    int width_px, height_px, output_width_px, output_height_px, fps;
    float pixel_size_um, focal_length_mm;
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

    std::string video_source;
    this->get_parameter("video_source", video_source);
    is_camera_ = (video_source.substr(0, 5) == "video" && video_source.length() == 6);

    if (is_camera_)
    {
        RCLCPP_INFO(this->get_logger(), "Opening camera device: %s", video_source.c_str());
        std::string device = "/dev/";
        mv_camera_ = std::make_shared<MVCamera>(device.append(video_source), 
            width_px, height_px, fps, true, output_width_px, output_height_px, flip, mirror);
    }
    else
    {
        RCLCPP_INFO(this->get_logger(), "Opening video file: %s", video_source.c_str());
        mv_camera_ = std::make_shared<MVVideo>(video_source, use_history_timestamp_);
    }

    mv_camera_->get_image_size(output_width_px, output_height_px);

    // Assuming square pixels for simplicity
    float scale_ratio_w = static_cast<float>(output_width_px) / static_cast<float>(width_px);
    float scale_ratio_h = static_cast<float>(output_height_px) / static_cast<float>(height_px);

    cam_intrinsics_.fx = focal_length_mm * 1000.0 / pixel_size_um * scale_ratio_w;
    cam_intrinsics_.fy = focal_length_mm * 1000.0 / pixel_size_um * scale_ratio_h;
    cam_intrinsics_.px = output_width_px / 2.0;
    cam_intrinsics_.py = output_height_px / 2.0;

    double fov_deg_w = std::atan2(output_width_px * 0.5, cam_intrinsics_.fx) * 180.0 / M_PI * 2.0;
    double fov_deg_h = std::atan2(output_height_px * 0.5, cam_intrinsics_.fy) * 180.0 / M_PI * 2.0;

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
    this->get_parameter("tracker.yolo_recall_relax_ratio", tracker_settings.yolo_recall_relax_ratio);
    this->get_parameter("tracker.lost_max_count", tracker_settings.lost_max_count);
    this->get_parameter("tracker.tracking_roi_scale_factor", tracker_settings.roi_scale_factor);

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

    gimbal_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
        TOPIC_GIMBAL, rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
        std::bind(&DroneTrackerNode::imu_callback, this, std::placeholders::_1)
    );

    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        TOPIC_ODOM, 
        rclcpp::QoS(rclcpp::KeepLast(10)).best_effort(), 
        std::bind(&DroneTrackerNode::odom_callback, this, std::placeholders::_1));

    detection_pub_ = this->create_publisher<vision_msgs::msg::Detection2D>(
        TOPIC_TARGET_BBOX, 10);
    image_pub_ = this->create_publisher<sensor_msgs::msg::Image>(
        TOPIC_TRACKER_IMAGE, 10);
    tracker_state_pub_ = this->create_publisher<std_msgs::msg::Int8>(
        TOPIC_TRACKER_STATE, 10);

    status_publisher_ = std::make_unique<StatusPublisher>(*this, 2);
    
    trigger_sub_ = this->create_subscription<std_msgs::msg::Header>(
        TOPIC_TRACKER_TRIGGER, rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
        [this](const std_msgs::msg::Header::SharedPtr msg)
        {
            if (msg->frame_id == "ON" && !running)
            {
                // Generate a timestamped filename prefix
                std::time_t t = msg->stamp.sec;
                std::tm* tm_ptr = std::localtime(&t);
                char time_buffer[32];
                std::strftime(time_buffer, sizeof(time_buffer), "%Y%m%d_%H%M%S", tm_ptr);
                long microseconds = msg->stamp.nanosec / 1000;
                std::string filename_ts = std::string(time_buffer) + "_" + std::to_string(microseconds);
                
                bool need_save = false;
                this->get_parameter("need_save", need_save);
                std::string save_path;
                this->get_parameter("save_path", save_path);
                
                if (mv_camera_)
                {
                    auto camera = std::dynamic_pointer_cast<MVCamera>(mv_camera_);
                    if (camera)
                        camera->setup(need_save, save_path, filename_ts);
                }
                if (need_save)
                {
                    rosbag_writer_ = std::make_unique<rosbag2_cpp::Writer>();
                    rosbag_writer_->open(save_path + "/drone_rosbag_" + filename_ts);
                    rosbag_writer_->create_topic({TOPIC_ODOM,
                        "nav_msgs/msg/Odometry",
                        rmw_get_serialization_format(),
                        ""
                    });
                }

                this->run();
                status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1, "Drone Tracker Started", true);
            }
            else if (msg->frame_id == "OFF" && running)
            {
                this->stop();
                if (rosbag_writer_)
                {
                    rosbag_writer_->close();
                    rosbag_writer_.reset();
                }
                status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1, "Drone Tracker Stopped", true);
            }
        }
    );
}

void DroneTrackerNode::worker()
{
    bool success = mv_camera_->run_async();
    if (!success)
    {
        RCLCPP_ERROR(this->get_logger(), "Failed to open MV Camera ");
        return;
    }
    RCLCPP_INFO(this->get_logger(), "MV Camera opened successfully");

    cv::Mat frame;

    int count = 0;
    double avg_time_ms = 0.0;

    // Main processing loop
    while (rclcpp::ok() && running)
    {
        // Process the frame with tracker_ and drone_ekf_
        rclcpp::Time start_time = this->now();
        std::shared_ptr<VPIImageWrapper> vpi_buffer;
        int64_t frame_ts = 0;

        if (mv_camera_->getImageQueue().wait_dequeue_timed(vpi_buffer, 1s)) {
            start_time = this->now();
            frame_ts = vpi_buffer->get_timestamps();
            tracker_->update(vpi_buffer);
        }
        else {
            std::cout << "No frame received from camera." << std::endl;
            break;
        }

        // --- 获取结果并绘制 ---
        auto bbox = tracker_->getCurrentBox();
        {
            std::lock_guard<std::mutex> lock(ekf_mtx_);
            detect_valid_ = bbox.is_valid;
            if (detect_valid_)
            {
                drone_ekf_->update(
                    frame_ts,
                    Eigen::Vector4d(bbox.cx, bbox.cy, bbox.width, bbox.height)
                );
            }
        }
        
        auto elapsed = this->now() - start_time;
        double cost_ms = elapsed.seconds() * 1000;

        // Update tracker state and publish if changed
        auto [tracker_state, state_text] = tracker_->getStateText();
        int8_t tracker_state_int = static_cast<int8_t>(tracker_state);
        if (tracker_state_int != last_tracker_state)
        {
            status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1, state_text, true);
            auto msg = std::make_unique<std_msgs::msg::Int8>();
            msg->data = tracker_state_int;
            tracker_state_pub_->publish(std::move(msg));
            last_tracker_state = tracker_state_int;
        }
        
        if (enable_image_output_) 
        {
            cv::Rect display_box = {
                static_cast<int>(bbox.cx - bbox.width / 2), 
                static_cast<int>(bbox.cy - bbox.height / 2),
                static_cast<int>(bbox.width), 
                static_cast<int>(bbox.height) };
            std::string info = tracker_->getStatus();
            auto color_val = tracker_->getColor();
            cv::Scalar color = cv::Scalar((color_val & 0xFF), ((color_val >> 8) & 0xFF), ((color_val >> 16) & 0xFF));
            
            // Convert VPI image to OpenCV Mat
            cv::Mat mono;
            VPIImageData image_data;
            vpiImageLockData(vpi_buffer->get(), VPI_LOCK_READ, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &image_data);
            vpiImageDataExportOpenCVMat(image_data, &mono);
            vpiImageUnlock(vpi_buffer->get());
            cv::merge(std::vector<cv::Mat>{mono, mono, mono}, frame);

            if (bbox.is_valid)
            {
                cv::rectangle(frame, display_box, color, 2, 1);
            }

            cv::putText(frame, "Cost: " + std::to_string(int(cost_ms)) + " ms", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);
            cv::putText(frame, info, cv::Point(20, 80), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);

            cv_bridge::CvImage cv_img;
            cv_img.header.stamp = rclcpp::Time(frame_ts*1000);
            cv_img.header.frame_id = "camera_frame";
            cv_img.encoding = "bgr8";
            cv_img.image = frame;
            auto img_msg = std::make_unique<sensor_msgs::msg::Image>();
            cv_img.toImageMsg(*img_msg);
            if (rosbag_writer_)
            {
                rosbag_writer_->write(*img_msg, TOPIC_TRACKER_IMAGE, rclcpp::Time(frame_ts*1000));
            }
            image_pub_->publish(std::move(img_msg));

            elapsed = this->now() - start_time;
            cost_ms = elapsed.seconds() * 1000;
        }

        avg_time_ms = (avg_time_ms * count + cost_ms) / (count + 1);
        ++count;

        // 计算剩余时间以保持帧率
        int remaining_time_us = static_cast<int>((interval_ms_ - cost_ms) * 1000);
        if (remaining_time_us <= 0) remaining_time_us = 1000; // 至少等待1ms

        rclcpp::sleep_for(std::chrono::microseconds(remaining_time_us));
    }

    std::cout << "Average tracking time per frame: " << std::fixed << std::setprecision(2) << avg_time_ms << " ms" << std::endl;

    mv_camera_->stop();
}

void DroneTrackerNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    if (rosbag_writer_)
    {
        rosbag_writer_->write(*msg, TOPIC_ODOM, rclcpp::Time(msg->header.stamp));
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
        drone_ekf_->predict(rclcpp::Time(msg->header.stamp).nanoseconds() / 1000, 
            Eigen::Vector3d(
                -msg->angular_velocity.y,
                -msg->angular_velocity.z,
                 msg->angular_velocity.x));
        
        current_state = drone_ekf_->getState();
    }

    auto detection_msg = std::make_unique<vision_msgs::msg::Detection2D>();
    detection_msg->header.stamp = this->now();
    detection_msg->header.frame_id = valid ? "camera_frame" : ""; // Empty frame_id indicates invalid detection
    detection_msg->bbox.center.position.x = current_state[0];
    detection_msg->bbox.center.position.y = current_state[1];
    detection_msg->bbox.size_x = current_state[2];
    detection_msg->bbox.size_y = current_state[3];

    if (rosbag_writer_)
    {
        rosbag_writer_->write(*msg, TOPIC_GIMBAL, rclcpp::Time(msg->header.stamp));
        rosbag_writer_->write(*detection_msg, TOPIC_TARGET_BBOX, rclcpp::Time(detection_msg->header.stamp));
    }

    detection_pub_->publish(std::move(detection_msg));
}

StatusPublisher::StatusPublisher(rclcpp::Node &node, int hz)
{
    status_pub_ = node.create_publisher<mavros_msgs::msg::StatusText>(
        "/mavros/statustext/send", 10);
    status_timer_ = node.create_wall_timer(std::chrono::milliseconds(1000 / hz), [this](){
        static uint8_t severity;
        static std::string status;

        std::lock_guard<std::mutex> lock(status_mtx_);
        if (status_counter_<=0)
        {
            if (status_queue_.empty()) return;
            auto [level, counter, text] = status_queue_.front();
            severity = level;
            status = text;
            status_counter_ = counter;
            // printf("Publishing status: severity=%d, count=%d, text=%s\n", severity, status_counter_, status.c_str());
            status_queue_.pop();
        }
        if (status_counter_-- > 0)
        {
            // Publish status
            auto msg = std::make_unique<mavros_msgs::msg::StatusText>();
            msg->header.stamp = rclcpp::Clock().now();
            msg->severity = severity; // mavros_msgs::msg::StatusText::WARNING;
            msg->text= status;
            status_pub_->publish(std::move(msg));
        }
    });
}

StatusPublisher::~StatusPublisher()
{
    if (status_timer_)
        status_timer_->cancel();
}

bool StatusPublisher::publish(uint8_t severity, int count,const std::string& text, bool force)
{
    std::lock_guard<std::mutex> lock(status_mtx_);
    if (!force && (status_counter_ > 0 || !status_queue_.empty()))
        return false;
    status_queue_.emplace(severity, count, text);
    return true;
}

int main(int argc, char * argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<DroneTrackerNode>();
    // node->run();
    rclcpp::spin(node);
    node.reset();
    rclcpp::shutdown();
    return 0;
}
