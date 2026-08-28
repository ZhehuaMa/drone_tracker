#include "yolo_detector.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <numeric>
#include <cstring>
#include <vpi/OpenCVInterop.hpp>
#include <vpi/algo/Rescale.h>
#include <vpi/algo/ConvertImageFormat.h>

// 辅助函数：计算TRT维度的体积
// static int64_t volume(const nvinfer1::Dims& d)
// {
//     return std::accumulate(d.d, d.d + d.nbDims, 1LL, std::multiplies<int64_t>());
// }

#define TILE_OVERLAP_RATIO  0.2f  // 切片重叠比例

YoloV8Detector::YoloV8Detector(const std::string& engine_path, float conf_thresh, float nms_thresh,
                               int target_class_id, float recall_relax_ratio, float max_box_area_ratio)
    : m_engine_path(engine_path),
      m_conf_thresh(conf_thresh),
      m_conf_thresh_current(conf_thresh),
      m_relax_ratio(recall_relax_ratio),
      m_nms_thresh(nms_thresh),
      m_max_box_area_ratio(max_box_area_ratio),
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
    if (vpi_fill_image)
        vpiImageDestroy(vpi_fill_image);
    if (vpi_frame_copy)
        vpiImageDestroy(vpi_frame_copy);
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
        // 创建恒为 114 的 U8 填充图，用于 letterbox 边距填充（与 Python 的
        // copyMakeBorder(value=(114,114,114)) 对齐；转换时 scale=1/255）
        if (vpiImageCreate(input_w, input_h, VPI_IMAGE_FORMAT_U8, flags, &vpi_fill_image) == VPI_SUCCESS) {
            VPIImageData fill_data;
            if (vpiImageLockData(vpi_fill_image, VPI_LOCK_WRITE,
                    VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &fill_data) == VPI_SUCCESS) {
                uint8_t* row = static_cast<uint8_t*>(fill_data.buffer.pitch.planes[0].data);
                for (int y = 0; y < input_h; ++y)
                    memset(row + y * fill_data.buffer.pitch.planes[0].pitchBytes, 114, input_w);
                vpiImageUnlock(vpi_fill_image);
            }
        }
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

