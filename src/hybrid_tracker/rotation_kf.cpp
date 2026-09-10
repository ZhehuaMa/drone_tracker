#include "rotation_kf.h"

RotationKalmanFilter::RotationKalmanFilter(const Params& params)
    : params_(params)
{
    R_ = cv::Matx44d::zeros();
    R_(0, 0) = params_.r_pos;
    R_(1, 1) = params_.r_pos;
    R_(2, 2) = params_.r_size;
    R_(3, 3) = params_.r_size;
    reset();
}

void RotationKalmanFilter::reset()
{
    initialized_ = false;
    history_.clear();
    x_ = cv::Vec6d{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    P_ = cv::Matx66d::eye() * 1000.0;
}

cv::Vec6d RotationKalmanFilter::rotatePredict(const cv::Vec6d& x, const cv::Matx33d& R_rel, double dt) const
{
    const double cx = x[0], cy = x[1], w = x[2], h = x[3], vx = x[4], vy = x[5];

    // 像素 -> 相机系方向向量（与 Python 端 _pixel_to_ray 一致，fx/fy 分轴）
    const double u = cx - params_.cx0;
    const double v = cy - params_.cy0;
    const cv::Vec3d p_old(1.0, -u / params_.fx, -v / params_.fy);
    const cv::Vec3d v_old(0.0, -vx / params_.fx, -vy / params_.fy);

    const cv::Vec3d p_new = R_rel * (p_old + v_old * dt);
    const cv::Vec3d v_new = R_rel * v_old;

    const double cx_pred = params_.cx0 - params_.fx * p_new[1] / p_new[0];
    const double cy_pred = params_.cy0 - params_.fy * p_new[2] / p_new[0];
    const double vx_pred = -params_.fx * v_new[1];
    const double vy_pred = -params_.fy * v_new[2];

    return cv::Vec6d(cx_pred, cy_pred, w, h, vx_pred, vy_pred);
}

cv::Matx66d RotationKalmanFilter::transitionMatrix(double dt) const
{
    cv::Matx66d F = cv::Matx66d::eye();
    F(0, 4) = dt;
    F(1, 5) = dt;
    return F;
}

cv::Matx66d RotationKalmanFilter::processNoise(double dt) const
{
    dt = std::max(1e-6, dt);
    const double dt2 = dt * dt;
    const double dt3 = dt2 * dt;

    cv::Matx66d Q = cv::Matx66d::zeros();
    // cx-vx 耦合子块（连续白噪声加速度模型）
    Q(0, 0) = params_.q_a * dt3 / 3.0;
    Q(0, 4) = Q(4, 0) = params_.q_a * dt2 / 2.0;
    Q(4, 4) = params_.q_a * dt;
    // cy-vy 耦合子块
    Q(1, 1) = params_.q_a * dt3 / 3.0;
    Q(1, 5) = Q(5, 1) = params_.q_a * dt2 / 2.0;
    Q(5, 5) = params_.q_a * dt;
    // w, h 尺寸噪声
    Q(2, 2) = params_.q_size * dt;
    Q(3, 3) = params_.q_size * dt;
    return Q;
}

void RotationKalmanFilter::initialize(const cv::Vec4d& cxcywh, int64_t ts_us, const cv::Matx33d& R_cam)
{
    x_ = cv::Vec6d(cxcywh[0], cxcywh[1], cxcywh[2], cxcywh[3], 0.0, 0.0);
    P_ = cv::Matx66d::eye();
    P_(4, 4) *= 1000.0;   // 对速度不确定
    P_(5, 5) *= 1000.0;

    initialized_ = true;
    history_.clear();
    history_.push_back({ts_us, x_, P_, R_cam});
}

bool RotationKalmanFilter::predictOnly(int64_t ts_us, const cv::Matx33d& R_cam, cv::Vec4d& out_box) const
{
    if (!initialized_ || history_.empty())
        return false;

    const Entry& last = history_.back();
    double dt = dtBetween(last.ts_us, ts_us);
    if (dt < 0.0)
        dt = 0.0;

    const cv::Matx33d R_rel = R_cam.t() * last.R;
    const cv::Vec6d predicted_x = rotatePredict(last.x, R_rel, dt);

    out_box = boxFromState(predicted_x);
    return true;
}

cv::Vec4d RotationKalmanFilter::update(const cv::Vec4d& cxcywh, int64_t ts_us, const cv::Matx33d& R_cam)
{
    if (!initialized_)
    {
        initialize(cxcywh, ts_us, R_cam);
        return boxFromState(x_);
    }

    // 从后向前找最后一个 ts <= 测量时刻的历史条目作为校正基准
    int base_idx = -1;
    for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i)
    {
        if (history_[i].ts_us <= ts_us)
        {
            base_idx = i;
            break;
        }
    }
    if (base_idx == -1)
    {
        // 测量比全部历史都早：退化为重新初始化
        initialize(cxcywh, ts_us, R_cam);
        return boxFromState(x_);
    }

    const Entry& base = history_[base_idx];
    const double dt = dtBetween(base.ts_us, ts_us);
    const cv::Matx66d F = transitionMatrix(dt);
    const cv::Matx33d R_rel = R_cam.t() * base.R;
    const cv::Vec6d predicted_x = rotatePredict(base.x, R_rel, dt);
    const cv::Matx66d predicted_P = F * base.P * F.t() + processNoise(dt);

    // H 选取前 4 个状态；约瑟夫形式校正
    const double h_vals[24] = {1., 0., 0., 0., 0., 0.,
                               0., 1., 0., 0., 0., 0.,
                               0., 0., 1., 0., 0., 0.,
                               0., 0., 0., 1., 0., 0.};
    const cv::Matx<double, 4, 6> H(h_vals);
    const cv::Vec4d y = cxcywh - H * predicted_x;
    const cv::Matx44d S = H * predicted_P * H.t() + R_;
    const cv::Matx<double, 6, 4> K = predicted_P * H.t() * S.inv();
    const cv::Vec6d corrected_x = predicted_x + K * y;
    const cv::Matx66d KH = K * H;
    const cv::Matx66d I_KH = cv::Matx66d::eye() - KH;
    const cv::Matx66d corrected_P = I_KH * predicted_P * I_KH.t() + K * R_ * K.t();

    // 截断历史到基准条目，追加校正结果，再重传播之后的所有条目
    std::deque<Entry> new_history(history_.begin(), history_.begin() + base_idx + 1);
    new_history.push_back({ts_us, corrected_x, corrected_P, R_cam});

    for (size_t i = base_idx + 1; i < history_.size(); ++i)
    {
        const Entry& prev = new_history.back();
        const Entry& cur = history_[i];
        const double rdt = dtBetween(prev.ts_us, cur.ts_us);
        const cv::Matx66d rF = transitionMatrix(rdt);
        const cv::Matx33d r_rel = cur.R.t() * prev.R;
        const cv::Vec6d r_x = rotatePredict(prev.x, r_rel, rdt);
        const cv::Matx66d r_P = rF * prev.P * rF.t() + processNoise(rdt);
        new_history.push_back({cur.ts_us, r_x, r_P, cur.R});
        if (static_cast<int>(new_history.size()) > params_.history_len)
            new_history.pop_front();
    }

    history_ = std::move(new_history);
    while (static_cast<int>(history_.size()) > params_.history_len)
        history_.pop_front();

    x_ = history_.back().x;
    P_ = history_.back().P;
    return boxFromState(x_);
}
