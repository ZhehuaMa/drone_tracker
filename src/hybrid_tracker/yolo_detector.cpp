#include "model/validation.h"
#include "yolo_detector.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <numeric>
#include <cstring>
#include <utility>
#include "preprocess/input_validation.h"

#define TILE_OVERLAP_RATIO  0.2f  // 切片重叠比例

YoloV8Detector::YoloV8Detector(std::unique_ptr<model::YoloBackend> model,
    std::unique_ptr<YoloPreprocessor> preprocessor, float conf_thresh, float nms_thresh,
                               int target_class_id, float recall_relax_ratio, float max_box_area_ratio, DetectionThreadLauncher launcher)
    : model_(std::move(model)),
      preprocessor_(std::move(preprocessor)),
      m_conf_thresh(conf_thresh),
      m_conf_thresh_current(conf_thresh),
      m_relax_ratio(recall_relax_ratio),
      m_nms_thresh(nms_thresh),
      m_max_box_area_ratio(max_box_area_ratio),
      m_target_class_id(target_class_id),
      m_launcher(std::move(launcher)),
      m_result_ready(false),
      m_stop_requested(false)
{
}

YoloV8Detector::~YoloV8Detector() { stop(); }

bool YoloV8Detector::init() {
    if (!model_ || !preprocessor_) { return false; }
    try {
        input_contract_ = model_->inputContract();
        const auto size{yoloInputSize(input_contract_)};
        const auto count{model_->classCount()};
        if (!size) {
            std::cerr << "YOLO initialization failed: " << size.message << std::endl;
            return false;
        }
        if (!count) {
            std::cerr << model::describe(count.error) << std::endl;
            return false;
        }
        if (m_target_class_id < 0 || m_target_class_id >= count.value) {
            std::cerr << "YOLO target class " << m_target_class_id << " outside 0.." << count.value - 1 << std::endl;
            return false;
        }
        class_count_ = count.value;
        input_h = size.value.height;
        input_w = size.value.width;
        m_stop_requested = false;
        m_worker_thread = m_launcher([this] { workerThreadLoop(); });
        if (!m_worker_thread.joinable()) { return false; }
        return true;
    } catch (const std::exception &error) {
        std::cerr << "YOLO initialization failed: " << error.what() << std::endl;
        return false;
    }
}

void YoloV8Detector::stop()
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_stop_requested.exchange(true)) { return; }
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
    if (m_cleaning || m_stop_requested || m_new_request || m_result_ready)
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

void YoloV8Detector::finishRound() {
    std::unique_lock<std::mutex> lock{m_mutex};
    m_cleaning = true;
    m_new_request = false;
    m_next_request = {};
    m_cv_idle.wait(lock, [this] { return !m_executing; });
    m_last_result = {};
    m_result_ready = false;
    m_cleaning = false;
}

bool YoloV8Detector::isBusy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_new_request;
}

bool YoloV8Detector::get_result(YoloResult& result)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_result_ready || m_cleaning) { return false; }
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
    std::cout << "YOLO worker thread started." << std::endl;
    
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
            m_executing = true;
        } 
        
        // 此守卫先创建，确保会话和请求帧先释放，再发布空闲通知。
        auto complete{[this](void *) {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_executing = false;
            m_cv_idle.notify_all();
        }};
        std::unique_ptr<void, decltype(complete)> execution{this, complete};
        try {
            if (!req.frame) { continue; }

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
                    if (det.classId == m_target_class_id && det.score > best.score)
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
        } catch (const std::exception &error) {
            req.frame.reset();
            std::cerr << "YOLO request failed: " << error.what() << std::endl;
            std::lock_guard<std::mutex> lock{m_mutex};
            m_last_result = YoloResult{req.frame_id, false, Detection{}};
            m_result_ready = true;
        }
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
        const auto output{model_->infer(prepared.value.input)};
        if (!output) {
            std::cerr << model::describe(output.error) << std::endl;
            return;
        }
        const auto checked{model::yoloPrediction({output.value.predictions}, "YoloV8Detector")};
        if (!checked || output.value.attribute_axis != checked.value.attribute_axis
            || output.value.candidate_axis != checked.value.candidate_axis) { return; }
        const auto view{output.value.predictions.read()};
        const auto &dims{output.value.predictions.descriptor.shape};
        if (dims[1] - 4 != class_count_) { return; }
        const auto resolved{resolveYoloRegion(session.sourceSize(), region)};
        if (!resolved) { return; }
        postprocess(view.data(), dims, prepared.value.transform,
                    resolved.value.size(), cv::Point2f(resolved.value.x, resolved.value.y), results);
    } catch (const std::exception &error) {
        std::cerr << "YOLO inference failed: " << error.what() << std::endl;
    }
}

void YoloV8Detector::postprocess(const float* output, const TensorShape &output_dims, const LetterboxTransform& lb, const cv::Size& image_size,
    const cv::Point2f& global_offset, std::vector<Detection>& results)
{
    const std::size_t num_proposals{static_cast<std::size_t>(output_dims[2])};
    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;
    std::vector<int> class_ids;
    for (std::size_t i{0}; i < num_proposals; ++i) {
        const float score{output[(4 + static_cast<std::size_t>(m_target_class_id)) * num_proposals + i]};
        if (score >= m_conf_thresh_current) {
            const float cx{output[i]};
            const float cy{output[num_proposals + i]};
            const float w{output[2 * num_proposals + i]};
            const float h{output[3 * num_proposals + i]};
            const float x1{(cx - lb.left - w / 2.0f) / lb.scale_x + global_offset.x};
            const float y1{(cy - lb.top - h / 2.0f) / lb.scale_y + global_offset.y};
            const float x2{(cx - lb.left + w / 2.0f) / lb.scale_x + global_offset.x};
            const float y2{(cy - lb.top + h / 2.0f) / lb.scale_y + global_offset.y};
            if ((x2 - x1) / image_size.width <= m_max_box_area_ratio &&
                (y2 - y1) / image_size.height <= m_max_box_area_ratio) {
                boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                scores.push_back(score);
                class_ids.push_back(m_target_class_id);
            }
        }
    }

    // 执行 NMS
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, m_conf_thresh_current, m_nms_thresh, indices);

    for (int idx : indices)
    {
        results.push_back({cv::Rect2f(boxes[idx]), scores[idx], class_ids[idx]});
    }
}
