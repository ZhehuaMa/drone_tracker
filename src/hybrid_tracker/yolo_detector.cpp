#include "yolo_detector.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <numeric>
#include <cstring>
#include <utility>
#include "preprocess/input_validation.h"

#define TILE_OVERLAP_RATIO  0.2f  // 切片重叠比例

YoloV8Detector::YoloV8Detector(std::unique_ptr<tracker_detail::YoloModelCalls> model,
    std::unique_ptr<YoloPreprocessor> preprocessor, float conf_thresh, float nms_thresh,
                               int target_class_id, float recall_relax_ratio, float max_box_area_ratio)
    : model_(std::move(model)),
      preprocessor_(std::move(preprocessor)),
      m_conf_thresh(conf_thresh),
      m_conf_thresh_current(conf_thresh),
      m_relax_ratio(recall_relax_ratio),
      m_nms_thresh(nms_thresh),
      m_max_box_area_ratio(max_box_area_ratio),
      m_target_class_id(target_class_id),
      m_result_ready(false),
      m_stop_requested(false)
{
}

YoloV8Detector::~YoloV8Detector() { stop(); }

bool YoloV8Detector::init() {
    if (!model_ || !preprocessor_) { return false; }
    try {
        input_contract_ = model_->inputContract();
        if (!matchesImageContract(input_contract_, 1.0f / 255.0f)) { return false; }
        input_h = input_contract_.shape[2];
        input_w = input_contract_.shape[3];
        m_stop_requested = false;
        m_worker_thread = std::thread(&YoloV8Detector::workerThreadLoop, this);
        return true;
    } catch (const std::exception &error) {
        std::cerr << "YOLO initialization failed: " << error.what() << std::endl;
        return false;
    }
}

void YoloV8Detector::stop()
{
    if (m_stop_requested.exchange(true))
    {
        return; // 已经请求停止
    }

    m_cv_request.notify_one(); // 唤醒工作线程以使其退出
    if (m_worker_thread.joinable())
    {
        m_worker_thread.join();
    }
}

bool YoloV8Detector::request(const FrameRef& frame, uint64_t frame_id, const cv::Rect& roi)
{
    if (validatePreprocessFrame(frame) != PreprocessError::None) { return false; }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_new_request || m_result_ready)
    {
        return false; // 上一个请求还未处理，忽略此请求（与Python逻辑一致）
    }
    m_next_request.frame = frame;
    m_next_request.frame_id = frame_id;
    m_next_request.roi = roi;

    m_new_request = true;
    m_result_ready = false;
    m_cv_request.notify_one(); // 唤醒工作线程
    return true;
}

bool YoloV8Detector::isBusy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_new_request;
}

bool YoloV8Detector::get_result(YoloResult& result)
{
    if (!m_result_ready)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    result = m_last_result;
    m_result_ready = false; // 标记结果已被取走
    return true;
}

std::vector<cv::Rect> YoloV8Detector::createOverlappingTiles(int width, int height, float overlap_ratio)
{
    // 短边固定 2 份，长边自适应份数使每块尽量正方形；
    // tile_size 保证 n 块恰好覆盖长度 L 且相邻重叠 overlap_ratio
    const float base_tile = std::min(width, height) / 2.0f;
    const int n_long = std::max(2, static_cast<int>(std::lround(std::max(width, height) / base_tile)));
    int n_h, n_w;
    if (height <= width) {
        n_h = 2;
        n_w = n_long;
    } else {
        n_h = n_long;
        n_w = 2;
    }

    auto axis_params = [overlap_ratio](int length, int n) {
        const float tile_size = length / (n - (n - 1) * overlap_ratio);
        const float step = tile_size * (1.0f - overlap_ratio);
        std::vector<int> starts(n);
        for (int i = 0; i < n; ++i)
            starts[i] = static_cast<int>(std::lround(i * step));
        // 每块的 [起点, 终点)，最后一块顶到 length
        std::vector<std::pair<int, int>> sizes(n);
        for (int i = 0; i < n; ++i) {
            const int s = starts[i];
            const int e = (i + 1 < n) ? starts[i + 1] + static_cast<int>(std::lround(tile_size - step))
                                      : length;
            sizes[i] = {s, std::min(e, length)};
        }
        return sizes;
    };

    const auto rows = axis_params(height, n_h);
    const auto cols = axis_params(width, n_w);
    std::vector<cv::Rect> tiles;
    for (const auto& [y0, y1] : rows)
        for (const auto& [x0, x1] : cols)
            tiles.emplace_back(x0, y0, x1 - x0, y1 - y0);
    return tiles;
}

std::pair<int, int> YoloV8Detector::getInputSize() const
{
    return {input_w, input_h};
}


