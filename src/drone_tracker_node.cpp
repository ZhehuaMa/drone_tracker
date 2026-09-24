#include "drone_tracker_node.h"

#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "platform/frame_source_ops.h"
#ifdef DRONE_TRACKER_RK3576
#include "platform/file_identity.h"
#endif
#include "osd/config.h"
#ifdef DRONE_TRACKER_HAS_OSD
#include "platform/rk3576/osd/hdmi_backend.h"
#endif

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cv_bridge/cv_bridge.h>

using namespace std::chrono_literals;

static const std::string TOPIC_GIMBAL = "/fpv/gimbal";
static const std::string TOPIC_ODOM = "/fpv/odom";
static const std::string TOPIC_TARGET_BBOX = "/fpv/target_bbox";
static const std::string TOPIC_TRACKER_IMAGE = "/fpv/tracker_image";
static const std::string TOPIC_TRACKER_STATE =  "/fpv/tracker_state";
static const std::string TOPIC_TRACKER_TRIGGER = "/fpv/tracker_trigger";

DroneTrackerNode::DroneTrackerNode(const rclcpp::NodeOptions &options,
    std::unique_ptr<PlatformFactory> factory,
    std::function<std::thread(std::function<void()>)> launcher, std::function<void(const char *)> recording_hook,
    std::unique_ptr<osd::DisplayBackend> display_backend)
    : Node("drone_tracker_node", options), recording_hook_{std::move(recording_hook)}, launcher_{std::move(launcher)} {
    if (!factory) { factory = CreatePlatformFactory(); }
    if (!launcher_) {
        launcher_ = [](std::function<void()> work) { return std::thread{std::move(work)}; };
    }
    declare_parameters();
    setupDisplay(std::move(display_backend));
    setup(*factory);
    parameter_guard_ = add_on_set_parameters_callback([](const std::vector<rclcpp::Parameter> &parameters) {
        rcl_interfaces::msg::SetParametersResult result{};
        result.successful = true;
        for (const auto &parameter : parameters) {
            const auto &name{parameter.get_name()};
            if (name.rfind("recording.", 0) == 0) {
                result.successful = false;
                result.reason = "Recording startup parameters cannot change at runtime; restart the node: " + name;
                break;
            }
            if (name.rfind("osd.", 0) == 0) {
                result.successful = false;
                result.reason = "OSD startup parameters cannot change at runtime: " + name;
                break;
            }
#ifdef DRONE_TRACKER_RK3576
            if (name == "mv_camera.sensor_model") {
                result.successful = false;
                result.reason = "mv_camera.sensor_model cannot change at runtime; restart the node to apply it";
                break;
            }
            if (name == "use_sim_time" || name == "use_history_timestamp" || name == "video_source"
                || name == "need_save" || name == "save_path" || name == "visualized_result"
                || name.rfind("tracker.", 0) == 0 || name.rfind("camera.", 0) == 0
                || name.rfind("mv_camera.", 0) == 0) {
                result.successful = false;
                result.reason = "RK3576 startup configuration is fixed; system clock only: " + name;
                break;
            }
#endif
        }
        return result;
    });
}

DroneTrackerNode::~DroneTrackerNode() {
    stop();
    osd_telemetry_.reset();
    if (osd_presenter_) {
        osd_presenter_->shutdown();
        osd_presenter_->reportTiming();
    }
    if (mv_camera_) { mv_camera_->stop(); }
    tracker_.reset();
    closeRecording();
}

void DroneTrackerNode::run() { run(static_cast<builtin_interfaces::msg::Time>(now())); }

void DroneTrackerNode::run(const builtin_interfaces::msg::Time &stamp) {
    std::lock_guard<std::mutex> control{control_mutex_};
    if (run_state_.load() != RunState::Idle) { return; }
    if (worker_thread_.joinable()) { worker_thread_.join(); }
    run_state_ = RunState::Starting;
    RCLCPP_INFO(get_logger(), "Round starting");
    recording_failed_ = false;
    try {
        tracker_->finishRound();
        tracker_->reset();
        last_tracker_state = -1;
        consumed_frames_ = 0;
        last_acquisition_report_ = std::chrono::steady_clock::now();
        last_camera_phase_.clear();
        last_camera_recording_phase_.clear();
        running = true;
        if (osd_presenter_) {
            osd_round_ = osd_presenter_->beginRound();
            osd_sequence_ = 0;
        }
        openRecording(stamp);
        worker_thread_ = launcher_([this] { worker(); });
        if (!worker_thread_.joinable()) { throw std::runtime_error("Thread launcher returned no worker"); }
    } catch (const std::exception &error) {
        running = false;
        if (osd_presenter_) { osd_presenter_->endRound(osd_round_, osd::DisplayStatus::InputFailure); }
        closeRecording();
        run_state_ = RunState::Idle;
        RCLCPP_ERROR(get_logger(), "Round start failed: %s", error.what());
    }
}

