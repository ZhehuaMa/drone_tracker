#include "yolo_detector.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <numeric>
#include <vpi/OpenCVInterop.hpp>
#include <vpi/algo/Rescale.h>
#include <vpi/algo/ConvertImageFormat.h>

// 辅助函数：计算TRT维度的体积
// static int64_t volume(const nvinfer1::Dims& d)
// {
//     return std::accumulate(d.d, d.d + d.nbDims, 1LL, std::multiplies<int64_t>());
// }

#define TILE_OVERLAP_RATIO  0.2f  // 切片重叠比例

YoloV8Detector::YoloV8Detector(const std::string& engine_path, float conf_thresh, float nms_thresh, int target_class_id, float recall_relax_ratio)
    : m_engine_path(engine_path),
      m_conf_thresh(conf_thresh),
      m_conf_thresh_current(conf_thresh),
      m_relax_ratio(recall_relax_ratio),
      m_nms_thresh(nms_thresh),
      m_target_class_id(target_class_id),
      m_result_ready(false),
      m_stop_requested(false)
{
    if (vpiStreamCreate(0, &vpi_stream) != VPI_SUCCESS) {
        std::cerr << "Failed to create VPI stream" << std::endl;
        return;
    }
}

YoloV8Detector::~YoloV8Detector()
{
    stop();
    if (vpi_padded_image)
        vpiImageDestroy(vpi_padded_image);
    if (vpi_resized_image)
        vpiImageDestroy(vpi_resized_image);
    if (vpi_stream) {
        vpiStreamDestroy(vpi_stream);
        vpi_stream = nullptr;
    }
}

bool YoloV8Detector::init()
{
    // 1. 创建 TrtEngine 实例
    try {
        m_trt_engine = std::make_unique<TrtEngine>(m_engine_path);
    } catch (const std::exception& e) {
        std::cerr << "Error initializing TrtEngine: " << e.what() << std::endl;
        return false;
    }

    const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;

    // 2. 从 TrtEngine 获取维度信息
    try {
        std::string input_name = m_trt_engine->getPrimaryInputName();
        std::string output_name = m_trt_engine->getPrimaryOutputName();
        m_input_dims = m_trt_engine->getTensorDims(input_name);
        m_output_dims = m_trt_engine->getTensorDims(output_name);
        input_buffer = TensorBuffer(m_input_dims, sizeof(float));
        input_h = m_input_dims.d[2];
        input_w = m_input_dims.d[3];
        vpiImageCreate(input_w, input_h, VPI_IMAGE_FORMAT_F32, flags, &vpi_padded_image);
    } catch (const std::exception& e) {
        std::cerr << "Error getting tensor info from TrtEngine: " << e.what() << std::endl;
        return false;
    }

    if (m_input_dims.nbDims == 0 || m_output_dims.nbDims == 0)
    {
         std::cerr << "Error: TrtEngine returned invalid dimensions." << std::endl;
         return false;
    }

    // 5. 启动工作线程
    m_stop_requested = false;
    m_worker_thread = std::thread(&YoloV8Detector::workerThreadLoop, this);

    std::cout << "YoloV8Detector initialized successfully." << std::endl;
    return true;
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

bool YoloV8Detector::request(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect& roi, bool use_tiling)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_new_request || m_result_ready)
    {
        return false; // 上一个请求还未处理，忽略此请求（与Python逻辑一致）
    }
    m_next_request.frame = frame;
    m_next_request.roi = roi;
    m_next_request.use_tiling = use_tiling;
    
    m_new_request = true;
    m_result_ready = false;
    m_cv_request.notify_one(); // 唤醒工作线程
    return true;
}

bool YoloV8Detector::is_result_ready() const
{
    return m_result_ready.load();
}

bool YoloV8Detector::get_result(std::vector<Detection>& detections)
{
    if (!m_result_ready)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    detections = m_last_detections;
    m_result_ready = false; // 标记结果已被取走
    return true;
}