void YoloV8Detector::workerThreadLoop()
{
    std::cout << "TensorRT worker thread started." << std::endl;
    
    while (true)
    {
        Request req;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv_request.wait(lock, [this] { return m_new_request || m_stop_requested; });

            if (m_stop_requested) break;

            req = std::move(m_next_request);
            m_next_request = Request{};
            m_new_request = false;
        } 
        
        if (!req.frame) continue;

        auto session_result{preprocessor_->beginFrame(req.frame, input_contract_)};
        req.frame.reset();
        if (!session_result || !session_result.value) {
            std::cerr << "YOLO beginFrame failed: " << session_result.message << std::endl;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_last_result = YoloResult{req.frame_id, false, Detection{}};
            m_result_ready = true;
            continue;
        }
        auto &session{*session_result.value};

        // 检测级联（与 Python 端 trt_worker 一致）：ROI -> 全图 -> 自适应切片，
        // 第一个产出目标类别检测的阶段胜出，最终取置信度最高的单框
        Detection best{};
        bool has_target = false;
        auto collect_target = [&](const std::vector<Detection>& dets) {
            for (const auto& det : dets)
            {
                if (det.classId >= m_target_class_id && det.score > best.score)
                {
                    best = det;
                    has_target = true;
                }
            }
        };

        // 1. ROI 优先（置信度阈值放宽以提高召回率）
        if (req.roi.width > 0 && req.roi.height > 0)
        {
            m_conf_thresh_current = m_conf_thresh * m_relax_ratio;
            std::vector<Detection> roi_detections;
            detectOnImage(session, req.roi, roi_detections);
            collect_target(roi_detections);
        }

        // 2. ROI 未找到目标，全图检测
        if (!has_target)
        {
            m_conf_thresh_current = m_conf_thresh;
            std::vector<Detection> full_detections;
            detectOnImage(session, {}, full_detections);
            collect_target(full_detections);
        }

        // 3. 全图仍然失败，自适应切片检测（一旦找到目标就停止）
        if (!has_target)
        {
            m_conf_thresh_current = m_conf_thresh;
            const int w{session.sourceSize().width};
            const int h{session.sourceSize().height};
            for (const auto& tile : createOverlappingTiles(w, h, TILE_OVERLAP_RATIO))
            {
                std::vector<Detection> tile_detections;
                detectOnImage(session, tile, tile_detections);
                collect_target(tile_detections);
                if (has_target)
                    break;
            }
        }

        // 存储结果：单框 + 帧号（Python 端 new_detections=[best_box], processed_frame_id）
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_last_result = YoloResult{req.frame_id, has_target, best};
            m_result_ready = true;
        }

        // Release frame reference
        req.frame.reset();
    }

    // std::cout << "TensorRT worker thread finished." << std::endl;
}

void YoloV8Detector::detectOnImage(YoloFrameSession &session, const cv::Rect &region,
                                   std::vector<Detection> &results) {
    auto prepared{session.prepare(region)};
    if (!prepared || !prepared.value.input) {
        std::cerr << "YOLO preprocessing failed: " << prepared.message << std::endl;
        return;
    }
    try {
        const auto output{model_->infer(*prepared.value.input)};
        if (output.tensors.empty() || output.tensors[0].empty() || output.tensors[0].dims != 3) {
            std::cerr << "Invalid YOLO output" << std::endl;
            return;
        }
        const auto &tensor{output.tensors[0]};
        const TensorShape dims{tensor.size[0], tensor.size[1], tensor.size[2]};
        const auto resolved{resolveYoloRegion(session.sourceSize(), region)};
        if (!resolved) { return; }
        postprocess(reinterpret_cast<float *>(tensor.data), dims, prepared.value.transform,
                    resolved.value.size(), cv::Point2f(resolved.value.x, resolved.value.y), results);
    } catch (const std::exception &error) {
        std::cerr << "YOLO inference failed: " << error.what() << std::endl;
    }
}