void DroneTrackerNode::stop() {
    const auto before{std::chrono::steady_clock::now()};
    std::lock_guard<std::mutex> control{control_mutex_};
    if (osd_presenter_) { osd_presenter_->endRound(osd_round_, osd::DisplayStatus::Stopped); }
    running = false;
    wait_cv_.notify_all();
    if (worker_thread_.joinable()) { worker_thread_.join(); }
    if (camera_input_) {
        RCLCPP_INFO(get_logger(), "Node OFF total_us=%ld", static_cast<long>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - before).count()));
    }
}

void DroneTrackerNode::openRecording(const builtin_interfaces::msg::Time &stamp) {
    const bool enabled{get_parameter("need_save").as_bool()};
    const auto directory{get_parameter("save_path").as_string()};
    const std::string prefix{std::to_string(stamp.sec) + "_" + std::to_string(stamp.nanosec)};
    configureFrameSourceRecording(*mv_camera_, {enabled, directory, prefix});
    std::lock_guard<std::mutex> lock{bag_mutex_};
    if (!enabled) { return; }
    const auto path{std::filesystem::path{directory} / ("drone_rosbag_" + prefix)};
    if (std::filesystem::exists(path)) { throw std::runtime_error("Recording directory already exists: " + path.string()); }
    if (recording_hook_) { recording_hook_("open"); }
    rosbag_writer_ = std::make_unique<rosbag2_cpp::Writer>();
    rosbag_writer_->open(path.string());
    rosbag_writer_->create_topic({TOPIC_ODOM, "nav_msgs/msg/Odometry", rmw_get_serialization_format(), ""});
}

void DroneTrackerNode::closeRecording() noexcept {
    std::lock_guard<std::mutex> lock{bag_mutex_};
    if (!rosbag_writer_) { return; }
    try {
        rosbag_writer_->close();
    } catch (const std::exception &error) {
        recording_failed_ = true;
        RCLCPP_ERROR(get_logger(), "Recording close failed: %s", error.what());
    }
    rosbag_writer_.reset();
}

void DroneTrackerNode::setupDisplay(std::unique_ptr<osd::DisplayBackend> backend) {
    osd::DisplayConfig config{};
    config.timing_enabled = get_parameter("osd.timing_enabled").as_bool();
    config.drm_device = get_parameter("osd.drm_device").as_string();
    config.connector = get_parameter("osd.connector").as_string();
    config.refresh_hz = get_parameter("osd.refresh_hz").as_double();
    config.craft_name = get_parameter("osd.craft_name").as_string();
    const auto width{get_parameter("osd.width").as_int()}, height{get_parameter("osd.height").as_int()};
    const auto stale{get_parameter("osd.frame_stale_ms").as_int()};
    const auto timeout{get_parameter("osd.telemetry_timeout_ms").as_int()};
    const auto cells{get_parameter("osd.battery_cells").as_int()};
    if (width < 2 || height < 2 || width > 4096 || height > 4096 || width % 2 || height % 2
        || !std::isfinite(config.refresh_hz) || config.refresh_hz < 1 || config.refresh_hz > 240
        || stale <= 0 || stale > 3600000 || timeout <= 0 || timeout > 3600000 || cells < 0 || cells > 32
        || config.drm_device.empty() || config.connector.empty() || config.craft_name.size() > 24
        || !std::all_of(config.craft_name.begin(), config.craft_name.end(),
                       [](unsigned char c) { return c >= 32 && c <= 126; })) {
        throw std::invalid_argument("Invalid osd.* startup configuration");
    }
    if (!get_parameter("osd.enabled").as_bool()) { return; }
    config.width = static_cast<int>(width); config.height = static_cast<int>(height);
    if (!backend) {
#ifdef DRONE_TRACKER_HAS_OSD
        backend = rk3576::createHdmiBackend(config);
#else
        throw std::invalid_argument("osd.enabled requires DRONE_TRACKER_BUILD_OSD=ON");
#endif
    }
    std::function<void(const osd::TimingWindow &)> timing_sink{};
    if (config.timing_enabled) {
        timing_sink = [this](const osd::TimingWindow &window) {
            const auto message{osd::formatTiming(window)};
            RCLCPP_INFO(get_logger(), "%s", message.c_str());
        };
    }
    osd_presenter_ = std::make_unique<osd::OsdPresenter>(std::move(backend), std::chrono::milliseconds{stale},
        osd::SteadyClock::now, std::move(timing_sink));
    osd_presenter_->start();
    osd_telemetry_ = std::make_unique<osd::OsdTelemetryAdapter>(*this,
        [this](osd::TelemetrySnapshot snapshot) {
            osd_presenter_->updateTelemetry(std::move(snapshot));
            const auto failure{osd_presenter_->failure()};
            if (!failure.empty() && !osd_failure_reported_) {
                osd_failure_reported_ = true;
                osd_presenter_->reportTiming();
                RCLCPP_ERROR(get_logger(), "OSD disabled after display failure: %s; tracking continues", failure.c_str());
            }
            const auto now{osd::SteadyClock::now()};
            if (now - osd_last_report_ >= std::chrono::seconds{1}) {
                osd_last_report_ = now;
                osd_presenter_->reportTiming();
                const auto counts{osd_presenter_->counters()};
                RCLCPP_INFO(get_logger(), "OSD submitted=%lu replaced=%lu displayed=%lu commits=%lu rejected=%lu",
                    static_cast<unsigned long>(counts.submitted), static_cast<unsigned long>(counts.replaced),
                    static_cast<unsigned long>(counts.displayed), static_cast<unsigned long>(counts.refreshes),
                    static_cast<unsigned long>(counts.rejected));
                const auto calls{osd_submit_calls_.load()};
                if (calls > 0) {
                    RCLCPP_INFO(get_logger(), "OSD submit_calls=%lu submit_mean_us=%.3f submit_max_us=%.3f",
                        static_cast<unsigned long>(calls), osd_submit_ns_.load() / (1000.0 * calls),
                        osd_submit_max_ns_.load() / 1000.0);
                }
            }
        }, std::chrono::milliseconds{timeout}, static_cast<int>(cells));
}

