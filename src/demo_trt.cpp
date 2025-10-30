#include <iostream>
#include <string>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <opencv2/tracking.hpp> // For selectROI
#include "nano_tracker.h"

void draw_bbox_on_image(cv::Mat& img, const cv::Rect2f& bbox, const std::string& label) {
    cv::rectangle(img, bbox, cv::Scalar(0, 255, 0), 2);

    int baseline = 0;
    cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
    cv::Point text_org(bbox.x, bbox.y - 10);
    if (text_org.y < text_size.height) {
        text_org.y = bbox.y + text_size.height + 10;
    }
    
    cv::rectangle(img, text_org + cv::Point(0, baseline), text_org + cv::Point(text_size.width, -text_size.height), cv::Scalar(0, 255, 0), cv::FILLED);
    cv::putText(img, label, text_org, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 0), 2);
}

int main(int argc, char* argv[]) {
    // --- Configuration ---
    const std::string backbone_path = "nanotrackv3_backbone_int8.engine";
    const std::string head_path = "nanotrack_head_int8.engine";
    std::string video_path = "V_DRONE_090.mp4";

    if (argc > 1) {
        video_path = argv[1];
    } else {
        std::cout << "Usage: " << argv[0] << " <path_to_video>" << std::endl;
        std::cout << "Using default video path: " << video_path << std::endl;
    }

    // --- Initialization ---
    std::unique_ptr<NanoTracker> tracker;
    try {
        tracker = std::make_unique<NanoTracker>(backbone_path, head_path);
    } catch (const std::exception& e) {
        std::cerr << "Error initializing tracker: " << e.what() << std::endl;
        return -1;
    }
    
    cv::VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open video file " << video_path << std::endl;
        return -1;
    }
    
    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
        std::cerr << "Error: Could not read the first frame." << std::endl;
        return -1;
    }
    
    cv::Rect2f bbox = cv::selectROI("Select Object to Track", frame, false);
    cv::destroyWindow("Select Object to Track");

    if (bbox.empty()) {
        std::cout << "No ROI selected. Exiting." << std::endl;
        return 0;
    }

    auto frame_wrapper = std::make_shared<VPIImageWrapper>(frame);
    tracker->init(frame_wrapper, bbox);
    frame_wrapper.reset();
    
    double avg_time_ms = 0.0;
    int count = 0;
    // --- Main tracking loop ---
    while(true) {
        cap >> frame;
        if (frame.empty()) {
            break;
        }
        auto frame_wrapper = std::make_shared<VPIImageWrapper>(frame);

        auto start = std::chrono::high_resolution_clock::now();
        cv::Rect2f bbox;
        float score = tracker->update(frame_wrapper, bbox);
        auto end = std::chrono::high_resolution_clock::now();
        
        std::chrono::duration<double, std::milli> elapsed = end - start;
        std::cout << "Tracking time per frame: " << std::fixed << std::setprecision(2) << elapsed.count() << " ms" << std::endl;
        avg_time_ms = (avg_time_ms * count + elapsed.count()) / (count + 1);
        ++count;

        std::string score_text = "Score: " + std::to_string(score).substr(0, 4);
        draw_bbox_on_image(frame, bbox, score_text);

        cv::imshow("Tracking", frame);
        
        if (cv::waitKey(25) == 'q') {
            break;
        }
    }
    
    cap.release();
    cv::destroyAllWindows();

    std::cout << "Average tracking time per frame: " << std::fixed << std::setprecision(2) << avg_time_ms << " ms" << std::endl;
    
    return 0;
}
