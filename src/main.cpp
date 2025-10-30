#include <iostream>
#include <string>
#include <chrono>
#include "hybrid_tracker.h"
#include "mv_camera.h"
#include "drone_ekf.h"
#include "opencv2/opencv.hpp"

using namespace std::chrono_literals;

// 定义默认配置
// const std::string DEFAULT_VIDEO_SOURCE = "V_DRONE_090.mp4";
const std::string DEFAULT_VIDEO_SOURCE = "0"; // 使用摄像头作为默认视频源
const std::string DEFAULT_YOLO_ENGINE = "drone_int8.engine";
const std::string DEFAULT_NANO_BACKBONE = "nanotrackv3_backbone_int8.engine";
const std::string DEFAULT_NANO_HEAD = "nanotrack_head_int8.engine";

int main(int argc, char** argv)
{
    // --- 1. 参数解析 ---
    cv::CommandLineParser parser(argc, argv,
        "{v video |" + DEFAULT_VIDEO_SOURCE + "| Path to video file or camera ID.}"
        "{o output | |Path to save the output video.}"
        // "{yolo_e  |" + DEFAULT_YOLO_ENGINE + "| Path to YOLOv8 TensorRT engine.}"
        // "{nano_b  |" + DEFAULT_NANO_BACKBONE + "| Path to NanoTracker backbone engine.}"
        // "{nano_h  |" + DEFAULT_NANO_HEAD + "| Path to NanoTracker head engine.}"
        // "{no_nano | | Disable NanoTracker and use CSRT.}"
    );

    std::string video_source = parser.get<std::string>("video");
    std::string output_path = parser.get<std::string>("output");

    printf("Output path: %s\n", output_path.c_str());

    // std::string video_source = DEFAULT_VIDEO_SOURCE;
    // std::string output_path = ""; // 不保存输出视频

    // --- 2. 设置 TrackerSettings ---
    TrackerSettings settings;
    // settings.yolo_engine_path = parser.get<std::string>("yolo_e");
    // settings.nanotrack_backbone_path = parser.get<std::string>("nano_b");
    // settings.nanotrack_head_path = parser.get<std::string>("nano_h");
    // settings.enable_nanotrack = !parser.has("no_nano");

    settings.target_class_id = 0; // 目标类别ID，可根据需要修改
    settings.yolo_engine_path = DEFAULT_YOLO_ENGINE;
    settings.nanotrack_backbone_path = DEFAULT_NANO_BACKBONE;
    settings.nanotrack_head_path = DEFAULT_NANO_HEAD;

    // --- 3. 初始化视频捕捉 ---
    bool is_camera = (video_source.length() == 1 && std::isdigit(video_source[0]));
    cv::VideoCapture cap;
    std::unique_ptr<MVCamera> mv_camera;
    bool success = false;

    int frame_width = 640;
    int frame_height = 640;
    double fps = 30;
    int delay_ms = 1;

    if (is_camera)
    {
        std::string device = "/dev/video";
        int width = 1920;
        int height = 1920;
        mv_camera = std::make_unique<MVCamera>(device.append(video_source), width, height, fps, true, frame_width, frame_height);
        success = mv_camera->run_async(false);
    }
    else
    {
        cap.open(video_source); // 打开视频文件
        success = cap.isOpened();
    }

    if (!success)
    {
        std::cerr << "Error: Cannot open video source '" << video_source << "'" << std::endl;
        return -1;
    }
    if (!is_camera)
    {
        frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        fps = cap.get(cv::CAP_PROP_FPS);
        delay_ms = (fps > 0) ? static_cast<int>(1000 / fps) : 1;
    }

    // --- 4. 初始化视频写入 (如果需要) ---
    cv::VideoWriter video_writer;
    if (!output_path.empty())
    {
        video_writer.open(output_path, cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, cv::Size(frame_width, frame_height));
        if (!video_writer.isOpened())
        {
            std::cerr << "Error: Could not open output video file: " << output_path << std::endl;
        }
    }

    // --- 5. 初始化 HybridTracker ---
    auto tracker = HybridTracker::CreateInstance(settings);
    if (!tracker->init())
    {
        std::cerr << "Error: Failed to initialize HybridTracker." << std::endl;
        return -1;
    }

    cv::Mat frame;
    std::cout << "Starting tracking loop... Press ESC to exit." << std::endl;

    int count = 0;
    double avg_time_ms = 0.0;
    // --- 6. 主循环 ---
    while (true)
    {
        auto start_time = std::chrono::high_resolution_clock::now();
        std::shared_ptr<VPIImageWrapper> vpi_buffer;
        if (is_camera)
        {
            if (mv_camera->getImageQueue().wait_dequeue_timed(vpi_buffer, 1s)) {
                start_time = std::chrono::high_resolution_clock::now();
                tracker->update(vpi_buffer);
            }
            else {
                std::cout << "No frame received from camera." << std::endl;
                break;
            }
        }
        else
        {
            if (!cap.read(frame))
            {
                std::cout << "Video ended." << std::endl;
                break;
            }

            cv::Mat mono;
            cv::cvtColor(frame, mono, cv::COLOR_BGR2GRAY);
            // cv::merge(std::vector<cv::Mat>{mono, mono, mono}, frame);

            // --- 7. 更新跟踪器 ---
            auto frame_wrapper = std::make_shared<VPIImageWrapper>(mono);
            tracker->update(frame_wrapper);
        }

        // --- 8. 获取结果并绘制 ---
        auto bbox = tracker->getCurrentBox();
        cv::Rect display_box = { 
            static_cast<int>(bbox.cx - bbox.width / 2), static_cast<int>(bbox.cy - bbox.height / 2), 
            static_cast<int>(bbox.width), static_cast<int>(bbox.height) };
        std::string status = tracker->getStatus();
        auto color_val = tracker->getColor();
        cv::Scalar color = cv::Scalar((color_val & 0xFF), ((color_val >> 8) & 0xFF), ((color_val >> 16) & 0xFF));

        if (is_camera)
        {
            cv::Mat mono;
            VPIImageData image_data;
            vpiImageLockData(vpi_buffer->get(), VPI_LOCK_READ, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &image_data);
            vpiImageDataExportOpenCVMat(image_data, &mono);
            vpiImageUnlock(vpi_buffer->get());
            cv::merge(std::vector<cv::Mat>{mono, mono, mono}, frame);
        }

        if (display_box.width > 0 && display_box.height > 0)
        {
            cv::rectangle(frame, display_box, color, 2, 1);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
        int cost_ms = static_cast<int>(elapsed.count());
        avg_time_ms = (avg_time_ms * count + elapsed.count()) / (count + 1);
        ++count;

        cv::putText(frame, "Cost: " + std::to_string(cost_ms) + " ms", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
        cv::putText(frame, status, cv::Point(20, 80), cv::FONT_HERSHEY_SIMPLEX, 1, color, 2);
        
        // --- 9. 写入和显示 ---
        if (video_writer.isOpened())
        {
            video_writer.write(frame);
        }

        cv::imshow("Hybrid Tracker C++", frame);

        // --- 10. 按键和延迟 ---
        // 计算剩余时间以保持帧率
        int remaining_time = delay_ms - cost_ms;
        if (remaining_time <= 0) remaining_time = 1; // 至少等待1ms

        if (cv::waitKey(remaining_time) == 27) // ESC
        {
            break;
        }
    }

    // --- 11. 清理 ---
    std::cout << "Exiting..." << std::endl;
    // tracker 将在析构时自动停止其线程
    if (is_camera)
    {
        mv_camera->stop();
    }
    else
    {
        cap.release();
    }
    if (video_writer.isOpened())
    {
        video_writer.release();
    }
    cv::destroyAllWindows();

    std::cout << "Average tracking time per frame: " << std::fixed << std::setprecision(2) << avg_time_ms << " ms" << std::endl;

    return 0;
}