void DroneTrackerNode::setup(PlatformFactory &factory)
{
    // Initialize publishers, subscribers, and services here
    this->get_parameter("visualized_result", enable_image_output_);
    this->get_parameter("use_history_timestamp", use_history_timestamp_);

#ifdef DRONE_TRACKER_RK3576
    for (const auto *name : {"use_sim_time", "use_history_timestamp"}) {
        if (get_parameter(name).as_bool()) { throw std::invalid_argument(std::string{name} + " must be false"); }
    }
    double *values[]{&cam_intrinsics_.fx, &cam_intrinsics_.fy, &cam_intrinsics_.px, &cam_intrinsics_.py};
    const char *names[]{"camera.fx", "camera.fy", "camera.cx", "camera.cy"};
    for (int i{0}; i < 4; ++i) {
        *values[i] = get_parameter(names[i]).as_double();
        if (!std::isfinite(*values[i]) || (i < 2 && *values[i] <= 0)) {
            throw std::invalid_argument(std::string{names[i]} + " requires a finite explicit pixel value; fx/fy > 0");
        }
    }
#endif

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
#ifdef DRONE_TRACKER_RK3576
    const bool shorthand{video_source.rfind("video", 0) == 0 && video_source.size() > 5
        && std::all_of(video_source.begin() + 5, video_source.end(), [](char c) { return c >= '0' && c <= '9'; })};
    const bool is_camera{shorthand || video_source.rfind("/dev/", 0) == 0};
#else
    const bool is_camera = (video_source.substr(0, 5) == "video" && video_source.length() == 6);
#endif
#ifdef DRONE_TRACKER_RK3576
    camera_input_ = is_camera;
#endif

    FrameSourceConfig source_config{};
    source_config.type = is_camera ? FrameSourceType::Camera : FrameSourceType::Video;
    auto &recording{source_config.camera_recording};
    recording.enabled = get_parameter("recording.enabled").as_bool();
#ifndef DRONE_TRACKER_RK3576
    if (recording.enabled) { throw std::invalid_argument("Independent recording requires RK3576 SC233HGS"); }
#endif
    recording.directory = get_parameter("recording.directory").as_string();
    recording.bitrate_bps = get_parameter("recording.bitrate_bps").as_int();
    const auto segment{get_parameter("recording.segment_seconds").as_int()};
    const auto interval{get_parameter("recording.sync_interval_ms").as_int()};
    const auto maximum{get_parameter("recording.max_total_bytes").as_int()};
    const auto minimum{get_parameter("recording.min_free_bytes").as_int()};
    if (segment < 10 || segment > 600 || (interval != 500 && interval != 1000 && interval != 2000)
        || recording.bitrate_bps <= 0 || recording.bitrate_bps > INT32_MAX || maximum <= 0 || minimum <= 0) {
        throw std::invalid_argument("Invalid recording numeric startup parameter");
    }
    recording.segment_seconds = static_cast<int>(segment); recording.sync_interval_ms = static_cast<int>(interval);
    recording.max_total_bytes = maximum; recording.min_free_bytes = minimum;
#ifdef DRONE_TRACKER_RK3576
    source_config.path = shorthand ? "/dev/" + video_source : video_source;
    if (is_camera) {
        if (get_parameter("need_save").as_bool()) {
            throw std::invalid_argument("RK3576 camera requires need_save=false; use recording.enabled for SC233HGS");
        }
        const auto mode{get_parameter("mv_camera.capture_mode").as_string()};
        if (mode != "continuous" && mode != "software_trigger") {
            throw std::invalid_argument("mv_camera.capture_mode requires continuous or software_trigger");
        }
        source_config.trigger_mode = mode == "software_trigger";
        const auto sensor_model{get_parameter("mv_camera.sensor_model").as_string()};
        if (sensor_model == "ar0234") {
            source_config.camera_sensor_model = CameraSensorModel::Ar0234;
        } else if (sensor_model == "sc233hgs") {
            source_config.camera_sensor_model = CameraSensorModel::Sc233hgs;
            if (source_config.trigger_mode) {
                throw std::invalid_argument(
                    "mv_camera.sensor_model=sc233hgs does not support software trigger; use continuous");
            }
        } else {
            throw std::invalid_argument(
                "mv_camera.sensor_model must be ar0234 or sc233hgs; requested: " + sensor_model);
        }
        source_config.camera_trigger_rate_hz = get_parameter("mv_camera.trigger_rate_hz").as_double();
        source_config.camera_media_device = get_parameter("mv_camera.media_device").as_string();
        source_config.camera_sensor_entity = get_parameter("mv_camera.sensor_entity").as_string();
        source_config.camera_buffer_count = static_cast<int>(get_parameter("mv_camera.buffer_count").as_int());
        const double rate{source_config.camera_trigger_rate_hz};
        if (source_config.camera_media_device.empty() || source_config.camera_sensor_entity.empty()
            || !std::isfinite(rate) || (source_config.trigger_mode ? rate < 1 || rate > fps : rate != 0)) {
            throw std::invalid_argument("Camera requires media_device, sensor_entity and mode-compatible trigger_rate_hz");
        }
    }
#else
    source_config.path = is_camera ? "/dev/" + video_source : video_source;
#endif
    source_config.width = width_px;
    source_config.height = height_px;
    source_config.fps = fps;
    source_config.output_width = output_width_px;
    source_config.output_height = output_height_px;
    source_config.flip = flip;
    source_config.mirror = mirror;
    source_config.use_history_timestamp = use_history_timestamp_;
#ifdef DRONE_TRACKER_RK3576
    if (recording.enabled) {
        if (!is_camera || source_config.camera_sensor_model != CameraSensorModel::Sc233hgs) {
            throw std::invalid_argument("recording.enabled requires SC233HGS camera input");
        }
        try {
            std::ostringstream identity{};
            identity << "program_sha256=" << recordingFileIdentity("/proc/self/exe") << '\n';
            auto names{list_parameters({}, 100).names};
            std::sort(names.begin(), names.end());
            for (const auto &name : names) {
                identity << name << '=' << get_parameter(name).value_to_string() << '\n';
            }
            const std::filesystem::path models{
                std::filesystem::path{ament_index_cpp::get_package_share_directory(ROS_PACKAGE_NAME)} / "models"};
            for (const char *name : {"tracker.yolo_engine", "tracker.nano_template_backbone",
                                     "tracker.nano_search_backbone", "tracker.nano_head"}) {
                std::filesystem::path path{get_parameter(name).as_string()};
                if (path.is_relative()) { path = models / path; }
                identity << name << " path=" << path.string()
                    << " sha256=" << recordingFileIdentity(path.string()) << '\n';
            }
            recording.identity = identity.str();
        } catch (const std::exception &error) {
            recording.preparation_error = error.what();
        }
    }
#endif
    RCLCPP_INFO(this->get_logger(), "Opening %s: %s", is_camera ? "camera device" : "video file", video_source.c_str());
    auto source{factory.createFrameSource(source_config)};
    if (!source) { throw std::runtime_error(source.error.describe()); }
    mv_camera_ = std::move(source.value);
    mv_camera_->getImageSize(output_width_px, output_height_px);

#ifndef DRONE_TRACKER_RK3576
    // Assuming square pixels for simplicity
    float scale_ratio_w = static_cast<float>(output_width_px) / static_cast<float>(width_px);
    float scale_ratio_h = static_cast<float>(output_height_px) / static_cast<float>(height_px);

    cam_intrinsics_.fx = focal_length_mm * 1000.0 / pixel_size_um * scale_ratio_w;
    cam_intrinsics_.fy = focal_length_mm * 1000.0 / pixel_size_um * scale_ratio_h;
    cam_intrinsics_.px = output_width_px / 2.0;
    cam_intrinsics_.py = output_height_px / 2.0;

#endif
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
    this->get_parameter("tracker.yolo_conf_threshold", tracker_settings.yolo_conf_threshold);
    this->get_parameter("tracker.yolo_nms_threshold", tracker_settings.yolo_nms_threshold);
    this->get_parameter("tracker.yolo_interval", tracker_settings.yolo_interval);
    this->get_parameter("tracker.iou_reset_threshold", tracker_settings.iou_reset_threshold);
    this->get_parameter("tracker.iou_calibrate_threshold", tracker_settings.iou_calibrate_threshold);
    this->get_parameter("tracker.yolo_recall_relax_ratio", tracker_settings.yolo_recall_relax_ratio);
    this->get_parameter("tracker.max_position_jump", tracker_settings.max_position_jump);
    this->get_parameter("tracker.max_size_jump_ratio", tracker_settings.max_size_jump_ratio);
    this->get_parameter("tracker.max_box_area_ratio", tracker_settings.max_box_area_ratio);
    this->get_parameter("tracker.lost_max_count", tracker_settings.lost_max_count);
    this->get_parameter("tracker.tracking_roi_scale_factor", tracker_settings.roi_scale_factor);
    this->get_parameter("tracker.frame_buffer_size", tracker_settings.frame_buffer_size);
    this->get_parameter("tracker.jump_tolerance", tracker_settings.jump_tolerance);
    this->get_parameter("tracker.enable_dcf", tracker_settings.enable_dcf);
    this->get_parameter("tracker.dcf_fixed_template", tracker_settings.dcf.fixed_template);
    this->get_parameter("tracker.dcf_learning_rate", tracker_settings.dcf.learning_rate);
    this->get_parameter("tracker.dcf_lambda", tracker_settings.dcf.lambda_reg);
    this->get_parameter("tracker.dcf_psr_thresh", tracker_settings.dcf.psr_thresh);
    this->get_parameter("tracker.dcf_max_div_factor", tracker_settings.dcf_max_div_factor);
    this->get_parameter("tracker.kf_r_pos", tracker_settings.kf_r_pos);
    this->get_parameter("tracker.kf_r_size", tracker_settings.kf_r_size);
    this->get_parameter("tracker.kf_q_a", tracker_settings.kf_q_a);
    this->get_parameter("tracker.kf_q_size", tracker_settings.kf_q_size);

    // KF 内参来自相机参数（Python 端 camera.fov_deg 家族的等价物）
    tracker_settings.kf_fx = cam_intrinsics_.fx;
    tracker_settings.kf_fy = cam_intrinsics_.fy;
    tracker_settings.kf_cx0 = cam_intrinsics_.px;
    tracker_settings.kf_cy0 = cam_intrinsics_.py;

    auto resolve{[&share_dir](const std::string &value) {
        if (value.empty()) { return value; }
        const std::filesystem::path path{value};
        return path.is_absolute() ? path.string() : (std::filesystem::path{share_dir} / "models" / path).string();
    }};
    ModelPaths paths{tracker_settings.yolo_engine_path, tracker_settings.nanotrack_backbone_path,
        tracker_settings.nanotrack_head_path, get_parameter("tracker.nano_template_backbone").as_string(),
        get_parameter("tracker.nano_search_backbone").as_string()};
#ifdef DRONE_TRACKER_RK3576
    if (!paths.backbone.empty()) { throw std::invalid_argument("tracker.nano_backbone must be empty on RK3576"); }
    if (paths.template_backbone.empty()) { throw std::invalid_argument("tracker.nano_template_backbone is required"); }
    if (paths.search_backbone.empty()) { throw std::invalid_argument("tracker.nano_search_backbone is required"); }
#else
    if (!paths.template_backbone.empty() || !paths.search_backbone.empty()) {
        throw std::invalid_argument("Jetson requires shared tracker.nano_backbone, no split fields");
    }
    if (paths.backbone.empty()) { throw std::invalid_argument("tracker.nano_backbone is required"); }
#endif
    if (paths.yolo.empty()) { throw std::invalid_argument("tracker.yolo_engine is required"); }
    if (paths.head.empty()) { throw std::invalid_argument("tracker.nano_head is required"); }
    paths.yolo = resolve(paths.yolo);
    paths.backbone = resolve(paths.backbone);
    paths.head = resolve(paths.head);
    paths.template_backbone = resolve(paths.template_backbone);
    paths.search_backbone = resolve(paths.search_backbone);
    auto components{factory.createTrackerComponents(paths)};
    if (!components) { throw std::runtime_error(components.error.describe()); }
    tracker_ = HybridTracker::CreateInstance(tracker_settings, std::move(components.value));
    if (!tracker_->init())
    {
        throw std::runtime_error("Failed to initialize HybridTracker; check model contracts and target class");
    }

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
            if (msg->frame_id == "ON") {
                run(msg->stamp);
            } else if (msg->frame_id == "OFF") {
                stop();
            }
        }
    );
}

