#include "mjpeg_writer.h"


MJPEGWriter::~MJPEGWriter() {
    if (format_ctx_) {
        if (first_timestamp_ != 0) {
            // Write the stream trailer
            av_write_trailer(format_ctx_);
            std::cout << "Finalizing output file." << std::endl;
        }

        // Cleanup
        avcodec_close(codec_ctx_);
        avcodec_free_context(&codec_ctx_);
        if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx_->pb);
        }
        avformat_free_context(format_ctx_);

        format_ctx_ = nullptr;
        stream_ = nullptr;
        codec_ctx_ = nullptr;
    }
}

bool MJPEGWriter::setup(const std::string& filename, int width, int height, int fps) 
{
    // Allocate the output format context
    int ret = avformat_alloc_output_context2(&format_ctx_, nullptr, "mov", filename.c_str());
    if (ret < 0) {
        log_error("Could not allocate output context", ret);
        return false;
    }

    // Find the MJPEG encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        std::cerr << "MJPEG encoder not found" << std::endl;
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
        return false;
    }

    // Create a new video stream
    stream_ = avformat_new_stream(format_ctx_, codec);
    if (!stream_) {
        std::cerr << "Could not create new stream" << std::endl;
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
        return false;
    }

    // --- Set stream parameters using the modern API ---
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "Could not allocate video codec context" << std::endl;
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
        return false;
    }

    // Set stream parameters on the codec context
    stream_->id = format_ctx_->nb_streams - 1;
    codec_ctx_->codec_id = AV_CODEC_ID_MJPEG;
    codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;

    // MJPEG doesn't have a fixed resolution, but some containers require it.
    // We will set it from the first image later, but provide placeholders.
    codec_ctx_->width = width;
    codec_ctx_->height = height;
    codec_ctx_->framerate = {fps, 1}; // Default to 30 FPS; can be adjusted later
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUVJ420P; // Common pixel format for JPEG

    // Set the time base. This is crucial for controlling timestamps.
    // A high-resolution time base (like 90kHz) is good for precision.
    stream_->time_base = {1, 90000};
    codec_ctx_->time_base = stream_->time_base;
    
    // Some formats want stream headers to be separate.
    if (format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Copy codec parameters from the context to the stream.
    ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx_);
    if (ret < 0) {
        log_error("Could not copy codec parameters to stream", ret);
        avcodec_close(codec_ctx_);
        avcodec_free_context(&codec_ctx_);
        avformat_free_context(format_ctx_);
        format_ctx_ = nullptr;
        return false;
    }

    // Open the output file for writing
    if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&format_ctx_->pb, filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            log_error("Could not open output file", ret);
            avcodec_close(codec_ctx_);
            avcodec_free_context(&codec_ctx_);
            avformat_free_context(format_ctx_);
            format_ctx_ = nullptr;
            return false;
        }
    }

    return true;
}

bool MJPEGWriter::writeFrame(const uint8_t* data, size_t size, uint64_t timestamp_us) 
{
    if (!format_ctx_) {
        std::cerr << "Format context or codec context not initialized" << std::endl;
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);

    // Calculate the presentation timestamp (PTS)
    if (first_timestamp_ == 0) {
        // Write the stream header to the output file
        av_dict_set_int(&format_ctx_->metadata, "Artist", timestamp_us, AV_DICT_APPEND);
        auto ret = avformat_write_header(format_ctx_, nullptr);
        if (ret < 0) {
            log_error("Error occurred when opening output file", ret);
            return false;
        }
        first_timestamp_ = timestamp_us;
    }
    uint64_t elapsed_us = timestamp_us - first_timestamp_;
    pkt->pts = av_rescale_q(elapsed_us, {1, 1000000}, stream_->time_base);
    pkt->dts = pkt->pts;
    pkt->duration = 0; // Duration of a single frame
    pkt->flags |= AV_PKT_FLAG_KEY; // Each JPEG is a keyframe

    // Write the packet to the output file
    int ret = av_interleaved_write_frame(format_ctx_, pkt);
    av_packet_free(&pkt);
    if (ret < 0) {
        log_error("Error writing frame", ret);
        return false;
    }

    frame_count_++;
    return true;
}

void MJPEGWriter::log_error(const std::string& msg, int errnum) {
    char errbuf[128];
    av_strerror(errnum, errbuf, sizeof(errbuf));
    std::cerr << msg << ": " << errbuf << std::endl;
}


