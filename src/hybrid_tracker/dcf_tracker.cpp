#include "dcf_tracker.h"

#include <cmath>
#include <iostream>

#include <vpi/OpenCVInterop.hpp>

// ---------------------------------------------------------------------------
// VpiGrayLock
// ---------------------------------------------------------------------------

VpiGrayLock::VpiGrayLock(const std::shared_ptr<VPIImageWrapper>& frame)
{
    if (!frame || frame->get() == nullptr)
        return;
    img_ = frame->get();
    if (vpiImageLockData(img_, VPI_LOCK_READ, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &data_) != VPI_SUCCESS)
    {
        img_ = nullptr;
        return;
    }
    locked_ = true;
    vpiImageDataExportOpenCVMat(data_, &mat_);
    if (mat_.channels() == 3)
        cv::cvtColor(mat_, gray_, cv::COLOR_BGR2GRAY);
    else
        gray_ = mat_;   // 相机输出即单通道灰度，零拷贝
}

VpiGrayLock::~VpiGrayLock()
{
    if (locked_)
        vpiImageUnlock(img_);
}

// ---------------------------------------------------------------------------
// GrayscaleDCF
// ---------------------------------------------------------------------------

GrayscaleDCF::GrayscaleDCF(const Params& params)
    : params_(params)
{
    const int sz = params_.model_sz;

    // 对称汉宁窗（np.hanning 语义：w[k] = 0.5 - 0.5*cos(2*pi*k/(N-1))）。
    // 注意不能用 cv::createHanningWindow——那是周期汉宁，端点不为零。
    cv::Mat hann(1, sz, CV_64FC1);
    for (int k = 0; k < sz; ++k)
        hann.at<double>(k) = 0.5 - 0.5 * std::cos(2.0 * CV_PI * k / (sz - 1));
    window_ = hann.t() * hann;   // 外积 -> sz x sz

    // 中心高斯回归标签（峰值在中心）
    cv::Mat label(sz, sz, CV_64FC1);
    const double c = (sz - 1) / 2.0;
    const double sigma = sz * params_.sigma_factor;
    const double s2 = 2.0 * sigma * sigma;
    for (int r = 0; r < sz; ++r)
        for (int col = 0; col < sz; ++col)
            label.at<double>(r, col) = std::exp(-((col - c) * (col - c) + (r - c) * (r - c)) / s2);
    cv::dft(label, Y_, cv::DFT_COMPLEX_OUTPUT);

    reset();
}

void GrayscaleDCF::reset()
{
    trained_ = false;
    scale_ = 1.0;
    A_.release();
    B_.release();
    H_.release();
}

int GrayscaleDCF::nativeSize(const cv::Size2f& size) const
{
    const int native_sz = static_cast<int>(std::lround(
        (size.width + size.height) * 0.5 * (1.0 + 2.0 * params_.pad)));
    return std::max(16, native_sz);
}

cv::Mat GrayscaleDCF::getPatch(const cv::Mat& gray, const cv::Point2f& center, int native_sz) const
{
    // 以 center 为中心抽取正方形 patch（边界自动复制），再 resize 到模型分辨率：
    // 滤波器形状与目标尺度解耦
    cv::Mat patch;
    cv::getRectSubPix(gray, cv::Size(native_sz, native_sz), center, patch);
    if (patch.rows != params_.model_sz || patch.cols != params_.model_sz)
        cv::resize(patch, patch, cv::Size(params_.model_sz, params_.model_sz), 0, 0, cv::INTER_LINEAR);

    patch.convertTo(patch, CV_64F, 1.0 / 255.0);
    // 逐 patch 归一化，提升对光照变化的鲁棒性
    cv::Scalar mean, stddev;
    cv::meanStdDev(patch, mean, stddev);
    patch -= mean[0];
    if (stddev[0] > params_.eps)
        patch /= stddev[0];
    return patch.mul(window_);
}

void GrayscaleDCF::recomputeFilter()
{
    const int n = params_.model_sz * params_.model_sz;
    H_.create(params_.model_sz, params_.model_sz, CV_64FC2);
    const double* a = A_.ptr<double>();
    const double* b = B_.ptr<double>();
    double* h = H_.ptr<double>();
    for (int i = 0; i < n; ++i)
    {
        // B = X·conj(X) 的虚部为 0，只取实部
        const double denom = b[2 * i] + params_.lambda_reg;
        h[2 * i] = a[2 * i] / denom;
        h[2 * i + 1] = a[2 * i + 1] / denom;
    }
}

