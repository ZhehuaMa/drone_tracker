// 混合跟踪器单元测试（无 gtest 依赖，普通 main）
// 构建: colcon build --packages-select drone_tracker \
//        --cmake-args -DCMAKE_BUILD_TYPE=Release -DDRONE_TRACKER_BUILD_TESTS=ON
// 运行: ./build/drone_tracker/src/hybrid_tracker/hybrid_tracker_tests
#include <cstdio>
#include <cmath>
#include <vector>
#include <opencv2/opencv.hpp>

#include "dcf_tracker.h"
#include "rotation_kf.h"
#include "yolo_detector.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    ++checks; \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else { std::printf("ok:   %s\n", msg); } \
} while (0)

// 在 center 处画一个高斯亮斑（模拟被跟踪目标）
static cv::Mat makeBlob(int w, int h, const cv::Point2f& center, float radius)
{
    cv::Mat img(h, w, CV_8UC1, cv::Scalar(30));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float d2 = (x - center.x) * (x - center.x) + (y - center.y) * (y - center.y);
            const double v = 30.0 + 200.0 * std::exp(-d2 / (2.0 * radius * radius));
            img.at<uchar>(y, x) = static_cast<uchar>(std::min(255.0, v));
        }
    return img;
}

// ---------------------------------------------------------------------------
// GrayscaleDCF：合成亚像素平移 -> 定位误差 < 1px，PSR > 5
// ---------------------------------------------------------------------------
static void testDCF()
{
    GrayscaleDCF::Params p;
    GrayscaleDCF dcf(p);
    const cv::Point2f c0(100, 100);
    const cv::Size2f size(20, 20);
    dcf.init(makeBlob(200, 200, c0, 6.0f), c0, size);

    const cv::Point2f offsets[] = {
        {0, 0}, {2.5f, 0}, {-3.0f, 1.5f}, {0.5f, 0.5f}, {5.0f, -4.0f}, {0.0f, 6.0f}};
    double max_axis_err = 0;
    float min_psr = 1e9f;
    bool all_ok = true;
    for (const auto& off : offsets)
    {
        cv::Point2f center;
        float psr = 0.0f;
        const bool ok = dcf.update(makeBlob(200, 200, c0 + off, 6.0f), c0, size, center, psr);
        if (!ok)
        {
            all_ok = false;
            continue;
        }
        max_axis_err = std::max(max_axis_err, static_cast<double>(std::max(
            std::fabs(center.x - (c0.x + off.x)), std::fabs(center.y - (c0.y + off.y)))));
        min_psr = std::min(min_psr, psr);
        std::printf("      off=(%.1f,%.1f) err=(%.2f,%.2f) psr=%.1f\n",
            off.x, off.y, center.x - (c0.x + off.x), center.y - (c0.y + off.y), psr);
    }
    CHECK(all_ok, "DCF update trained after init");
    CHECK(max_axis_err < 1.0, "DCF localizes sub-pixel shifted blob < 1px per axis");
    CHECK(min_psr > 5.0f, "DCF PSR > 5 on clean blob");

    // 大位移（超出 max_div = 0.5*mean(size) = 10px 的搜索域之外）时响应峰
    // 应追踪到位移位置或给出低 PSR（重锚定由 NanoDcfTracker 负责，这里只验证
    // 滤波器本身仍产生合法输出）
    cv::Point2f center;
    float psr = 0.0f;
    const bool ok = dcf.update(makeBlob(200, 200, c0 + cv::Point2f(15, 12), 6.0f), c0, size, center, psr);
    CHECK(ok && center.x > 104 && center.x < 122 && psr >= 0.0f, "DCF large-displacement output sane");
}

// ---------------------------------------------------------------------------
// RotationKalmanFilter：匀速 / 旋转补偿约定 / OOSM 顺序无关性
// ---------------------------------------------------------------------------
static cv::Matx33d rotZ(double a)
{
    const double c = std::cos(a), s = std::sin(a);
    return cv::Matx33d(c, -s, 0, s, c, 0, 0, 0, 1);
}