void DroneTrackerNode::worker() {
    try {
        if (running) {
            if (!mv_camera_->start()) {
                const auto status{mv_camera_->status()};
                throw std::runtime_error("Input start failed [" + status.path + "]: " + status.reason);
            }
            run_state_ = RunState::Running;
            status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1, "Drone Tracker Started", true);
            processFrames();
        }
    } catch (const std::exception &error) {
        RCLCPP_ERROR(get_logger(), "Round failed: %s", error.what());
        if (osd_presenter_) { osd_presenter_->endRound(osd_round_, osd::DisplayStatus::InputFailure); }
    }
    run_state_ = RunState::Stopping;
    running = false;
    const auto stop_begin{std::chrono::steady_clock::now()};
    const bool source_stopped{mv_camera_->stop()};
    if (!source_stopped) { RCLCPP_ERROR(get_logger(), "Input cleanup failed: %s", mv_camera_->status().reason.c_str()); }
    tracker_->finishRound();
    reportAcquisition(true);
    if (camera_input_) {
        RCLCPP_INFO(get_logger(), "Round cleanup including model wait_us=%ld", static_cast<long>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - stop_begin).count()));
    }
    closeRecording();
    if (recording_failed_) { RCLCPP_ERROR(get_logger(), "Round ended: recording failure"); }
    if (osd_presenter_) {
        osd_presenter_->endRound(osd_round_, recording_failed_ ? osd::DisplayStatus::InputFailure
                                                             : osd::DisplayStatus::Stopped);
    }
    status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1, "Drone Tracker Stopped", true);
    run_state_ = RunState::Idle;
}

