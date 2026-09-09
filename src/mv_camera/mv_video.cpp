#include "mv_video.h"

#include <NvBuffer.h>
#include <NvJpegDecoder.h>

#include <sys/mman.h>

#include <iostream>

#include "frame/vpi_frame.h"

#define MEMORY_ALIGNMENT 256

MVVideo::MVVideo(const std::string &video_path, bool use_video_timestamp)
    : MVCameraBase(30), use_video_ts(use_video_timestamp)
{
    printf("Opening video file: %s\n", video_path.c_str());
    // --- Open the input file ---
    int ret = avformat_open_input(&format_ctx, video_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        // std::cerr << "Could not open input file: " << video_path << std::endl;
        return;
    }

    // --- Find stream information ---
    ret = avformat_find_stream_info(format_ctx, &format_ctx->metadata);
    if (ret < 0) {
        // log_error("Failed to find stream information", ret);
        avformat_close_input(&format_ctx);
        return;
    }

    AVDictionaryEntry *entry = av_dict_get(format_ctx->metadata, "Artist", nullptr, 0);
    if (entry) {
        start_ts = strtoul(entry->value, nullptr, 10);
        // std::cout << "First frame timestamp: " << ts << std::endl;
    }

    // --- Find the video stream ---
    AVStream* video_stream = nullptr;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            video_stream = format_ctx->streams[i];
            break;
        }
    }

    if (video_stream_index == -1) {
        // std::cerr << "Could not find a video stream in the input file." << std::endl;
        avformat_close_input(&format_ctx);
        return;
    }
    
    // Verify that the stream is actually MJPEG
    if (video_stream->codecpar->codec_id != AV_CODEC_ID_MJPEG) {
        // std::cerr << "The video stream is not MJPEG. This tool only extracts frames from MJPEG streams." << std::endl;
        avformat_close_input(&format_ctx);
        return;
    }

    time_base = video_stream->time_base;
    packet = av_packet_alloc();
    valid = true;
    
    // std::cout << "Found MJPEG video stream at index " << video_stream_index << std::endl;
    // std::cout << "Stream time base: " << time_base.num << "/" << time_base.den << std::endl;
    // std::cout << "start_ts: " << start_ts << std::endl;
}

MVVideo::~MVVideo() {
    stop();
    if (packet) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    if (format_ctx) {
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }
    if (uv_ptr_) {
        // Release UV planes memory
        munlock(uv_ptr_, uv_size_);
        free(uv_ptr_);
        uv_ptr_ = nullptr;
    }
}

bool MVVideo::stop() {
    if (is_running_) {
        is_running_.store(false);
        if (decode_thread.joinable()) {
            decode_thread.join();
        }
    }
    return true;
}

bool MVVideo::run(CallbackFn &&callback) {
    if (!valid || is_running_) {
        return false;
    }
    is_running_.store(true);
    if (!use_video_ts) {
        start_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count()/1000;
    }
    decode_worker();
    return true;
}

bool MVVideo::run_async(CallbackFn &&callback) {
    if (!valid || is_running_) {
        return false;
    }
    is_running_.store(true);
    
    decode_thread = std::thread(&MVVideo::decode_worker, this);
    return true;
}

bool MVVideo::get_image_size(int &width, int &height) const {
    if (valid && format_ctx) {
        AVStream *video_stream = format_ctx->streams[video_stream_index];
        width = video_stream->codecpar->width;
        height = video_stream->codecpar->height;
        return true;
    }
    return false;
}