bool YoloV8Detector::get_result(std::priority_queue<Detection>& detections)
{
    if (!m_result_ready)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& det : m_last_detections) {
        detections.push(det);
    }
    m_result_ready = false; // 标记结果已被取走
    return true;
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

            req = m_next_request;
            m_new_request = false;
        } 
        
        if (!req.frame) continue;

        std::vector<Detection> all_detections;
        VPIImage vpi_input = req.frame->get();
        
        cv::Point2f global_offset(0, 0);

        const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;

        // 1. Detect on full or ROI image 
        // Handle ROI
        if (req.roi.width > 0 && req.roi.height > 0)
        {
            m_conf_thresh_current = m_conf_thresh * m_relax_ratio; // 降低置信度阈值以提高召回率
            VPIImage vpi_roi_view = nullptr;
            VPIRectangleI roi_rect = {req.roi.x, req.roi.y, req.roi.width-1, req.roi.height-1};
            auto status = vpiImageCreateView(vpi_input, &roi_rect, flags, &vpi_roi_view);            
            if (status == VPI_SUCCESS)
            {
                global_offset = cv::Point2f(req.roi.x, req.roi.y);
                detectOnImage(vpi_roi_view, global_offset, all_detections);
                vpiImageDestroy(vpi_roi_view);
            }
            else
            {
                std::cerr << "Failed to create ROI view, using full image. error code: " << status << std::endl;
            }
        }
        else
        {
            m_conf_thresh_current = m_conf_thresh;
            detectOnImage(vpi_input, global_offset, all_detections);
        }

        // 2. Tiling strategy (only if no detections and tiling enabled)
        if (all_detections.empty() && req.use_tiling)
        {
            int w, h;
            vpiImageGetSize(vpi_input, &w, &h);
            int overlap_w = static_cast<int>(w * (1 + TILE_OVERLAP_RATIO) * 0.5);
            int overlap_h = static_cast<int>(h * (1 + TILE_OVERLAP_RATIO) * 0.5);

            std::vector<VPIRectangleI> tiles = {
                {0, 0, overlap_w, overlap_h},
                {w - overlap_w, 0, overlap_w, overlap_h},
                {0, h - overlap_h, overlap_w, overlap_h},
                {w - overlap_w, h - overlap_h, overlap_w, overlap_h}
            };
            
            m_conf_thresh_current = m_conf_thresh;
            for (const auto& t : tiles)
            {
                // Clamp to image boundaries
                int x = std::max(0, t.x);
                int y = std::max(0, t.y);
                int tw = std::min(w - x, t.width);
                int th = std::min(h - y, t.height);

                VPIRectangleI tile_rect {x, y, tw, th};
                VPIImage tile_view;
                if (vpiImageCreateView(vpi_input, &tile_rect, flags, &tile_view) == VPI_SUCCESS)
                {
                    detectOnImage(tile_view, global_offset + cv::Point2f(x, y), all_detections);
                    vpiImageDestroy(tile_view);
                    if (!all_detections.empty())
                        break; // 找到目标后跳出
                }
                else
                {
                    std::cerr << "Failed to create tile view at (" << x << ", " << y << ", " << tw << ", " << th << ")" << std::endl;
                }
            }
        }
        
        // Final NMS on merged results
        if (all_detections.size() > 1)
        {
            std::vector<cv::Rect2d> boxes;
            std::vector<float> scores;
            for (const auto& det : all_detections) {
                boxes.push_back(det.box);
                scores.push_back(det.score);
            }
            std::vector<int> indices;
            cv::dnn::NMSBoxes(boxes, scores, m_conf_thresh, m_nms_thresh, indices);
            
            std::vector<Detection> filtered_detections;
            for(int idx : indices) filtered_detections.push_back(all_detections[idx]);
            all_detections = filtered_detections;
        }

        // Store results
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_last_detections = std::move(all_detections);
            m_result_ready = true;
        }
        
        // Release frame reference
        req.frame.reset();
    }
    
    // std::cout << "TensorRT worker thread finished." << std::endl;
}