void DroneTrackerNode::reportAcquisition(bool final) {
#ifdef DRONE_TRACKER_RK3576
    if (!camera_input_) { return; }
    const auto now{std::chrono::steady_clock::now()};
    const auto state{mv_camera_->status()};
    const bool transition{state.camera.phase != last_camera_phase_
        || (state.recording.phase == "failed" && last_camera_recording_phase_ != "failed")};
    if (!final && !transition && now - last_acquisition_report_ < 1s) { return; }
    last_camera_phase_ = state.camera.phase;
    last_acquisition_report_ = now;
    const auto &recording{state.recording};
    if (recording.enabled) {
        const auto lag{recording.synced_known
            ? std::to_string(recording.latest_frame_us - recording.last_synced_frame_us) : "unknown"};
        RCLCPP_INFO(get_logger(),
            "Camera recording phase=%s round=%lu attempted=%lu accepted=%lu dropped=%lu cancelled=%lu "
            "encoded=%lu written=%lu synced=%lu lag_us=%s last_sync_mono_us=%ld sync_us=%ld "
            "used_bytes=%lu free_bytes=%lu prepare_us=%ld stop_us=%ld session=%s reason=%s",
            recording.phase.c_str(), static_cast<unsigned long>(recording.round),
            static_cast<unsigned long>(recording.attempted), static_cast<unsigned long>(recording.accepted),
            static_cast<unsigned long>(recording.dropped), static_cast<unsigned long>(recording.cancelled),
            static_cast<unsigned long>(recording.encoded), static_cast<unsigned long>(recording.written),
            static_cast<unsigned long>(recording.synced), lag.c_str(), static_cast<long>(recording.last_sync_monotonic_us),
            static_cast<long>(recording.sync_duration_us), static_cast<unsigned long>(recording.total_bytes),
            static_cast<unsigned long>(recording.free_bytes), static_cast<long>(recording.prepare_us),
            static_cast<long>(recording.stop_us), recording.session.c_str(), recording.reason.c_str());
        if (recording.phase == "failed" && last_camera_recording_phase_ != "failed") {
            status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1,
                "Camera recording failed: " + recording.reason, true);
        }
        last_camera_recording_phase_ = recording.phase;
    }
    RCLCPP_INFO(get_logger(),
        "Camera %s mode=%s requested_hz=%.3f actual_hz=%.3f commands=%lu command_errors=%lu skipped=%lu "
        "read=%lu enqueued=%lu queue_dropped=%lu sequence_gaps=%lu consumed=%lu time=%s "
        "unattributed_command_frame_difference=%ld clock_offset_delta_us=%ld stop_us=%ld restored=%d state=%d reason=%s",
        final ? "final" : "running", state.software_trigger ? "software_trigger" : "continuous",
        state.requested_trigger_hz, state.actual_trigger_hz, static_cast<unsigned long>(state.trigger_success),
        static_cast<unsigned long>(state.trigger_errors), static_cast<unsigned long>(state.trigger_skipped),
        static_cast<unsigned long>(state.read), static_cast<unsigned long>(state.enqueued),
        static_cast<unsigned long>(state.dropped), static_cast<unsigned long>(state.sequence_gaps),
        static_cast<unsigned long>(consumed_frames_), state.time_source.c_str(),
        static_cast<long>(state.trigger_success) - static_cast<long>(state.read),
        static_cast<long>(state.clock_offset_delta_us), static_cast<long>(state.stop_us), state.controls_restored, static_cast<int>(state.state), state.reason.c_str());
    const auto &camera{state.camera};
    if (camera.phase != "disabled") {
        const std::string first{camera.first_frame_known
            ? std::to_string(camera.first_sequence) + ":" + std::to_string(camera.first_timestamp_us)
                + " latency_us=" + std::to_string(camera.first_latency_us)
            : "unknown"};
        RCLCPP_INFO(get_logger(),
            "Camera warmup phase=%s limits=2000000us/120frames/8consecutive elapsed_us=%ld "
            "dequeued_total=%lu examined=%u discarded=%lu repeats=%u streak=%u terminal_discarded=%lu "
            "first=%s reclaimed=%d reason=%s",
            camera.phase.c_str(), static_cast<long>(camera.warmup_us),
            static_cast<unsigned long>(camera.dequeued_total), camera.warmup_examined,
            static_cast<unsigned long>(camera.warmup_discarded), camera.warmup_repeats, camera.warmup_streak,
            static_cast<unsigned long>(camera.terminal_discarded), first.c_str(), camera.resources_reclaimed,
            camera.reason.c_str());
    }
