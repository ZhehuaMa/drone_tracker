// NanoDcfTracker + YoloV8Detector 压力测试（真实 TRT 引擎，无 ROS）
// 目的：复现 "Failed to create VPI image view 2" + 堆损坏。
// 场景：目标框扫过四角/边缘（触发均值填充路径）、框尺寸 20~700px 往复
// （native_sz 超过图像、padded 图巨大）、DCF 灰度锁、并发 YOLO 工作线程。
#include <cstdio>
#include <cmath>
#include <thread>
#include <atomic>
#include <opencv2/opencv.hpp>

#include "dcf_tracker.h"
#include "yolo_detector.h"

int main(int argc, char** argv)
{
    const std::string model_dir = argc > 1 ? argv[1] : ".";
    const int W = 960, H = 960;
    const int N = argc > 2 ? std::atoi(argv[2]) : 1500;

    NanoDcfTracker::Params tp;
    tp.backbone_path = model_dir + "/nanotrackv3_backbone_int8.engine";
    tp.head_path = model_dir + "/nanotrack_head_int8.engine";
    tp.enable_dcf = true;
    tp.max_div_factor = 0.5f;
    NanoDcfTracker tracker(tp);

    YoloV8Detector yolo(model_dir + "/drone_int8.engine", 0.4f, 0.45f, 0, 1.0f, 0.8f);
    if (!yolo.init())
        return 2;

    uint64_t frame_id = 0;
    cv::RNG rng(12345);
    for (int i = 0; i < N; ++i)
    {
        // 目标位置：Lissajous 扫全图（含四角），尺寸 20~700px 三角波往复
        const double t = i * 0.02;
        const float cx = 480 + 430 * std::sin(t * 1.3);
        const float cy = 480 + 430 * std::sin(t * 2.1 + 1.0);
        const float sz = 360 + 340 * std::sin(t * 0.4);

        cv::Mat mat(H, W, CV_8UC1, cv::Scalar(60));
        rng.fill(mat, cv::RNG::UNIFORM, 40, 80);
        cv::rectangle(mat, cv::Rect(cx - sz / 2, cy - sz / 2, sz, sz), cv::Scalar(230), -1);
        cv::circle(mat, cv::Point(cvRound(cx), cvRound(cy)), cvRound(sz / 3), cv::Scalar(140), -1);

        auto frame = std::make_shared<VPIImageWrapper>(mat, 1000000ull + i * 20000ull);
        ++frame_id;

        // 模拟主管线：先 tracker 更新（DCF 主机灰度锁），后 YOLO 请求（工作线程）
        cv::Rect2f bbox(cx - sz / 2, cy - sz / 2, sz, sz);
        if (i % 37 == 0 || i < 3)
        {
            tracker.init(frame, bbox);   // 模拟 YOLO 触发的重初始化/补帧
        }
        else
        {
            cv::Rect2f out;
            tracker.update(frame, out);
        }

        if (i % 4 == 0)
        {
            yolo.request(frame, frame_id, cv::Rect());
            // 回归防护：模拟补帧回放覆盖"刚提交给 YOLO 工作线程的当前帧"——
            // 主管线的 catchup 会遍历缓存帧（含当前帧）并做 DCF 主机灰度锁。
            // 工作线程必须已在私有副本上工作，与原帧的主机锁并发也必须安全。
            {
                VpiGrayLock lock(frame);
                if (lock.valid()) {
                    cv::Mat patch;
                    cv::getRectSubPix(lock.gray(), cv::Size(96, 96), cv::Point2f(cx, cy), patch);
                }
            }
        }
        YoloResult res;
        while (yolo.get_result(res)) {}

        if (i % 200 == 0)
            std::printf("iter %d/%d box=(%.0f,%.0f,%.0f)\n", i, N, cx, cy, sz);
    }
    std::printf("stress finished without crash\n");
    return 0;
}
