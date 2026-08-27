#include "nano_tracker.h"
#include <numeric>
#include <algorithm>
#include <cstring>
#include <vpi/OpenCVInterop.hpp>
#include <vpi/algo/Rescale.h>
#include <vpi/algo/ConvertImageFormat.h>

// Helper to create Dims for TensorBuffer initialization
static nvinfer1::Dims create_dims(int b, int c, int h, int w) {
    nvinfer1::Dims dims;
    dims.nbDims = 4;
    dims.d[0] = b;
    dims.d[1] = c;
    dims.d[2] = h;
    dims.d[3] = w;
    return dims;
}

NanoTracker::NanoTracker(const std::string& backbone_path, const std::string& head_path) :
    // Initialize input buffers in the member initializer list
    z_crop_buffer(create_dims(1, 3, template_size, template_size), sizeof(float)),
    x_crop_buffer(create_dims(1, 3, instance_size, instance_size), sizeof(float))
{
    session_backbone = std::make_unique<TrtEngine>(backbone_path);
    session_head = std::make_unique<TrtEngine>(head_path);
    
    if (vpiStreamCreate(0, &vpi_stream) != VPI_SUCCESS) {
        std::cerr << "Failed to create VPI stream" << std::endl;
        return;
    }

    // Create Hanning window
    cv::Mat hanning;
    cv::createHanningWindow(hanning, cv::Size(score_size, score_size), CV_32F);
    window = hanning.reshape(1, 1); // Flatten
    
    generate_points();
}

NanoTracker::~NanoTracker() {
    if (vpi_padded_image) {
        vpiImageDestroy(vpi_padded_image);
        vpi_padded_image = nullptr;
    }
    if (vpi_mean_image) {
        vpiImageDestroy(vpi_mean_image);
        vpi_mean_image = nullptr;
    }
    for (auto& pair : vpi_model_images) {
        vpiImageDestroy(pair.second);
    }
    if (vpi_stream) {
        vpiStreamDestroy(vpi_stream);
        vpi_stream = nullptr;
    }
}