void YoloV8Detector::postprocess(float* output, const TensorShape &output_dims, const LetterboxTransform& lb, const cv::Size& image_size,
    const cv::Point2f& global_offset, std::vector<Detection>& results)
{
    // Python 版本中的输出形状是 [batch, 84, 8400] 或 [batch, num_classes+4, num_detections]
    // 假设 m_output_dims 是 [1, 84, 8400] (对于COCO)
    // d[0]=batch, d[1]=num_attrs (84), d[2]=num_proposals (8400)
    
    // C++ 中我们直接处理 [1, 84, 8400]
    // 检查：确认 postprocess 逻辑与 TrtEngine 输出的维度匹配
    
    int num_proposals = output_dims[2]; // e.g., 8400
    int num_attrs = output_dims[1]; // e.g., 84 (4 box + 80 class)
    int num_classes = num_attrs - 4;

    // 逻辑检查：如果 d[1] < d[2] (e.g., 84 < 8400)，则我们假设是 [B, C+4, N]
    // 如果 d[1] > d[2] (e.g., 8400 > 84)，则我们假设是 [B, N, C+4]
    
    if (num_attrs > num_proposals) {
        // 形状是 [B, N, C+4], e.g., [1, 8400, 84]
        // 交换它们以便后续逻辑统一
        std::swap(num_attrs, num_proposals);
        // num_proposals = 8400, num_attrs = 84
    }
    
    // 此时，num_proposals = 8400, num_attrs = 84, num_classes = 80
    // 但我们仍然需要知道内存布局

    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;
    std::vector<int> class_ids;
    
    // 检查内存布局
    // 假设布局是 [B, C+4, N] (num_attrs, num_proposals)
    if (output_dims[1] == num_attrs && output_dims[2] == num_proposals)
    {
        for (int i = 0; i < num_proposals; ++i)
        {
            // output 指向 [84, 8400] 的开头
            // 第 i 个 proposal 的数据是交错存储的
            // [cx_0...cx_N], [cy_0...cy_N], ..., [class0_0...class0_N], ...

            // 找到最高分数的类别
            float max_score = -1.0f;
            int max_class_id = -1;
            
            // 指向第i个proposal的第一个类别分数
            // (output + (4 * num_proposals) + i)
            float* class_scores_start = output + 4 * num_proposals; 
            for (int c = 0; c < num_classes; ++c)
            {
                // 分数 = (class_scores_start + c * num_proposals)[i]
                float score = class_scores_start[c * num_proposals + i];
                if (score > max_score)
                {
                    max_score = score;
                    max_class_id = c;
                }
            }

            // 检查是否满足置信度阈值（类别过滤由调用方的级联收集负责：
            // 目标类别定义为 cid >= target_class_id）
            if (max_score >= m_conf_thresh_current)
            {
                float cx = output[0 * num_proposals + i];
                float cy = output[1 * num_proposals + i];
                float w = output[2 * num_proposals + i];
                float h = output[3 * num_proposals + i];

                // 将坐标转换回原始图像空间
                float x1 = (cx - lb.left - w / 2.0f) / lb.scale_x + global_offset.x;
                float y1 = (cy - lb.top - h / 2.0f) / lb.scale_y + global_offset.y;
                float x2 = (cx - lb.left + w / 2.0f) / lb.scale_x + global_offset.x;
                float y2 = (cy - lb.top + h / 2.0f) / lb.scale_y + global_offset.y;

                if ((x2-x1)/image_size.width <= m_max_box_area_ratio && (y2-y1)/image_size.height <= m_max_box_area_ratio)  // 过滤掉过大的框
                {
                    boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                    scores.push_back(max_score);
                    class_ids.push_back(max_class_id);
                }
            }
        }
    }
    // 假设布局是 [B, N, C+4] (num_proposals, num_attrs)
    else if (output_dims[1] == num_proposals && output_dims[2] == num_attrs)
    {
        for (int i = 0; i < num_proposals; ++i)
        {
            // 第 i 个 proposal 的数据是连续的
            // [cx, cy, w, h, class0_score, class1_score, ...]
            float* proposal_data = output + i * num_attrs;
            
            float* class_scores = proposal_data + 4;
            float max_score = -1.0f;
            int max_class_id = -1;

            for (int c = 0; c < num_classes; ++c)
            {
                if (class_scores[c] > max_score)
                {
                    max_score = class_scores[c];
                    max_class_id = c;
                }
            }
            
            if (max_score >= m_conf_thresh_current)
            {
                float cx = proposal_data[0];
                float cy = proposal_data[1];
                float w = proposal_data[2];
                float h = proposal_data[3];

                float x1 = (cx - lb.left - w / 2.0f) / lb.scale_x + global_offset.x;
                float y1 = (cy - lb.top - h / 2.0f) / lb.scale_y + global_offset.y;
                float x2 = (cx - lb.left + w / 2.0f) / lb.scale_x + global_offset.x;
                float y2 = (cy - lb.top + h / 2.0f) / lb.scale_y + global_offset.y;

                if ((x2-x1)/image_size.width <= m_max_box_area_ratio && (y2-y1)/image_size.height <= m_max_box_area_ratio)  // 过滤掉过大的框
                {
                    boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                    scores.push_back(max_score);
                    class_ids.push_back(max_class_id);
                }
            }
        }
    }
    else 
    {
        std::cerr << "Error: Unsupported output dimension layout in postprocess." << std::endl;
    }
    
    // 执行 NMS
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, m_conf_thresh_current, m_nms_thresh, indices);

    for (int idx : indices)
    {
        results.push_back({cv::Rect2f(boxes[idx]), scores[idx], class_ids[idx]});
    }
}