static void testRotationKF()
{
    RotationKalmanFilter::Params p;
    p.fx = p.fy = 200.0;
    p.cx0 = p.cy0 = 100.0;

    // (a) 匀速 + 单位旋转
    {
        RotationKalmanFilter kf(p);
        const double dt = 1.0 / 30.0;
        const double v = 30.0; // px/s
        for (int i = 0; i <= 60; ++i)
        {
            const cv::Vec4d z(100 + v * i * dt, 100, 20, 20);
            if (i == 0)
                kf.initialize(z, static_cast<int64_t>(i * 1e6), cv::Matx33d::eye());
            else
                kf.update(z, static_cast<int64_t>(i * 1e6), cv::Matx33d::eye());
        }
        cv::Vec4d pred;
        const bool ok = kf.predictOnly(static_cast<int64_t>(61 * 1e6), cv::Matx33d::eye(), pred);
        const double err = std::fabs(pred[0] - (100 + v * 61.0 * dt));
        CHECK(ok && err < 2.0, "KF constant-velocity 1-step prediction < 2px");
    }

    // (b) 旋转补偿约定测试：静止目标（世界系固定射线），相机绕 FLU z 轴旋转。
    //     约定正确 => 多步预测误差接近 0；约定错误（转错方向）=> 比不补偿更差。
    {
        auto ray_to_pixel = [&p](const cv::Vec3d& r) {
            return cv::Vec2d(p.cx0 - p.fx * r[1] / r[0], p.cy0 - p.fy * r[2] / r[0]);
        };
        const cv::Vec3d r_c0(1.0, -0.3, -0.2); // 初始像素约 (160, 140)
        const double omega = 1.5;              // rad/s
        const double dt = 1.0 / 30.0;
        const int N = 60;
        const int H = 10;                      // 预测视野：10 步 (~0.33s)

        auto run = [&](bool compensate, double& mean_err) {
            RotationKalmanFilter kf(p);
            double err = 0;
            int n = 0;
            for (int i = 0; i <= N; ++i)
            {
                const double th = omega * i * dt;
                const cv::Matx33d R = rotZ(th);
                const cv::Vec3d r_c = R.t() * r_c0;
                const cv::Vec2d px = ray_to_pixel(r_c);
                const cv::Vec4d z(px[0], px[1], 20, 20);
                const cv::Matx33d R_in = compensate ? R : cv::Matx33d::eye();
                if (i == 0)
                    kf.initialize(z, static_cast<int64_t>(i * 1e6), R_in);
                else
                    kf.update(z, static_cast<int64_t>(i * 1e6), R_in);

                if (i > N / 2 && i + H <= N)
                {
                    const double th_h = omega * (i + H) * dt;
                    const cv::Vec3d r_h = rotZ(th_h).t() * r_c0;
                    const cv::Vec2d px_h = ray_to_pixel(r_h);
                    const cv::Matx33d R_h = compensate ? rotZ(th_h) : cv::Matx33d::eye();
                    cv::Vec4d pr;
                    if (kf.predictOnly(static_cast<int64_t>((i + H) * 1e6), R_h, pr))
                    {
                        err += std::hypot(pr[0] - px_h[0], pr[1] - px_h[1]);
                        ++n;
                    }
                }
            }
            mean_err = n > 0 ? err / n : 1e9;
        };

        double comp_err = 0, uncomp_err = 0;
        run(true, comp_err);
        run(false, uncomp_err);
        std::printf("      rotation test: compensated=%.3fpx uncompensated=%.3fpx\n", comp_err, uncomp_err);
        CHECK(comp_err < 1.5, "rotation-compensated 10-step prediction < 1.5px");
        CHECK(comp_err < uncomp_err / 3.0, "compensated beats uncompensated (FLU/sign convention correct)");
    }

    // (c) OOSM 回溯语义（与 kalman_filter.py 一致）：乱序测量在基准处校正后，
    //     之后的历史条目被【纯重传播】替换（不重新应用它们的测量）。
    //     因此：先 t2 后补 t1 的最终状态 == 只用 {t0, t1} 滤波再外推到 t2。
    {
        RotationKalmanFilter b(p), ref(p);
        const double dt = 1.0; // s
        auto z_at = [](double t) { return cv::Vec4d(100 + 20 * t, 100 + 5 * t, 20, 20); };
        const int64_t t0 = 0, t1 = 1000000, t2 = 2000000;

        b.initialize(z_at(0), t0, cv::Matx33d::eye());
        b.update(z_at(2 * dt), t2, cv::Matx33d::eye()); // 新测量先到
        b.update(z_at(dt), t1, cv::Matx33d::eye());     // 旧测量回溯校正

        ref.initialize(z_at(0), t0, cv::Matx33d::eye());
        ref.update(z_at(dt), t1, cv::Matx33d::eye());

        cv::Vec4d xb, xr;
        b.predictOnly(t2, cv::Matx33d::eye(), xb);
        ref.predictOnly(t2, cv::Matx33d::eye(), xr);
        const double diff = std::fabs(xb[0] - xr[0]) + std::fabs(xb[1] - xr[1]) +
                            std::fabs(xb[2] - xr[2]) + std::fabs(xb[3] - xr[3]);
        std::printf("      OOSM re-propagation diff vs {t0,t1}+extrapolate: %.3g\n", diff);
        CHECK(diff < 1e-6, "retroactive update corrects the past and re-propagates (Python parity)");
    }
}

// ---------------------------------------------------------------------------
// 自适应重叠切片
// ---------------------------------------------------------------------------
static void testTiles()
{
    auto tiles = YoloV8Detector::createOverlappingTiles(960, 960, 0.2f);
    CHECK(tiles.size() == 4, "960x960 -> 4 tiles (2x2)");
    bool covers_tl = false, covers_br = false, in_bounds = true;
    for (const auto& t : tiles)
    {
        if (t.contains(cv::Point(0, 0))) covers_tl = true;
        if (t.contains(cv::Point(959, 959))) covers_br = true;
        if (t.x < 0 || t.y < 0 || t.x + t.width > 960 || t.y + t.height > 960) in_bounds = false;
    }
    CHECK(covers_tl && covers_br && in_bounds, "960x960 tiles cover corners and stay in bounds");
    CHECK(std::abs(tiles[0].width - 534) <= 1, "first tile width ~534 (Python parity)");

    tiles = YoloV8Detector::createOverlappingTiles(1920, 1200, 0.2f);
    CHECK(tiles.size() == 6, "1920x1200 -> 6 tiles (2x3)");
    int rows = 0, cols = 0;
    std::vector<int> ys, xs;
    for (const auto& t : tiles)
    {
        if (std::find(ys.begin(), ys.end(), t.y) == ys.end()) ys.push_back(t.y);
        if (std::find(xs.begin(), xs.end(), t.x) == xs.end()) xs.push_back(t.x);
    }
    CHECK(ys.size() == 2 && xs.size() == 3, "1920x1200 tiles form a 2x3 grid");
}

int main()
{
    std::printf("== GrayscaleDCF ==\n");
    testDCF();
    std::printf("== RotationKalmanFilter ==\n");
    testRotationKF();
    std::printf("== createOverlappingTiles ==\n");
    testTiles();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