bool YoloV8Detector::request(const std::shared_ptr<VPIImageWrapper>& frame, uint64_t frame_id, const cv::Rect& roi)
{
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

            req = m_next_request;
            m_new_request = false;
        } 
        
        if (!req.frame) continue;

        const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;

        // 立即把帧复制到工作线程私有的 VPI 图并释放原帧引用。
        // Python 端在 trt 交接前显式 copy（"trt 交接独立 copy"）；主线程的
        // DCF/补帧会对缓存帧做主机端 vpiImageLockData，若工作线程仍在同一
        // VPIImage 上提交流操作，会破坏 VPI 的锁协议并导致内部状态损坏。
        // 复制后工作线程只触碰私有副本，与主线程彻底隔离。
        VPIImage vpi_input = nullptr;
        {
            VPIImage src = req.frame->get();
            int w = 0, h = 0;
            vpiImageGetSize(src, &w, &h);
            if (vpi_frame_copy && (copy_w != w || copy_h != h)) {
                vpiImageDestroy(vpi_frame_copy);
                vpi_frame_copy = nullptr;
            }
            if (!vpi_frame_copy) {
                if (vpiImageCreate(w, h, VPI_IMAGE_FORMAT_U8, flags, &vpi_frame_copy) != VPI_SUCCESS) {
                    std::cerr << "Failed to create worker frame copy" << std::endl;
                    req.frame.reset();
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_last_result = YoloResult{req.frame_id, false, Detection{}};
                        m_result_ready = true;
                    }
                    continue;
                }
                copy_w = w;
                copy_h = h;
            }
            if (vpiSubmitConvertImageFormat(vpi_stream, VPI_BACKEND_CUDA, src, vpi_frame_copy, nullptr) != VPI_SUCCESS) {
                std::cerr << "Failed to copy frame for worker" << std::endl;
                req.frame.reset();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_last_result = YoloResult{req.frame_id, false, Detection{}};
                    m_result_ready = true;
                }
                continue;
            }
            vpiStreamSync(vpi_stream);
            vpi_input = vpi_frame_copy;
            req.frame.reset();
        }


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
            VPIImage vpi_roi_view = nullptr;
            VPIRectangleI roi_rect = {req.roi.x, req.roi.y, req.roi.width, req.roi.height};
            auto status = vpiImageCreateView(vpi_input, &roi_rect, flags, &vpi_roi_view);
            if (status == VPI_SUCCESS)
            {
                std::vector<Detection> roi_detections;
                detectOnImage(vpi_roi_view, cv::Point2f(req.roi.x, req.roi.y), roi_detections);
                collect_target(roi_detections);
                vpiImageDestroy(vpi_roi_view);
            }
            else
            {
                std::cerr << "Failed to create ROI view. error code: " << status << std::endl;
            }
        }

        // 2. ROI 未找到目标，全图检测
        if (!has_target)
        {
            m_conf_thresh_current = m_conf_thresh;
            std::vector<Detection> full_detections;
            detectOnImage(vpi_input, cv::Point2f(0, 0), full_detections);
            collect_target(full_detections);
        }

        // 3. 全图仍然失败，自适应切片检测（一旦找到目标就停止）
        if (!has_target)
        {
            m_conf_thresh_current = m_conf_thresh;
            int w, h;
            vpiImageGetSize(vpi_input, &w, &h);
            for (const auto& tile : createOverlappingTiles(w, h, TILE_OVERLAP_RATIO))
            {
                VPIImage tile_view = nullptr;
                VPIRectangleI tile_rect = {tile.x, tile.y, tile.width, tile.height};
                if (vpiImageCreateView(vpi_input, &tile_rect, flags, &tile_view) != VPI_SUCCESS)
                {
                    std::cerr << "Failed to create tile view at (" << tile.x << ", " << tile.y << ", "
                              << tile.width << ", " << tile.height << ")" << std::endl;
                    continue;
                }
                std::vector<Detection> tile_detections;
                detectOnImage(tile_view, cv::Point2f(tile.x, tile.y), tile_detections);
                collect_target(tile_detections);
                vpiImageDestroy(tile_view);
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

void YoloV8Detector::detectOnImage(VPIImage image, const cv::Point2f& global_offset, std::vector<Detection>& results)
{
    LetterboxInfo lb;
    cv::Size image_size;
    vpiImageGetSize(image, &image_size.width, &image_size.height);
    if (!preprocess(image, lb, image_size, input_buffer))
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

    postprocess(reinterpret_cast<float*>(trt_results[0].host_ptr), lb, image_size, global_offset, results);
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

bool YoloV8Detector::preprocess(VPIImage vpiIm, LetterboxInfo& lb, const cv::Size& image_size, TensorBuffer& buffer)
{
    if (vpiIm == nullptr) {
        std::cerr << "Input VPIImage is null" << std::endl;
        return false;
    }

    const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;

    VPIImageFormat img_format;
    vpiImageGetFormat(vpiIm, &img_format);

    // letterbox：scale 取 min，内容尺寸用 round（float 截断会让 1536*0.33333 -> 639，
    // 内容比浮点 dw/dh 反算小 ~1px，导致非方输入的框整体偏移几个像素）
    float scale = std::min(static_cast<float>(input_h) / image_size.height,
                           static_cast<float>(input_w) / image_size.width);
    int new_h = static_cast<int>(std::lround(image_size.height * scale));
    int new_w = static_cast<int>(std::lround(image_size.width * scale));
    new_w = std::min(new_w, input_w);
    new_h = std::min(new_h, input_h);

    // 返回真实整数 padding（对称：left = pad/2），反算用每轴比例
    int pad_w = input_w - new_w;
    int pad_h = input_h - new_h;
    lb.scale_x = new_w / static_cast<float>(image_size.width);
    lb.scale_y = new_h / static_cast<float>(image_size.height);
    lb.left = pad_w / 2;
    lb.top = pad_h / 2;

    VPIStatus status;
    bool resized = false;
    if (new_w != image_size.width || new_h != image_size.height) {
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
        .x = lb.left,
        .y = lb.top,
        .width = new_w,
        .height = new_h
    };
    static const VPIConvertImageFormatParams fmtParams {
        .policy = VPI_CONVERSION_CLAMP,
        .scale = 1.0f / 255.0f,
        .offset = 0.0f,
        .flags = 0,
        .chromaUpFilter = VPI_INTERP_NEAREST,
        .chromaDownFilter = VPI_INTERP_NEAREST,
    };
    // 用 114 填充 letterbox 边距。与 nano_tracker 的搜索窗填充同理：不能
    // "整图填充后再用视图覆写内容区"——无中间同步的两次重叠写同一张 VPI 图
    // 在 VPI 3.2 上可能全部丢失（实测全零）。这里只写与内容不相交的四条
    // 边距带，所有写入互不相交。
    VPIImage margin_views[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    int margin_view_count = 0;
    if ((pad_w > 0 || pad_h > 0) && vpi_fill_image) {
        const struct { int x, y, w, h; } bands[4] = {
            {0, 0, input_w, lb.top},
            {0, lb.top + new_h, input_w, input_h - lb.top - new_h},
            {0, lb.top, lb.left, new_h},
            {lb.left + new_w, lb.top, input_w - lb.left - new_w, new_h},
        };
        for (const auto& band : bands) {
            if (band.w <= 0 || band.h <= 0)
                continue;
            VPIImage fill_view = nullptr, canvas_view = nullptr;
            const VPIRectangleI band_rect {band.x, band.y, band.w, band.h};
            const bool ok_view =
                vpiImageCreateView(vpi_fill_image, &band_rect, flags, &fill_view) == VPI_SUCCESS &&
                vpiImageCreateView(vpi_padded_image, &band_rect, flags, &canvas_view) == VPI_SUCCESS;
            if (ok_view &&
                vpiSubmitConvertImageFormat(vpi_stream, VPI_BACKEND_CUDA, fill_view, canvas_view,
                                            &fmtParams) == VPI_SUCCESS) {
                margin_views[margin_view_count++] = fill_view;
                margin_views[margin_view_count++] = canvas_view;
                continue;
            }
            std::cerr << "Failed to fill letterbox margins" << std::endl;
            if (fill_view) vpiImageDestroy(fill_view);
            if (canvas_view) vpiImageDestroy(canvas_view);
        }
    }
    status = vpiImageCreateView(vpi_padded_image, &roi_patch, flags, &vpi_view);
    if (status != VPI_SUCCESS) {
        std::cerr << "Failed to create letterbox content view" << std::endl;
        for (int k = 0; k < margin_view_count; ++k) vpiImageDestroy(margin_views[k]);
        return false;
    }
    status = vpiSubmitConvertImageFormat(vpi_stream, VPI_BACKEND_CUDA,
        resized ? vpi_resized_image : vpiIm, vpi_view, &fmtParams);
    if (status != VPI_SUCCESS) {
        std::cerr << "Failed to submit VPI format conversion" << std::endl;
        vpiImageDestroy(vpi_view);
        for (int k = 0; k < margin_view_count; ++k) vpiImageDestroy(margin_views[k]);
        return false;
    }
    vpiStreamSync(vpi_stream);
    vpiImageDestroy(vpi_view);
    for (int k = 0; k < margin_view_count; ++k)
        vpiImageDestroy(margin_views[k]);


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

void YoloV8Detector::postprocess(float* output, const LetterboxInfo& lb, const cv::Size& image_size,
    const cv::Point2f& global_offset, std::vector<Detection>& results)
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

