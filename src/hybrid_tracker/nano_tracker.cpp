#include "nano_tracker.h"
#include <numeric>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

NanoTracker::NanoTracker(std::unique_ptr<tracker_detail::NanoModelCalls> model,
                         std::unique_ptr<NanoTrackPreprocessor> preprocessor)
    : model_{std::move(model)}, preprocessor_{std::move(preprocessor)} {
    if (!model_ || !preprocessor_) { throw std::invalid_argument("NanoTracker dependencies are required"); }
    // Create Hanning window
    cv::Mat hanning;
    cv::createHanningWindow(hanning, cv::Size(score_size, score_size), CV_32F);
    window = hanning.reshape(1, 1); // Flatten
    
    generate_points();
}

NanoTracker::~NanoTracker() = default;

void NanoTracker::reset() {
    preprocessor_->reset();
    m_initialized = false;
    center_pos = cv::Point2f(0, 0);
    size = cv::Point2f(0, 0);
}

void NanoTracker::generate_points() {
    const float ori = -static_cast<float>(score_size / 2) * stride;
    points.create(score_size * score_size, 2, CV_32F);

    for (int y = 0; y < score_size; ++y) {
        for (int x = 0; x < score_size; ++x) {
            points.at<float>(y * score_size + x, 0) = ori + static_cast<float>(x) * stride;
            points.at<float>(y * score_size + x, 1) = ori + static_cast<float>(y) * stride;
        }
    }
}

static float sz(float w, float h) {
    float pad = (w + h) * 0.5f;
    return std::sqrt((w + pad) * (h + pad));
}

static cv::Mat change(const cv::Mat& r) {
    cv::Mat max_val, inv_r;
    cv::divide(1.0, r, inv_r);
    cv::max(r, inv_r, max_val);
    return max_val;
};

void NanoTracker::init(const FrameRef& frame, const cv::Rect2f& bbox) {
    m_initialized = false;

    preprocessor_->reset();
    if (!frame) { return; }

    const int w = frame->width();
    const int h = frame->height();
    if (w <= 0 || h <= 0)
        return;

    // 与 Python 端一致：不裁剪输入框本身。目标处于 FOV 边缘时检测框会部分
    // 越界——按图像裁剪会产生狭长的退化模板（边缘跟踪抖动），完全越界时甚至
    // 得到空框（初始化死循环）。搜索窗越界由预处理器的均值填充
    // 处理；这里只把中心约束到图像内、尺寸约束到图像量级（下限 min_wh），
    // 保证裁剪窗与图像有重叠且不会产生异常巨大的 padded 图。
    const float cx = bbox.x + (bbox.width - 1) / 2.0f;
    const float cy = bbox.y + (bbox.height - 1) / 2.0f;
    center_pos = cv::Point2f(
        std::max(0.0f, std::min(cx, static_cast<float>(w))),
        std::max(0.0f, std::min(cy, static_cast<float>(h))));
    size = cv::Point2f(
        std::max(static_cast<float>(min_wh), std::min(bbox.width, static_cast<float>(w))),
        std::max(static_cast<float>(min_wh), std::min(bbox.height, static_cast<float>(h))));

    float s_z = sz(size.x, size.y);

    const ModelInputContract contract{{1, 3, template_size, template_size}};
    auto prepared{preprocessor_->prepareTemplate(frame, center_pos, static_cast<int>(round(s_z)), contract)};
    if (!prepared || !prepared.value) {
        std::cerr << "NanoTrack template failed: " << prepared.message << std::endl;
        return;
    }
    try {
        model_->setTemplate(*prepared.value);
        m_initialized = true;
    } catch (const std::exception &error) {
        preprocessor_->reset();
        std::cerr << "NanoTrack template inference failed: " << error.what() << std::endl;
    }
}

