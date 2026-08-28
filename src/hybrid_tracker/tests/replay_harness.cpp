// 离线回放工具：绕过 ROS，直接用录制视频驱动完整 HybridTracker
// （YOLO 级联检测 + NanoTrack/DCF + 仲裁 + KF），逐帧输出 bbox CSV，
// 用于测量边缘目标的 bbox 稳定性。
//
// 用法: replay_harness <models_dir> <recording.mjpeg> [max_frames] [csv_path]
#include <cstdio>
#include <cstdlib>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <opencv2/opencv.hpp>

#include "hybrid_tracker.h"

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <models_dir> <recording> [max_frames] [csv]\n", argv[0]);
        return 2;
    }
    const std::string model_dir = argv[1];
    const std::string recording = argv[2];
    const int max_frames = argc > 3 ? std::atoi(argv[3]) : 3000;
    const std::string csv_path = argc > 4 ? argv[4] : "/tmp/replay_bbox.csv";
    const std::string yolo_engine = argc > 5 ? argv[5] : "balloon_int8.engine";

    cv::VideoCapture cap(recording);
    if (!cap.isOpened())
    {
        std::printf("cannot open %s\n", recording.c_str());
        return 2;
    }
    const int W = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int H = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    std::printf("recording %dx%d @%.1ffps\n", W, H, cap.get(cv::CAP_PROP_FPS));

    // balloon_tracker_params.yaml 的等价配置（enable_dcf=false 为当前实机配置）
    TrackerSettings s;
    s.yolo_engine_path = model_dir + "/" + yolo_engine;
    s.nanotrack_backbone_path = model_dir + "/nanotrackv3_backbone_int8.engine";
    s.nanotrack_head_path = model_dir + "/nanotrack_head_int8.engine";
    s.target_class_id = 0;
    s.yolo_conf_threshold = 0.4f;
    s.yolo_nms_threshold = 0.45f;
    s.yolo_interval = 3;
    s.yolo_recall_relax_ratio = 0.6f;
    s.iou_reset_threshold = 0.4f;
    s.iou_calibrate_threshold = 0.8f;
    s.max_position_jump = 150.0f;
    s.max_size_jump_ratio = 2.0f;
    s.max_box_area_ratio = 0.8f;
    s.lost_max_count = 5;
    s.roi_scale_factor = 2.0f;
    s.frame_buffer_size = 30;
    s.jump_tolerance = 3;
    s.enable_dcf = false;
    // IMX265: 6.0mm / 3.45um, 1520 -> 960
    s.kf_fx = s.kf_fy = 6.0 * 1000.0 / 3.45 * (960.0 / 1520.0);
    s.kf_cx0 = W / 2.0;
    s.kf_cy0 = H / 2.0;

    auto tracker = HybridTracker::CreateInstance(s);
    if (!tracker->init())
    {
        std::printf("tracker init failed\n");
        return 2;
    }

    std::vector<std::array<double, 8>> rows; // frame, state, cx, cy, size, nano_cy, nano_h, score
    std::vector<std::string> infos;
    cv::Mat frame;
    for (int i = 0; i < max_frames && cap.read(frame); ++i)
    {
        cv::Mat gray;
        if (frame.channels() == 3)
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        else
            gray = frame;

        FrameContext ctx;
        ctx.frame = std::make_shared<VPIImageWrapper>(gray, 1000000ull + i * 20000ull);
        ctx.frame_ts_us = ctx.now_us = 1000000ull + i * 20000ull;

        TrackerOutput out = tracker->update(ctx);
        const cv::Rect2f nano = tracker->lastNanoBox();
        rows.push_back({static_cast<double>(i), static_cast<double>(static_cast<int8_t>(out.state)),
                        out.realtime_box.cx, out.realtime_box.cy,
                        std::max(out.realtime_box.width, out.realtime_box.height),
                        nano.y + nano.height / 2.0, nano.height,
                        static_cast<double>(tracker->lastNanoScore())});
        infos.push_back(out.consumed ? out.info : "(dropped)");
    }

    std::ofstream csv(csv_path);
    csv << "frame,state,cx,cy,size,nano_cy,nano_h,score,info\n";
    for (size_t k = 0; k < rows.size(); ++k)
        csv << rows[k][0] << "," << rows[k][1] << "," << rows[k][2] << "," << rows[k][3]
            << "," << rows[k][4] << "," << rows[k][5] << "," << rows[k][6] << ","
            << rows[k][7] << "," << infos[k] << "\n";
    csv.close();

    // ---- 稳定性分析：跟踪态(state==2)且贴边的窗口内中心抖动 ----
    double edge_jitter_sum = 0; int edge_jitter_n = 0;
    double in_jitter_sum = 0; int in_jitter_n = 0;
    const int WIN = 20;
    for (size_t i = WIN; i < rows.size(); ++i)
    {
        bool tracking = true;
        for (size_t k = i - WIN; k < i; ++k)
            if (rows[k][1] != 2.0) { tracking = false; break; }
        if (!tracking || rows[i - 1][4] <= 0)
            continue;
        // 中心均值与标准差（滑动窗口）
        double mx = 0, my = 0;
        for (int k = 1; k <= WIN; ++k) { mx += rows[i - k][2]; my += rows[i - k][3]; }
        mx /= WIN; my /= WIN;
        double vx = 0, vy = 0;
        for (int k = 1; k <= WIN; ++k)
        {
            vx += (rows[i - k][2] - mx) * (rows[i - k][2] - mx);
            vy += (rows[i - k][3] - my) * (rows[i - k][3] - my);
        }
        const double jitter = std::sqrt(vx / WIN) + std::sqrt(vy / WIN);
        const bool at_edge = (mx < rows[i - 1][4] || mx > W - rows[i - 1][4] ||
                              my < rows[i - 1][4] || my > H - rows[i - 1][4]);
        if (at_edge) { edge_jitter_sum += jitter; ++edge_jitter_n; }
        else { in_jitter_sum += jitter; ++in_jitter_n; }
    }
    std::printf("frames=%zu  edge_windows=%d  mean_edge_jitter=%.2fpx  mean_interior_jitter=%.2fpx\n",
        rows.size(), edge_jitter_n,
        edge_jitter_n ? edge_jitter_sum / edge_jitter_n : -1.0,
        in_jitter_n ? in_jitter_sum / in_jitter_n : -1.0);
    std::printf("csv written to %s\n", csv_path.c_str());
    return 0;
}
