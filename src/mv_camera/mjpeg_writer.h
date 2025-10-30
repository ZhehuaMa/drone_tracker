#pragma once

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

#include <iostream>
#include <vector>
#include <string>
#include <chrono>

class MJPEGWriter
{
public:
    MJPEGWriter() = default;
    ~MJPEGWriter(); 

    bool setup(const std::string& filename, int width, int height, int fps);
    bool writeFrame(const uint8_t* data, size_t size, uint64_t timestamp_us);
private:
    AVFormatContext* format_ctx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    int frame_count_ = 0;
    uint64_t first_timestamp_ = 0;

    void log_error(const std::string& msg, int errnum);
};