void GrayscaleDCF::init(const cv::Mat& gray, const cv::Point2f& center, const cv::Size2f& size)
{
    const int native_sz = nativeSize(size);
    scale_ = native_sz / static_cast<double>(params_.model_sz);

    cv::Mat X;
    cv::dft(getPatch(gray, center, native_sz), X, cv::DFT_COMPLEX_OUTPUT);
    cv::mulSpectrums(Y_, X, A_, 0, true);   // A = Y·conj(X)
    cv::mulSpectrums(X, X, B_, 0, true);    // B = X·conj(X)
    recomputeFilter();                      // H = A/(B+λ)
    trained_ = true;
}

bool GrayscaleDCF::update(const cv::Mat& gray, const cv::Point2f& search_center, const cv::Size2f& size,
                          cv::Point2f& new_center, float& psr)
{
    if (!trained_)
    {
        new_center = search_center;
        psr = 0.0f;
        return false;
    }

    const int native_sz = nativeSize(size);
    scale_ = native_sz / static_cast<double>(params_.model_sz);

    cv::Mat S, prod, R_full;
    cv::dft(getPatch(gray, search_center, native_sz), S, cv::DFT_COMPLEX_OUTPUT);
    cv::mulSpectrums(S, H_, prod, 0, true);                       // S·conj(H)
    // 乘积不满足 Hermitian 对称，逆变换不能用 DFT_REAL_OUTPUT；
    // DFT_SCALE 对应 np.fft.ifft2 的 1/N 归一化
    cv::dft(prod, R_full, cv::DFT_INVERSE | cv::DFT_SCALE);
    cv::Mat R;
    cv::extractChannel(R_full, R, 0);                             // 取实部

    int ri, ci;
    double rmax, cmax;
    peak(R, ri, ci, rmax, cmax, psr);

    const double center = (params_.model_sz - 1) / 2.0;
    const double disp_x = (cmax - center) * scale_;
    const double disp_y = (rmax - center) * scale_;
    new_center = cv::Point2d(search_center.x + disp_x, search_center.y + disp_y);

    // 固定模板：跟踪时不更新滤波器，避免背景干扰引入的峰值微跳。
    // 仅当 fixed_template=false 时，以极低学习率在线缓慢更新。
    if (!params_.fixed_template)
    {
        cv::Mat X;
        cv::dft(getPatch(gray, new_center, native_sz), X, cv::DFT_COMPLEX_OUTPUT);
        cv::Mat A_new, B_new;
        cv::mulSpectrums(Y_, X, A_new, 0, true);
        cv::mulSpectrums(X, X, B_new, 0, true);
        const double lr = params_.learning_rate;
        cv::addWeighted(A_new, lr, A_, 1.0 - lr, 0.0, A_);
        cv::addWeighted(B_new, lr, B_, 1.0 - lr, 0.0, B_);
        recomputeFilter();
    }

    return true;
}

void GrayscaleDCF::peak(const cv::Mat& R, int& ri, int& ci, double& rmax, double& cmax, float& psr) const
{
    const int sz = params_.model_sz;
    double peak_val = 0.0;
    cv::Point loc;
    cv::minMaxLoc(R, nullptr, &peak_val, nullptr, &loc);
    ri = loc.y;
    ci = loc.x;

    // 抛物线亚像素插值（边缘跳过）——抑制中心抖动的关键
    rmax = ri;
    cmax = ci;
    if (ri >= 1 && ri <= sz - 2 && ci >= 1 && ci <= sz - 2)
    {
        const double r_m = R.at<double>(ri - 1, ci), r_p = R.at<double>(ri + 1, ci);
        const double c_m = R.at<double>(ri, ci - 1), c_p = R.at<double>(ri, ci + 1);
        const double dr_den = r_m - 2.0 * peak_val + r_p;
        const double dc_den = c_m - 2.0 * peak_val + c_p;
        if (std::fabs(dr_den) > params_.eps)
            rmax = ri + 0.5 * (r_m - r_p) / dr_den;
        if (std::fabs(dc_den) > params_.eps)
            cmax = ci + 0.5 * (c_m - c_p) / dc_den;
    }

    // PSR = (max - mean) / std，峰附近 11x11 邻域（含旁瓣）排除后统计（总体标准差）
    const int r0 = std::max(0, ri - 5), r1 = std::min(sz, ri + 6);
    const int c0 = std::max(0, ci - 5), c1 = std::min(sz, ci + 6);
    double sum = 0.0, sumsq = 0.0;
    int count = 0;
    for (int r = 0; r < sz; ++r)
    {
        const bool in_row = (r >= r0 && r < r1);
        const double* row = R.ptr<double>(r);
        for (int col = 0; col < sz; ++col)
        {
            if (in_row && col >= c0 && col < c1)
                continue;
            sum += row[col];
            sumsq += row[col] * row[col];
            ++count;
        }
    }
    const double mean = count > 0 ? sum / count : 0.0;
    const double var = count > 0 ? std::max(0.0, sumsq / count - mean * mean) : 0.0;
    const double std_dev = std::sqrt(var);
    psr = std_dev > params_.eps ? static_cast<float>((peak_val - mean) / std_dev) : 0.0f;
}