float NanoTracker::update(const FrameRef& frame, cv::Rect2f& output_bbox) {
    if (!m_initialized) {
        return 0.0f; // 未初始化：与 Python 端 (False, None) 对齐
    }
    if (!frame) { return -1.0f; }
    float s_z = sz(size.x, size.y);
    float scale_z = static_cast<float>(template_size) / s_z;
    float s_x = s_z * (static_cast<float>(instance_size) / template_size);
    
    const cv::Size img_size{frame->width(), frame->height()};
    const ModelInputContract contract{{1, 3, instance_size, instance_size}};
    auto prepared{preprocessor_->prepareSearch(frame, center_pos, static_cast<int>(round(s_x)), contract)};
    if (!prepared || !prepared.value) {
        std::cerr << "NanoTrack search failed: " << prepared.message << std::endl;
        return -1.0f;
    }
    tracker_detail::ModelOutputs outputs;
    try {
        outputs = model_->search(*prepared.value);
    } catch (const std::exception &error) {
        std::cerr << "NanoTrack search inference failed: " << error.what() << std::endl;
        return -1.0f;
    }
    if (outputs.tensors.size() < 2 || outputs.tensors[0].dims != 4 || outputs.tensors[1].dims != 4) {
        return -1.0f;
    }
    cv::Mat cls_logits = outputs.tensors[0].size[1] == 2 ? outputs.tensors[0] : outputs.tensors[1];
    cv::Mat loc_reg = outputs.tensors[0].size[1] == 2 ? outputs.tensors[1] : outputs.tensors[0];

    // --- The rest of the tracking logic is unchanged ---

    // Reshape cls_logits: (1, 2, 15, 15) -> (2, 225) -> (225, 2)
    cls_logits = cls_logits.reshape(1, 2).t();

    cv::Mat exp_scores;
    cv::exp(cls_logits, exp_scores);
    
    cv::Mat sum_exp_scores;
    cv::reduce(exp_scores, sum_exp_scores, 1, cv::REDUCE_SUM);

    cv::Mat scores = exp_scores.col(1) / sum_exp_scores;

    cv::Mat pred_bbox = convert_bbox(loc_reg, points);

    cv::Mat pred_sz;
    cv::vconcat(pred_bbox.row(2), pred_bbox.row(3), pred_sz);
    cv::Mat s_c_sz;
    s_c_sz.create(1, pred_sz.cols, CV_32F);
    for(int i = 0; i < pred_sz.cols; ++i) {
        s_c_sz.at<float>(0, i) = sz(pred_sz.at<float>(0, i), pred_sz.at<float>(1, i));
    }
    
    float original_sz_scaled = sz(size.x * scale_z, size.y * scale_z);
    cv::Mat s_c = change(s_c_sz / original_sz_scaled);

    cv::Mat pred_ratio = pred_bbox.row(2) / pred_bbox.row(3);
    float original_ratio = size.x / size.y;
    cv::Mat r_c = change(pred_ratio / original_ratio);
    cv::Mat penalty;
    cv::exp(- (r_c.mul(s_c) - 1) * track_penalty_k, penalty);

    cv::Mat pscore = penalty.mul(scores.t());
    pscore = pscore * (1.0f - window_influence) + window * window_influence;
    
    cv::Point best_idx_pt;
    double max_val;
    cv::minMaxLoc(pscore, nullptr, &max_val, nullptr, &best_idx_pt);
    int best_idx = best_idx_pt.x;

    cv::Mat bbox = pred_bbox.col(best_idx) / scale_z;
    
    float lr = penalty.at<float>(best_idx) * scores.at<float>(best_idx) * track_lr;
    
    float cx = bbox.at<float>(0) + center_pos.x;
    float cy = bbox.at<float>(1) + center_pos.y;
    float width = size.x * (1.0f - lr) + bbox.at<float>(2) * lr;
    float height = size.y * (1.0f - lr) + bbox.at<float>(3) * lr;

    cv::Rect2f clipped_box = bbox_clip({cx, cy}, {width, height}, img_size);
    center_pos = {clipped_box.x, clipped_box.y};
    size = {clipped_box.width, clipped_box.height};

    output_bbox.x = clipped_box.x - clipped_box.width/2.f;
    output_bbox.y = clipped_box.y - clipped_box.height/2.f;
    output_bbox.width = clipped_box.width;
    output_bbox.height = clipped_box.height;
    return scores.at<float>(best_idx);
}


cv::Mat NanoTracker::convert_bbox(const cv::Mat& delta, const cv::Mat& points) {
    // delta is (1, 4, 15, 15), needs to be (4, 225)
    cv::Mat delta_reshaped = delta.reshape(1, 4);

    cv::Mat p_t = points.t(); // (2, 225)
    
    // x1 = px - dx0, y1 = py - dy0
    // x2 = px + dx1, y2 = py + dy1
    cv::Mat pred_x1 = p_t.row(0) - delta_reshaped.row(0);
    cv::Mat pred_y1 = p_t.row(1) - delta_reshaped.row(1);
    cv::Mat pred_x2 = p_t.row(0) + delta_reshaped.row(2);
    cv::Mat pred_y2 = p_t.row(1) + delta_reshaped.row(3);
    
    // convert to (cx, cy, w, h)
    cv::Mat pred_cx = (pred_x1 + pred_x2) * 0.5;
    cv::Mat pred_cy = (pred_y1 + pred_y2) * 0.5;
    cv::Mat pred_w = pred_x2 - pred_x1;
    cv::Mat pred_h = pred_y2 - pred_y1;

    cv::Mat result;
    cv::vconcat(std::vector<cv::Mat>{pred_cx, pred_cy, pred_w, pred_h}, result);
    return result;
}

cv::Rect2f NanoTracker::bbox_clip(const cv::Point2f& center, const cv::Point2f& sz, const cv::Size& boundary) {
    float cx = std::max(0.0f, std::min(center.x, static_cast<float>(boundary.width)));
    float cy = std::max(0.0f, std::min(center.y, static_cast<float>(boundary.height)));
    float width = std::max(static_cast<float>(min_wh), std::min(sz.x, static_cast<float>(boundary.width)));
    float height = std::max(static_cast<float>(min_wh), std::min(sz.y, static_cast<float>(boundary.height)));
    return {cx, cy, width, height};
}