void YoloV8Detector::detectOnImage(VPIImage image, const cv::Point2f& global_offset, std::vector<Detection>& results)
{
    float scale = 1.0f;
    cv::Point2f offset(0, 0);
    cv::Size image_size;
    vpiImageGetSize(image, &image_size.width, &image_size.height);
    if (!preprocess(image, scale, image_size, offset, input_buffer))
    {
        std::cerr << "Error during preprocessing." << std::endl;
        return;
    }

    // 调用 TrtEngine 进行同步推理
    std::vector<TensorBuffer> trt_results;
    try {
        trt_results = m_trt_engine->infer({&input_buffer});
    } catch (const std::exception& e) {
        std::cerr << "Error during TrtEngine inference: " << e.what() << std::endl;
        return;
    }

    if (trt_results.empty() || trt_results[0].host_ptr == nullptr) {
         std::cerr << "Error: TrtEngine output is invalid." << std::endl;
         return;
    }
    
    postprocess(reinterpret_cast<float*>(trt_results[0].host_ptr), scale, image_size, offset, global_offset, results);
}

void YoloV8Detector::update_resized_image(int new_width, int new_height, VPIImageFormat new_format, int flags)
{
    int img_w = 0;
    int img_h = 0;
    VPIImageFormat format;
    if (vpi_resized_image) {
        vpiImageGetSize(vpi_resized_image, &img_w, &img_h);
        vpiImageGetFormat(vpi_resized_image, &format);
    }
    if (img_w != new_width || img_h != new_height || format != new_format) {
        if (vpi_resized_image) {
            vpiImageDestroy(vpi_resized_image);
        }
        auto status = vpiImageCreate(new_width, new_height, new_format, flags, &vpi_resized_image);
        if (status != VPI_SUCCESS) {
            std::cerr << "Failed to create VPI resized image" << std::endl;
            return;
        }
    }
}

bool YoloV8Detector::preprocess(VPIImage vpiIm, float &scale, const cv::Size& image_size, cv::Point2f& offset, TensorBuffer& buffer)
{
    if (vpiIm == nullptr) {
        std::cerr << "Input VPIImage is null" << std::endl;
        return false;
    }
    // 输入维度 (batch, C, H, W)
    // const int input_h = m_input_dims.d[2];
    // const int input_w = m_input_dims.d[3];

    const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;

    VPIImageFormat img_format;
    vpiImageGetFormat(vpiIm, &img_format);

     // 计算缩放比例和letterbox
    scale = std::min(static_cast<float>(input_h) / image_size.height, static_cast<float>(input_w) / image_size.width);
    int new_h = static_cast<int>(image_size.height * scale);
    int new_w = static_cast<int>(image_size.width * scale);

    offset.x = (input_w - new_w) / 2.0f; // dw
    offset.y = (input_h - new_h) / 2.0f; // dh

    VPIStatus status;
    bool resized = false;
    if (std::fabs(scale - 1.0f) > 1e-6) {
        update_resized_image(new_w, new_h, img_format, flags);
        status = vpiSubmitRescale(vpi_stream, VPI_BACKEND_CUDA, vpiIm, vpi_resized_image, VPI_INTERP_LINEAR, VPI_BORDER_CLAMP, 0);
        if (status != VPI_SUCCESS) {
            std::cerr << "Failed to submit VPI rescale" << std::endl;
            return false;
        }
        resized = true;
    }
    VPIImage vpi_view = nullptr;
    VPIRectangleI roi_patch {
        .x = static_cast<int>(offset.x), 
        .y = static_cast<int>(offset.y),
        .width = new_w,
        .height = new_h
    };
    status = vpiImageCreateView(vpi_padded_image, &roi_patch, flags, &vpi_view);
    static const VPIConvertImageFormatParams fmtParams {
        .policy = VPI_CONVERSION_CLAMP,
        .scale = 1.0f / 255.0f,
        .offset = 0.0f,
        .flags = 0,
        .chromaUpFilter = VPI_INTERP_NEAREST,
        .chromaDownFilter = VPI_INTERP_NEAREST,
    };
    status = vpiSubmitConvertImageFormat(vpi_stream, VPI_BACKEND_CUDA, 
        resized ? vpi_resized_image : vpiIm, vpi_view, &fmtParams);
    if (status != VPI_SUCCESS) {
        std::cerr << "Failed to submit VPI format conversion" << std::endl;
        return false;
    }
    vpiStreamSync(vpi_stream);
    vpiImageDestroy(vpi_view);


    VPIImageData f32Data;
    vpiImageLockData(vpi_padded_image, VPI_LOCK_READ, VPI_IMAGE_BUFFER_CUDA_PITCH_LINEAR, &f32Data);
    size_t width_bytes = input_w * sizeof(float);
    cudaMemcpy2DAsync(
        static_cast<float*>(buffer.device_ptr), width_bytes,
        f32Data.buffer.pitch.planes[0].data, f32Data.buffer.pitch.planes[0].pitchBytes,
        width_bytes, input_h, cudaMemcpyDeviceToDevice);
    cudaMemcpy2DAsync(
        static_cast<float*>(buffer.device_ptr) + input_w * input_h, width_bytes,
        f32Data.buffer.pitch.planes[0].data, f32Data.buffer.pitch.planes[0].pitchBytes,
        width_bytes, input_h, cudaMemcpyDeviceToDevice);
    cudaMemcpy2DAsync(
        static_cast<float*>(buffer.device_ptr) + input_w * input_h * 2, width_bytes,
        f32Data.buffer.pitch.planes[0].data, f32Data.buffer.pitch.planes[0].pitchBytes,
        width_bytes, input_h, cudaMemcpyDeviceToDevice);
    cudaStreamSynchronize(0);
    vpiImageUnlock(vpi_padded_image);
    return true;
}