// ---------------------------------------------------------------------------
// CenterKalmanFilter
// ---------------------------------------------------------------------------

CenterKalmanFilter::CenterKalmanFilter(const Params& params)
    : params_(params)
{
    reset();
}

void CenterKalmanFilter::reset()
{
    initialized_ = false;
    last_ts_us_ = 0;
    x_ = cv::Vec4d::zeros();
    P_ = cv::Matx44d::eye() * 1000.0;
}

void CenterKalmanFilter::initialize(const cv::Point2f& center, int64_t ts_us)
{
    x_ = cv::Vec4d(center.x, center.y, 0.0, 0.0);
    P_ = cv::Matx44d::eye();
    P_(2, 2) *= 1000.0;   // 对速度不确定
    P_(3, 3) *= 1000.0;
    last_ts_us_ = ts_us;
    initialized_ = true;
}

cv::Matx44d CenterKalmanFilter::transition(double dt) const
{
    cv::Matx44d F = cv::Matx44d::eye();
    F(0, 2) = dt;
    F(1, 3) = dt;
    return F;
}

cv::Matx44d CenterKalmanFilter::processNoise(double dt) const
{
    // 连续白噪声加速度模型（cx-vx / cy-vy 耦合子块，与主管线 KF 同族）
    dt = std::max(1e-6, dt);
    const double dt2 = dt * dt;
    const double dt3 = dt2 * dt;

    cv::Matx44d Q = cv::Matx44d::zeros();
    Q(0, 0) = params_.q_a * dt3 / 3.0;
    Q(0, 2) = Q(2, 0) = params_.q_a * dt2 / 2.0;
    Q(2, 2) = params_.q_a * dt;
    Q(1, 1) = params_.q_a * dt3 / 3.0;
    Q(1, 3) = Q(3, 1) = params_.q_a * dt2 / 2.0;
    Q(3, 3) = params_.q_a * dt;
    return Q;
}

cv::Point2f CenterKalmanFilter::predictOnly(int64_t ts_us) const
{
    if (!initialized_)
        return cv::Point2f();
    double dt = (ts_us - last_ts_us_) * 1e-6;
    if (dt < 0.0)
        dt = 0.0;
    return cv::Point2f(x_[0] + x_[2] * dt, x_[1] + x_[3] * dt);
}

cv::Point2f CenterKalmanFilter::update(const cv::Point2f& meas_center, int64_t ts_us)
{
    double dt = (ts_us - last_ts_us_) * 1e-6;
    if (dt < 0.0)
        dt = 0.0;
    const cv::Matx44d F = transition(dt);
    const cv::Vec4d x_pred = F * x_;
    const cv::Matx44d P_pred = F * P_ * F.t() + processNoise(dt);

    // H 选取 [cx, cy]；约瑟夫形式校正
    const double h_vals[8] = {1., 0., 0., 0.,
                              0., 1., 0., 0.};
    const cv::Matx<double, 2, 4> H(h_vals);
    cv::Matx22d R = cv::Matx22d::zeros();
    R(0, 0) = R(1, 1) = params_.r_pos;

    const cv::Vec2d y(meas_center.x - x_pred[0], meas_center.y - x_pred[1]);
    const cv::Matx22d S = H * P_pred * H.t() + R;
    const cv::Matx<double, 4, 2> K = P_pred * H.t() * S.inv();
    x_ = x_pred + K * y;
    const cv::Matx44d KH = K * H;
    const cv::Matx44d I_KH = cv::Matx44d::eye() - KH;
    P_ = I_KH * P_pred * I_KH.t() + K * R * K.t();
    last_ts_us_ = ts_us;
    return cv::Point2f(x_[0], x_[1]);
}

// ---------------------------------------------------------------------------
// NanoDcfTracker
// ---------------------------------------------------------------------------