void NanoTracker::reset() {
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

void NanoTracker::init(const std::shared_ptr<VPIImageWrapper>& frame, const cv::Rect2f& bbox) {
    m_initialized = false;

    if (!frame || !frame->get())
        return;
    int w = 0, h = 0;
    vpiImageGetSize(frame->get(), &w, &h);
    if (w <= 0 || h <= 0)
        return;

    // 与 Python 端一致：不裁剪输入框本身。目标处于 FOV 边缘时检测框会部分
    // 越界——按图像裁剪会产生狭长的退化模板（边缘跟踪抖动），完全越界时甚至
    // 得到空框（初始化死循环）。搜索窗越界由 get_subwindow_buffer 的均值填充
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

    // 计算通道均值（Python: channel_average = np.mean(img, axis=(0,1))），
    // 仅用于搜索窗越界时的均值填充
    channel_average = 114;
    if (frame && frame->get()) {
        VPIImageData host_data;
        if (vpiImageLockData(frame->get(), VPI_LOCK_READ,
                VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &host_data) == VPI_SUCCESS) {
            cv::Mat host_mat;
            vpiImageDataExportOpenCVMat(host_data, &host_mat);
            channel_average = cv::mean(host_mat);
            vpiImageUnlock(frame->get());
        }
    }
    mean_image_dirty = true;

    cv::Size crop_size = get_subwindow_buffer(frame->get(), center_pos, template_size,
                                              static_cast<int>(round(s_z)), z_crop_buffer);
    if (crop_size.width <= 0 || crop_size.height <= 0)
        return; // 模板裁剪失败：保持未初始化，由上层回退

    // Use new infer function
    auto backbone_out_vec = session_backbone->infer({&z_crop_buffer});

    // Move the output buffer into our persistent z_f state
    z_f = std::move(backbone_out_vec[0]);
    m_initialized = true;
}

float NanoTracker::update(const std::shared_ptr<VPIImageWrapper>& frame, cv::Rect2f& output_bbox) {
    if (!m_initialized) {
        return 0.0f; // 未初始化：与 Python 端 (False, None) 对齐
    }
    float s_z = sz(size.x, size.y);
    float scale_z = static_cast<float>(template_size) / s_z;
    float s_x = s_z * (static_cast<float>(instance_size) / template_size);
    
    auto img_size = get_subwindow_buffer(frame->get(), center_pos, instance_size, static_cast<int>(round(s_x)), x_crop_buffer);
    if (img_size.width <= 0 || img_size.height <= 0)
    {
        // 搜索窗裁剪失败：保持内部状态不变（不写入退化的 0x0 边界框），
        // 返回 -1 让上层按"本帧跟踪失败"处理
        return -1.0f;
    }

    // Run backbone inference
    auto x_f_results_vec = session_backbone->infer({&x_crop_buffer});
    TensorBuffer x_f = std::move(x_f_results_vec[0]); // x_f is local to this function

    // Run head inference
    // Inputs are the persistent z_f and the local x_f
    auto head_outputs_vec = session_head->infer({&z_f, &x_f});
    
    // Wrap the output TensorBuffers in cv::Mat headers to use existing logic
    TensorBuffer& out_cls_buf = (head_outputs_vec[0].dims.d[1] == 2) ? head_outputs_vec[0] : head_outputs_vec[1];
    TensorBuffer& out_loc_buf = (head_outputs_vec[0].dims.d[1] == 2) ? head_outputs_vec[1] : head_outputs_vec[0];

    // Create cv::Mat headers pointing to the host_ptr data
    // Dims are (1, 2, 15, 15) for cls, (1, 4, 15, 15) for loc
    int cls_dims_arr[] = {int(out_cls_buf.dims.d[0]), int(out_cls_buf.dims.d[1]), int(out_cls_buf.dims.d[2]), int(out_cls_buf.dims.d[3])};
    cv::Mat cls_logits(4, cls_dims_arr, CV_32F, out_cls_buf.host_ptr);

    int loc_dims_arr[] = {int(out_loc_buf.dims.d[0]), int(out_loc_buf.dims.d[1]), int(out_loc_buf.dims.d[2]), int(out_loc_buf.dims.d[3])};
    cv::Mat loc_reg(4, loc_dims_arr, CV_32F, out_loc_buf.host_ptr);

    
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


bool NanoTracker::update_padded_image(int new_width, int new_height, int flags)
{
    int padded_width = 0;
    int padded_height = 0;
    if (vpi_padded_image) {
        vpiImageGetSize(vpi_padded_image, &padded_width, &padded_height);
    }
    if (padded_width != new_width || padded_height != new_height) {
        if (vpi_padded_image) {
            vpiImageDestroy(vpi_padded_image);
            vpi_padded_image = nullptr;
        }
        padded_width = new_width;
        padded_height = new_height;
        auto status = vpiImageCreate(padded_width, padded_height, VPI_IMAGE_FORMAT_F32, flags, &vpi_padded_image);
        if (status != VPI_SUCCESS) {
            // 显式置空句柄，避免后续在悬空句柄上创建视图（会以
            // VPI_ERROR_INVALID_ARGUMENT 失败并可能破坏 VPI 内部状态）
            vpi_padded_image = nullptr;
            std::cerr << "Failed to create VPI padded image (" << new_width << "x" << new_height
                      << "): " << vpiStatusGetName(status) << std::endl;
            return false;
        }
    }
    return true;
}

void NanoTracker::update_mean_image(int new_width, int new_height, int flags)
{
    // 尺寸变化或均值变化时重建/重填，避免每帧重复写整图
    if (vpi_mean_image && mean_image_w == new_width && mean_image_h == new_height && !mean_image_dirty) {
        return;
    }
    if (vpi_mean_image && (mean_image_w != new_width || mean_image_h != new_height)) {
        vpiImageDestroy(vpi_mean_image);
        vpi_mean_image = nullptr;
    }
    if (!vpi_mean_image) {
        if (vpiImageCreate(new_width, new_height, VPI_IMAGE_FORMAT_U8, flags, &vpi_mean_image) != VPI_SUCCESS) {
            std::cerr << "Failed to create VPI mean image" << std::endl;
            return;
        }
        mean_image_w = new_width;
        mean_image_h = new_height;
    }
    VPIImageData data;
    if (vpiImageLockData(vpi_mean_image, VPI_LOCK_WRITE,
            VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &data) == VPI_SUCCESS) {
        uint8_t value = static_cast<uint8_t>(cv::saturate_cast<uchar>(channel_average[0]));
        uint8_t* row = static_cast<uint8_t*>(data.buffer.pitch.planes[0].data);
        for (int y = 0; y < new_height; ++y)
            memset(row + y * data.buffer.pitch.planes[0].pitchBytes, value, new_width);
        vpiImageUnlock(vpi_mean_image);
        mean_image_dirty = false;
    }
}

/**
 * @brief Prepares a subwindow from the image and loads it into a TensorBuffer.
 */
cv::Size NanoTracker::get_subwindow_buffer(VPIImage vpiIm, const cv::Point2f& pos, int model_sz, int original_sz, TensorBuffer& buffer) {
    float c = (static_cast<float>(original_sz) + 1) / 2.0f;
    float context_xmin = std::floor(pos.x - c + 0.5f);
    float context_ymin = std::floor(pos.y - c + 0.5f);

    int im_width = 0;
    int im_height = 0;
    vpiImageGetSize(vpiIm, &im_width, &im_height);
    cv::Size img_size(im_width, im_height);

    int left_pad = std::max(0, -static_cast<int>(context_xmin));
    int top_pad = std::max(0, -static_cast<int>(context_ymin));
    int right_pad = std::max(0, static_cast<int>(context_xmin + original_sz - im_width));
    int bottom_pad = std::max(0, static_cast<int>(context_ymin + original_sz - im_height));

    // auto start = std::chrono::high_resolution_clock::now();

    const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;
    if (!update_padded_image(im_width + left_pad + right_pad, im_height + top_pad + bottom_pad, flags))
        return {};

    // 搜索窗越界时，先用通道均值填满整张 padded 图，再写入图像内容——
    // 否则边界区域是未初始化显存（Python 端为 copyMakeBorder 均值填充）
    static const VPIConvertImageFormatParams meanFmtParams {
        .policy = VPI_CONVERSION_CLAMP,
        .scale = 1.0f,   // 与下方图像内容转换一致（NanoTrack 输入为 0-255 浮点）
        .offset = 0.0f,
        .flags = 0,
        .chromaUpFilter = VPI_INTERP_NEAREST,
        .chromaDownFilter = VPI_INTERP_NEAREST,
    };
    if (left_pad > 0 || top_pad > 0 || right_pad > 0 || bottom_pad > 0) {
        update_mean_image(im_width + left_pad + right_pad, im_height + top_pad + bottom_pad, flags);
        if (vpi_mean_image) {
            auto fill_status = vpiSubmitConvertImageFormat(vpi_stream, VPI_BACKEND_CUDA,
                vpi_mean_image, vpi_padded_image, &meanFmtParams);
            if (fill_status != VPI_SUCCESS) {
                std::cerr << "Failed to fill padded image with channel mean: " << fill_status << std::endl;
            }
        }
    }

    VPIImage vpiView;
    VPIRectangleI roi_view {
        .x = left_pad,
        .y = top_pad,
        .width = im_width,
        .height = im_height
    };
    auto status = vpiImageCreateView(vpi_padded_image, &roi_view, flags, &vpiView);
    if (status != VPI_SUCCESS) {
        std::cerr << "Failed to create VPI image view: " << vpiStatusGetName(status) << std::endl;
        return {};
    }
    static const VPIConvertImageFormatParams fmtParams {
        .policy = VPI_CONVERSION_CLAMP,
        .scale = 1.0f,
        .offset = 0.0f,
        .flags = 0,
        .chromaUpFilter = VPI_INTERP_NEAREST,
        .chromaDownFilter = VPI_INTERP_NEAREST,
    };
    status = vpiSubmitConvertImageFormat(vpi_stream, VPI_BACKEND_CUDA, vpiIm, vpiView, &fmtParams);
    if (status != VPI_SUCCESS) {
        std::cerr << "Failed to submit VPI format conversion" << std::endl;
        vpiImageDestroy(vpiView);
        return {};
    }

    vpiStreamSync(vpi_stream);
    vpiImageDestroy(vpiView);

    VPIImage vpiImPatch;
    VPIRectangleI roi_patch {
        .x = static_cast<int>(context_xmin + left_pad),
        .y = static_cast<int>(context_ymin + top_pad),
        .width = original_sz,
        .height = original_sz
    };
    status = vpiImageCreateView(vpi_padded_image, &roi_patch, flags, &vpiImPatch);
    if (status != VPI_SUCCESS) {
        std::cerr << "Failed to create VPI image patch: " << vpiStatusGetName(status) << std::endl;
        return {};
    }

    VPIImage vpiOut = vpiImPatch;
    if (model_sz != original_sz) {
        auto it = vpi_model_images.find(model_sz);
        if (it != vpi_model_images.end()) {
            vpiOut = it->second;
        } else {
            status = vpiImageCreate(model_sz, model_sz, VPI_IMAGE_FORMAT_F32, flags, &vpiOut);
            if (status != VPI_SUCCESS) {
                std::cerr << "Failed to create VPI resized image" << std::endl;
                vpiImageDestroy(vpiImPatch);
                return {};
            }
            vpi_model_images[model_sz] = vpiOut;
        }
        status = vpiSubmitRescale(vpi_stream, VPI_BACKEND_CUDA, vpiImPatch, vpiOut, VPI_INTERP_LINEAR, VPI_BORDER_CLAMP, 0);
        if (status != VPI_SUCCESS) {
            std::cerr << "Failed to submit VPI rescale" << std::endl;
            vpiImageDestroy(vpiImPatch);
            return {};
        }
    }

    // printf("Synchronizing VPI stream...\n");
    vpiStreamSync(vpi_stream);
    
    VPIImageData f32Data;
    if (vpiImageLockData(vpiOut, VPI_LOCK_READ, VPI_IMAGE_BUFFER_CUDA_PITCH_LINEAR, &f32Data) != VPI_SUCCESS)
    {
        std::cerr << "Failed to lock model image for memcpy" << std::endl;
        vpiImageDestroy(vpiImPatch);
        return {};
    }
    size_t width_bytes = model_sz * sizeof(float);
    cudaMemcpy2DAsync(
        static_cast<float*>(buffer.device_ptr), width_bytes,
        f32Data.buffer.pitch.planes[0].data, f32Data.buffer.pitch.planes[0].pitchBytes,
        width_bytes, model_sz, cudaMemcpyDeviceToDevice);
    cudaMemcpy2DAsync(
        static_cast<float*>(buffer.device_ptr) + model_sz * model_sz, width_bytes,
        f32Data.buffer.pitch.planes[0].data, f32Data.buffer.pitch.planes[0].pitchBytes,
        width_bytes, model_sz, cudaMemcpyDeviceToDevice);
    cudaMemcpy2DAsync(
        static_cast<float*>(buffer.device_ptr) + model_sz * model_sz * 2, width_bytes,
        f32Data.buffer.pitch.planes[0].data, f32Data.buffer.pitch.planes[0].pitchBytes,
        width_bytes, model_sz, cudaMemcpyDeviceToDevice);
    cudaStreamSynchronize(0);
    vpiImageUnlock(vpiOut);
    vpiImageDestroy(vpiImPatch);

    return img_size;
    
    // auto end = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double, std::milli> elapsed = end - start;

    // std::cout << "Elapsed time: " << elapsed.count() << " ms" << std::endl;

    // cv::Mat im_padded;
    // cv::copyMakeBorder(im, im_padded, top_pad, bottom_pad, left_pad, right_pad, cv::BORDER_CONSTANT, channel_average);
    
    // cv::Rect roi(static_cast<int>(context_xmin + left_pad),
    //              static_cast<int>(context_ymin + top_pad),
    //              original_sz, original_sz);
    
    // cv::Mat im_patch = im_padded(roi);
    
    // if (model_sz != original_sz) {
    //     cv::resize(im_patch, im_patch, cv::Size(model_sz, model_sz));
    // }
    
    // // im_patch is HWC, CV_8UC3
    // // buffer.host_ptr needs to be filled with CHW, CV_32FC1 data
    
    // float* host_data = static_cast<float*>(buffer.host_ptr);
    // size_t C = 3;
    // size_t H = model_sz;
    // size_t W = model_sz;
    // size_t channel_size = H * W;

    // for (size_t c = 0; c < C; ++c) {
    //     for (size_t h = 0; h < H; ++h) {
    //         for (size_t w = 0; w < W; ++w) {
    //             // Manually convert HWC to CHW and cast from 8U to 32F
    //             // (Assumes BGR input and model)
    //             host_data[c * channel_size + h * W + w] = static_cast<float>(im_patch.at<cv::Vec3b>(h, w)[c]);
    //         }
    //     }
    // }
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