void YoloV8Detector::postprocess(float* output, float scale, const cv::Size& image_size, 
    const cv::Point2f& offset, const cv::Point2f& global_offset, std::vector<Detection>& results)
{
    // Python 版本中的输出形状是 [batch, 84, 8400] 或 [batch, num_classes+4, num_detections]
    // 假设 m_output_dims 是 [1, 84, 8400] (对于COCO)
    // d[0]=batch, d[1]=num_attrs (84), d[2]=num_proposals (8400)
    
    // C++ 中我们直接处理 [1, 84, 8400]
    // 检查：确认 postprocess 逻辑与 TrtEngine 输出的维度匹配
    
    int num_proposals = m_output_dims.d[2]; // e.g., 8400
    int num_attrs = m_output_dims.d[1]; // e.g., 84 (4 box + 80 class)
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
    if (m_output_dims.d[1] == num_attrs && m_output_dims.d[2] == num_proposals)
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

            // 检查是否满足置信度阈值
            if (max_score >= m_conf_thresh_current)
            {
                // 检查是否是我们想要的目标类别
                if (m_target_class_id == -1 || max_class_id == m_target_class_id)
                {
                    float cx = output[0 * num_proposals + i];
                    float cy = output[1 * num_proposals + i];
                    float w = output[2 * num_proposals + i];
                    float h = output[3 * num_proposals + i];

                    // 将坐标转换回原始图像空间
                    float x1 = (cx - offset.x - w / 2.0f) / scale + global_offset.x;
                    float y1 = (cy - offset.y - h / 2.0f) / scale + global_offset.y;
                    float x2 = (cx - offset.x + w / 2.0f) / scale + global_offset.x;
                    float y2 = (cy - offset.y + h / 2.0f) / scale + global_offset.y;

                    if (x2-x1 < image_size.width*0.8 && y2-y1 < image_size.height*0.8)  // 过滤掉过大的框
                    {
                        boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                        scores.push_back(max_score);
                        class_ids.push_back(max_class_id);
                    }
                }
            }
        }
    }
    // 假设布局是 [B, N, C+4] (num_proposals, num_attrs)
    else if (m_output_dims.d[1] == num_proposals && m_output_dims.d[2] == num_attrs)
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
                 if (m_target_class_id == -1 || max_class_id == m_target_class_id)
                 {
                    float cx = proposal_data[0];
                    float cy = proposal_data[1];
                    float w = proposal_data[2];
                    float h = proposal_data[3];

                    float x1 = (cx - offset.x - w / 2.0f) / scale + global_offset.x;
                    float y1 = (cy - offset.y - h / 2.0f) / scale + global_offset.y;
                    float x2 = (cx - offset.x + w / 2.0f) / scale + global_offset.x;
                    float y2 = (cy - offset.y + h / 2.0f) / scale + global_offset.y;
                    
                    if (x2-x1 < image_size.width*0.8 && y2-y1 < image_size.height*0.8)  // 过滤掉过大的框
                    {
                        boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                        scores.push_back(max_score);
                        class_ids.push_back(max_class_id);
                    }
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