NanoDcfTracker::NanoDcfTracker(const Params& params)
    : params_(params),
      nano_(std::make_unique<NanoTracker>(params.backbone_path, params.head_path)),
      dcf_(std::make_unique<GrayscaleDCF>(params.dcf)),
      center_kf_(params.center_kf)
{
}

bool NanoDcfTracker::init(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& bbox)
{
    nano_->init(frame, bbox);
    if (!nano_->isInitialized())
    {
        std::cerr << "NanoDcfTracker: nano init failed (crop error?)" << std::endl;
        return false;
    }

    const cv::Point2f center(bbox.x + bbox.width / 2.0f, bbox.y + bbox.height / 2.0f);
    const cv::Size2f size(bbox.width, bbox.height);
    const int64_t ts_us = static_cast<int64_t>(frame->get_timestamps());

    if (params_.enable_dcf)
    {
        VpiGrayLock lock(frame);
        if (!lock.valid())
        {
            std::cerr << "NanoDcfTracker: failed to lock frame for DCF init" << std::endl;
            return false;
        }
        dcf_->init(lock.gray(), center, size);
    }
    center_kf_.initialize(center, ts_us);
    prev_center_ = center;
    has_prev_center_ = true;
    return true;
}

bool NanoDcfTracker::update(const std::shared_ptr<VPIImageWrapper>& frame, cv::Rect2f& bbox)
{
    cv::Rect2f nano_box;
    last_score_ = nano_->update(frame, nano_box);
    if (last_score_ < 0.0f)
    {
        // 搜索窗裁剪失败：本帧按跟踪失败处理，保持 DCF/中心 KF 状态不变
        return false;
    }
    if (!nano_->isInitialized())
        return false;
    last_nano_box_ = nano_box;

    const float w = nano_box.width;
    const float h = nano_box.height;
    const cv::Point2f nano_center(nano_box.x + w / 2.0f, nano_box.y + h / 2.0f);
    const cv::Size2f size(w, h);

    cv::Point2f chosen;
    if (!params_.enable_dcf)
    {
        // DCF 关闭：由轻量 CV 卡尔曼滤波承担"稳心"角色，抑制 NanoTrack 中心的
        // 逐帧抖动；测量远离滤波器预测（目标真实大幅移动）时信任 NanoTrack
        // 并重锚定——与 DCF 路径共用同一发散门限
        const int64_t ts_us = static_cast<int64_t>(frame->get_timestamps());
        if (!center_kf_.initialized())
            center_kf_.initialize(nano_center, ts_us);

        const cv::Point2f pred = center_kf_.predictOnly(ts_us);
        const double div = std::hypot(pred.x - nano_center.x, pred.y - nano_center.y);
        const double max_div = params_.max_div_factor * ((w + h) / 2.0);
        if (div > max_div)
        {
            chosen = nano_center;
            center_kf_.initialize(nano_center, ts_us);
        }
        else
        {
            chosen = center_kf_.update(nano_center, ts_us);
        }
    }
    else
    {
        VpiGrayLock lock(frame);
        if (!lock.valid())
        {
            // 拿不到灰度图时退化为 NanoTrack 结果
            chosen = nano_center;
        }
        else
        {
            const cv::Point2f search_center = has_prev_center_ ? prev_center_ : nano_center;
            cv::Point2f dcf_center;
            float psr = 0.0f;
            if (!dcf_->update(lock.gray(), search_center, size, dcf_center, psr))
            {
                chosen = nano_center;
                dcf_->init(lock.gray(), nano_center, size);
            }
            else
            {
                const double div = std::hypot(dcf_center.x - nano_center.x, dcf_center.y - nano_center.y);
                const double max_div = params_.max_div_factor * ((w + h) / 2.0);
                if (psr < dcf_->psrThreshold() || div > max_div)
                {
                    // DCF 失锁：信任 NanoTrack，并以其中心重新锚定滤波器
                    chosen = nano_center;
                    dcf_->init(lock.gray(), nano_center, size);
                }
                else
                {
                    chosen = dcf_center;
                }
            }
        }
    }

    prev_center_ = chosen;
    has_prev_center_ = true;
    bbox = cv::Rect2f(chosen.x - w / 2.0f, chosen.y - h / 2.0f, w, h);
    return true;
}

void NanoDcfTracker::reset()
{
    nano_->reset();
    dcf_->reset();
    center_kf_.reset();
    has_prev_center_ = false;
    prev_center_ = cv::Point2f();
    last_score_ = 0.0f;
}

bool NanoDcfTracker::isInitialized() const
{
    return nano_->isInitialized();
}