#else
    (void)final;
#endif
}

void DroneTrackerNode::processFrames() {
    static const std::unordered_map<int, std::string> state_text_map = {
        {0, "Detecting..."},
        {1, "Target Found"},
        {2, "Tracking..."},
        {3, "Target Lost"}
    };

    cv::Mat frame;

    int count = 0;
    double avg_time_ms = 0.0;

    // Main processing loop
    while (rclcpp::ok() && running)
    {
        reportAcquisition(false);
        rclcpp::Time start_time = this->now();
        FrameRef frame_buffer;

#ifdef DRONE_TRACKER_RK3576
        constexpr auto frame_timeout{20ms};
#else
        constexpr auto frame_timeout{1s};
#endif
        if (!waitForLatestFrame(*mv_camera_, frame_buffer, frame_timeout, [&] { start_time = this->now(); })) {
            const auto status{mv_camera_->status()};
            if (status.state == FrameSourceState::Running) { continue; }
            if (status.state == FrameSourceState::Failed) {
                throw std::runtime_error("Input failed [" + status.path + "] frame="
                    + std::to_string(status.frame_index) + ": " + status.reason);
            }
            // EOF 已在最后入队之后发布；再取一次避免超时和最后入队竞争。
            if (status.state != FrameSourceState::Eof || !mv_camera_->tryFrame(frame_buffer)) {
                RCLCPP_INFO(get_logger(), "Input ended: %s", status.state == FrameSourceState::Eof ? "EOF" : "stopped/unknown");
                if (osd_presenter_) {
                    osd_presenter_->endRound(osd_round_, status.state == FrameSourceState::Eof
                        ? osd::DisplayStatus::Eof : osd::DisplayStatus::InputFailure);
                }
                break;
            }
            FrameRef newer{};
            while (mv_camera_->tryFrame(newer)) { frame_buffer = std::move(newer); }
        }
        if (!running) { break; }

        // 构造帧上下文：帧时刻与当前时刻的相机旋转（无云台数据时保持单位阵）
        FrameContext ctx;
        ctx.frame = frame_buffer;
        const int64_t frame_timestamp_us = static_cast<int64_t>(frame_buffer->timestampUs());
        ctx.now_us = this->now().nanoseconds() / 1000;
        {
            std::lock_guard<std::mutex> lock(gimbal_mtx_);
            cv::Matx33d R;
            if (gimbal_history_.nearest(frame_timestamp_us, R))
                ctx.R_cam_at_frame = R;
            if (gimbal_history_.latest(R))
                ctx.R_cam_now = R;
        }

        const auto update_begin{update_timing_observer_ ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{}};
        TrackerOutput out = tracker_->update(ctx);
        if (update_timing_observer_) {
            const auto update_end{std::chrono::steady_clock::now()};
            update_timing_observer_(
                std::chrono::duration_cast<std::chrono::microseconds>(update_begin.time_since_epoch()).count(),
                std::chrono::duration_cast<std::chrono::microseconds>(update_end - update_begin).count(), out.consumed);
        }

        auto elapsed = this->now() - start_time;
        double cost_ms = elapsed.seconds() * 1000;

        if (out.consumed)
        {
            ++consumed_frames_;
            if (osd_presenter_) {
                osd::PresentationPacket packet{frame_buffer, osd_round_, ++osd_sequence_,
                    frame_buffer->timestampUs(), static_cast<std::int8_t>(out.state), {}};
                const auto &box{out.display_box};
                if (box.is_valid && box.width > 0 && box.height > 0) {
                    const double left{std::max(0.0, box.cx - box.width / 2.0)};
                    const double top{std::max(0.0, box.cy - box.height / 2.0)};
                    const double right{std::min(static_cast<double>(frame_buffer->width()), box.cx + box.width / 2.0)};
                    const double bottom{std::min(static_cast<double>(frame_buffer->height()), box.cy + box.height / 2.0)};
                    if (right > left && bottom > top) {
                        packet.boxes.push_back({left, top, right - left, bottom - top, {}});
                    }
                }
                const auto submit_start{osd::SteadyClock::now()};
                osd_presenter_->submit(std::move(packet));
                const auto elapsed{static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    osd::SteadyClock::now() - submit_start).count())};
                // 只测完整 submit 调用（含旧包释放）；不把此耗时当作屏幕显示延迟。
                osd_submit_ns_.fetch_add(elapsed);
                if (elapsed > osd_submit_max_ns_.load()) { osd_submit_max_ns_.store(elapsed); }
                osd_submit_calls_.fetch_add(1);
            }
            // 发布实时预测框（KF predict_only(now) 外推），空 frame_id 表示无效检测。
            // 时间戳用外推时刻（与外推框同 epoch，避免下游对已外推的测量二次外推）
            auto detection_msg = std::make_unique<vision_msgs::msg::Detection2D>();
            detection_msg->header.stamp = rclcpp::Time(ctx.now_us * 1000);
            detection_msg->header.frame_id = out.tracking ? "camera_frame" : "";
            detection_msg->bbox.center.position.x = out.realtime_box.cx;
            detection_msg->bbox.center.position.y = out.realtime_box.cy;
            detection_msg->bbox.size_x = out.realtime_box.width;
            detection_msg->bbox.size_y = out.realtime_box.height;
            record(*detection_msg, TOPIC_TARGET_BBOX, detection_msg->header.stamp);
            detection_pub_->publish(std::move(detection_msg));

            // 状态仅变化时发布
            int8_t tracker_state_int = static_cast<int8_t>(out.state);
            if (tracker_state_int != last_tracker_state)
            {
                auto it = state_text_map.find(tracker_state_int);
                status_publisher_->publish(mavros_msgs::msg::StatusText::WARNING, 1,
                    it != state_text_map.end() ? it->second : "Unknown", true);
                auto msg = std::make_unique<std_msgs::msg::Int8>();
                msg->data = tracker_state_int;
                tracker_state_pub_->publish(std::move(msg));
                last_tracker_state = tracker_state_int;
            }
        }

        if (enable_image_output_ && out.consumed)
        {
            const cv::Scalar color = cv::Scalar(
                (out.color & 0xFF), ((out.color >> 8) & 0xFF), ((out.color >> 16) & 0xFF));
            const int64_t frame_ts = static_cast<int64_t>(frame_buffer->timestampUs());

            bool frame_ready = false;
            {
                std::unique_ptr<FrameReadView> view = frame_buffer->mapRead();
                if (view)
                {
                    // 发布图像需要绘制检测框，因此无论原生格式如何都生成独立 BGR 副本。
                    // OpenCV 会按视图中的实际行跨度读取带填充的输入行。
                    if (view->pixelFormat() == FramePixelFormat::Gray8)
                    {
                        cv::cvtColor(view->mat(), frame, cv::COLOR_GRAY2BGR);
                        frame_ready = true;
                    }
                    else if (view->pixelFormat() == FramePixelFormat::Bgr8)
                    {
                        view->mat().copyTo(frame);
                        frame_ready = true;
                    }
                }
            }
            if (!frame_ready)
            {
                RCLCPP_ERROR(this->get_logger(), "Failed to map a supported frame for image publication");
            }
            else
            {
                if (out.display_box.is_valid)
                {
                    cv::Rect display_box = {
                        out.display_box.cx - out.display_box.width / 2,
                        out.display_box.cy - out.display_box.height / 2,
                        out.display_box.width,
                        out.display_box.height };
                    cv::rectangle(frame, display_box, color, 2, 1);
                }

                if (out.realtime_box.is_valid)
                {
                    // 红色实时预测框
                    cv::Point p1(out.realtime_box.cx - out.realtime_box.width / 2,
                                 out.realtime_box.cy - out.realtime_box.height / 2);
                    cv::Point p2(out.realtime_box.cx + out.realtime_box.width / 2,
                                 out.realtime_box.cy + out.realtime_box.height / 2);
                    cv::rectangle(frame, p1, p2, cv::Scalar(0, 0, 255), 2, 1);
                    cv::putText(frame, "Real-Time Pred", cv::Point(p1.x, std::max(20, p1.y - 10)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
                }

                cv::putText(frame, "Cost: " + std::to_string(int(cost_ms)) + " ms",
                            cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);
                cv::putText(frame, out.info, cv::Point(20, 80), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);

                cv_bridge::CvImage cv_img;
                cv_img.header.stamp = rclcpp::Time(frame_ts*1000);
                cv_img.header.frame_id = "camera_frame";
                cv_img.encoding = "bgr8";
                cv_img.image = frame;
                auto img_msg = std::make_unique<sensor_msgs::msg::Image>();
                cv_img.toImageMsg(*img_msg);
                record(*img_msg, TOPIC_TRACKER_IMAGE, rclcpp::Time(frame_ts*1000));
                image_pub_->publish(std::move(img_msg));
            }

            elapsed = this->now() - start_time;
            cost_ms = elapsed.seconds() * 1000;
        }

        avg_time_ms = (avg_time_ms * count + cost_ms) / (count + 1);
        ++count;

        // 计算剩余时间以保持帧率
        int remaining_time_us = static_cast<int>((interval_ms_ - cost_ms) * 1000);
        if (remaining_time_us <= 0) remaining_time_us = 1000; // 至少等待1ms

        std::unique_lock<std::mutex> wait_lock{wait_mutex_};
        wait_cv_.wait_for(wait_lock, std::chrono::microseconds(remaining_time_us), [this] { return !running; });
    }

    std::cout << "Average tracking time per frame: " << std::fixed << std::setprecision(2) << avg_time_ms << " ms" << std::endl;

    if (!running && !recording_failed_) { RCLCPP_INFO(get_logger(), "Round ended: stop requested"); }
}

void DroneTrackerNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    record(*msg, TOPIC_ODOM, rclcpp::Time(msg->header.stamp));
}

void DroneTrackerNode::imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg)
{
    // /fpv/gimbal 携带相机到世界的姿态（FLU 约定，由 apm_bridge 发布）。
    // 跟踪器 KF 只使用相对旋转 R_new^T * R_old，世界系约定无关紧要。
    // 无论是否 running 都持续积累，保证触发 ON 时历史已就绪。
    const auto& q = msg->orientation;
    if (q.w == 0.0 && q.x == 0.0 && q.y == 0.0 && q.z == 0.0)
        return; // 无姿态数据

    const int64_t ts_us = rclcpp::Time(msg->header.stamp).nanoseconds() / 1000;

    // 四元数 -> 旋转矩阵（与 Eigen toRotationMatrix 同式），归一化后按行列展开
    const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    const double w = q.w / norm, x = q.x / norm, y = q.y / norm, z = q.z / norm;
    const cv::Matx33d R(
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z), 2.0 * (x * z + w * y),
        2.0 * (x * y + w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - w * x),
        2.0 * (x * z - w * y), 2.0 * (y * z + w * x), 1.0 - 2.0 * (x * x + y * y));

    {
        std::lock_guard<std::mutex> lock(gimbal_mtx_);
        gimbal_history_.push(ts_us, R);
    }

    record(*msg, TOPIC_GIMBAL, rclcpp::Time(msg->header.stamp));
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
