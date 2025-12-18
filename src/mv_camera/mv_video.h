#pragma once

#include "mv_camera_base.h"

#include <thread>
#include <atomic>

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
}

class MVVideo : public MVCameraBase {
public:
    MVVideo(const std::string &video_path, bool use_video_timestamp=false);
    virtual ~MVVideo();
    virtual bool run(CallbackFn &&callback=nullptr) override;
    virtual bool run_async(CallbackFn &&callback=nullptr) override;
    virtual bool stop() override;
    virtual bool is_running() const override { return is_running_.load(); }
private:
    VPIImage decode(const char* data, size_t size);
    void decode_worker();

    AVFormatContext* format_ctx = nullptr;
    AVPacket *packet = nullptr;
    uint64_t start_ts = 0;
    AVRational time_base;
    int video_stream_index = -1;
    bool valid = false;
    bool use_video_ts = false;
    std::atomic<bool> is_running_{false};
    std::thread decode_thread;

    void *uv_ptr_ = nullptr;
    int uv_size_ = 0;
};