VPIImage MVVideo::decode(const char* data, size_t size) {
    static unsigned char *y[4 * DCTSIZE] = { NULL, };
    static unsigned char *u[4 * DCTSIZE] = { NULL, };
    static unsigned char *v[4 * DCTSIZE] = { NULL, };
    static unsigned char **line[3] = { y, u, v };

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    
    jpeg_mem_src(&cinfo, (unsigned char*)data, size);
    jpeg_read_header(&cinfo, TRUE);
    
    cinfo.out_color_space = JCS_YCbCr;
    cinfo.raw_data_out = TRUE;
    cinfo.do_fancy_upsampling = FALSE;
    
    jpeg_start_decompress(&cinfo);
    
    const int width = cinfo.output_width;
    const int height = cinfo.output_height;
    
    VPIImage vpiOut = nullptr;
    const int flags = VPI_BACKEND_CPU | VPI_BACKEND_CUDA;
    if (vpiImageCreate(width, height, VPI_IMAGE_FORMAT_U8, flags, &vpiOut) != VPI_SUCCESS) {
        jpeg_destroy_decompress(&cinfo);
        return nullptr;
    }
    
    VPIImageData outData;
    if (vpiImageLockData(vpiOut, VPI_LOCK_WRITE, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &outData) != VPI_SUCCESS) {
        vpiImageDestroy(vpiOut);
        jpeg_destroy_decompress(&cinfo);
        return nullptr;
    }

    int current_uv_size = outData.buffer.pitch.planes[0].pitchBytes * outData.buffer.pitch.planes[0].height / 2;
    if (uv_ptr_ == nullptr || uv_size_ != current_uv_size)
    {
        if (uv_ptr_) {
            // Release UV planes memory
            munlock(uv_ptr_, uv_size_);
            free(uv_ptr_);
            uv_ptr_ = nullptr;
        }
        uv_size_ = current_uv_size;
        auto res = posix_memalign(&uv_ptr_, MEMORY_ALIGNMENT, uv_size_);
        // Try to keep pages resident
        mlock(uv_ptr_, uv_size_);
        // memset(uv_ptr_, 0x80, uv_size_); // set UV to mid-range
    }

    auto nv_buffer = create_nv_buffer(
        reinterpret_cast<uint8_t*>(outData.buffer.pitch.planes[0].data), reinterpret_cast<uint8_t*>(uv_ptr_),
        outData.buffer.pitch.planes[0].width, outData.buffer.pitch.planes[0].height, 
        outData.buffer.pitch.planes[0].pitchBytes);
    
    int v_samp[3];
    unsigned char *base[3];
    int stride[3];
    for(int i=0; i<3; ++i) {
        v_samp[i] = cinfo.comp_info[i].v_samp_factor;
        base[i] = nv_buffer->planes[i].data;
        stride[i] = nv_buffer->planes[i].fmt.stride;
    }

    for (int i = 0; i < height; i += v_samp[0] * DCTSIZE) {
        for (int j = 0; j < (v_samp[0] * DCTSIZE); ++j) {
            line[0][j] = base[0] + (i + j) * stride[0];
            if (j % 2 == 0) {
                line[1][j/2] = base[1] + ((i + j) / 2) * stride[1];
                line[2][j/2] = base[2] + ((i + j) / 2) * stride[2];
            }
        }
        jpeg_read_raw_data(&cinfo, line, v_samp[0] * DCTSIZE);
    }
    
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    
    vpiImageUnlock(vpiOut);
    
    return vpiOut;
}

void MVVideo::decode_worker() {
    auto now = std::chrono::high_resolution_clock::now();
    int64_t base_ts = use_video_ts ? start_ts : now.time_since_epoch().count()/1000;
    int frame_count = 0;
    int64_t last_timestamp_us = 0;
    while (is_running_ && av_read_frame(format_ctx, packet) >= 0) {
        // Check if the packet belongs to the video stream
        if (packet->stream_index == video_stream_index) {
            frame_count++;

            // Calculate the presentation timestamp (PTS) in microseconds
            int64_t timestamp_us = av_rescale_q(packet->pts, time_base, {1, 1000000});
            int64_t ts_diff = timestamp_us - last_timestamp_us;
            last_timestamp_us = timestamp_us;
            printf("Decoding frame %d , diff %lu us\n", frame_count, ts_diff);
            VPIImage vpiOut = decode(reinterpret_cast<const char*>(packet->data), packet->size);

            // 帧时间戳必须是 PTS 的累计值；用 base_ts + ts_diff（帧间增量）会让
            // 第一帧之后的所有帧都拿到几乎相同的时钟，破坏单调性。
            FrameRef image_buffer = VpiFrame::takeOwnership(vpiOut, base_ts + timestamp_us);
            if (!image_buffer) {
                std::cerr << "Failed to create frame from decoded VPI image" << std::endl;
                av_packet_unref(packet);
                continue;
            }

            now += std::chrono::microseconds(ts_diff);
            std::this_thread::sleep_until(now);
            // 队列满时丢弃【最新】帧。绝不能在这里 pop() 腾位：pop 是 SPSC 队列的
            // 消费端操作，与消费者线程并发取同一槽位会导致 shared_ptr 双重释放
            // （释放后使用 -> 堆损坏）。消费端会排空到最新帧，队列不会长期满。
            if (!image_queue_.try_enqueue(std::move(image_buffer))) {
                static int drop_count = 0;
                if (++drop_count % 100 == 1)
                    printf("Frame queue full, dropped newest frame (%d)\n", drop_count);
            }
        }
    }
}
