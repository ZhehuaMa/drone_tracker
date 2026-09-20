#include "mjpeg_video_reader.h"
#include "timing_observer.h"

#include <array>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

extern "C" {
#include <jpeglib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace rk3576 {
namespace {
struct JpegState {
    jpeg_decompress_struct decoder{};
    jpeg_error_mgr error{};
    std::jmp_buf jump{};
    std::array<char, JMSG_LENGTH_MAX> message{};
    bool created{};
    ~JpegState() {
        if (created) {
            jpeg_destroy_decompress(&decoder);
        }
    }
};
void jpegFailure(j_common_ptr common) {
    auto *state{static_cast<JpegState *>(common->client_data)};
    common->err->format_message(common, state->message.data());
    std::longjmp(state->jump, 1);
}
void jpegMessage(j_common_ptr common, int level) {
    // 截断等警告也作为坏帧处理，禁止 libjpeg 静默补齐后交付。
    if (level < 0) {
        jpegFailure(common);
    }
}
// 跳转边界内只有平凡局部对象；所有拥有资源的对象均由外层持有。
bool decodeJpeg(JpegState *state, const unsigned char *data, unsigned long size, cv::Mat &output) {
    auto *decoder{&state->decoder};
    decoder->err = jpeg_std_error(&state->error);
    state->error.error_exit = jpegFailure;
    state->error.emit_message = jpegMessage;
    decoder->client_data = state;
    if (setjmp(state->jump) != 0) {
        return false;
    }
    state->created = true;
    jpeg_create_decompress(decoder);
    jpeg_mem_src(decoder, data, size);
    jpeg_read_header(decoder, TRUE);
    bool supported{decoder->data_precision == 8};
    if (decoder->jpeg_color_space == JCS_GRAYSCALE) {
        supported = supported && decoder->num_components == 1
            && decoder->comp_info[0].h_samp_factor == 1 && decoder->comp_info[0].v_samp_factor == 1;
    } else if (decoder->jpeg_color_space == JCS_YCbCr && decoder->num_components == 3) {
        supported = supported && decoder->comp_info[1].h_samp_factor == 1
            && decoder->comp_info[1].v_samp_factor == 1 && decoder->comp_info[2].h_samp_factor == 1
            && decoder->comp_info[2].v_samp_factor == 1
            && ((decoder->comp_info[0].h_samp_factor == 1 && decoder->comp_info[0].v_samp_factor == 1)
                || (decoder->comp_info[0].h_samp_factor == 2
                    && (decoder->comp_info[0].v_samp_factor == 1 || decoder->comp_info[0].v_samp_factor == 2)));
    } else {
        supported = false;
    }
    if (!supported) {
        std::snprintf(state->message.data(), state->message.size(), "Unsupported JPEG color, depth or sampling");
        return false;
    }
    decoder->raw_data_out = TRUE;
    decoder->do_fancy_upsampling = FALSE;
    decoder->dct_method = JDCT_ISLOW;
    jpeg_start_decompress(decoder);
    output.create(static_cast<int>(decoder->output_height), static_cast<int>(decoder->output_width), CV_8UC1);
    JSAMPARRAY planes[3]{};
    for (int i{0}; i < decoder->num_components; ++i) {
        planes[i] = decoder->mem->alloc_sarray(reinterpret_cast<j_common_ptr>(decoder), JPOOL_IMAGE,
            decoder->comp_info[i].width_in_blocks * DCTSIZE, decoder->comp_info[i].v_samp_factor * DCTSIZE);
    }
    while (decoder->output_scanline < decoder->output_height) {
        const JDIMENSION first{decoder->output_scanline};
        const JDIMENSION rows{jpeg_read_raw_data(decoder, planes, decoder->max_v_samp_factor * DCTSIZE)};
        if (rows == 0) {
            std::snprintf(state->message.data(), state->message.size(), "Suspended JPEG decode");
            return false;
        }
        for (JDIMENSION row{0}; row < rows && first + row < decoder->output_height; ++row) {
            std::memcpy(output.ptr(static_cast<int>(first + row)), planes[0][row], decoder->output_width);
        }
    }
    jpeg_finish_decompress(decoder);
    return true;
}
std::string avError(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return buffer.data();
}
} // namespace
struct MjpegVideoReader::Impl {
    AVFormatContext *format{};
    AVPacket *packet{};
    int stream{-1};
    int width{};
    int height{};
    const std::atomic<bool> &stop;
    std::vector<std::pair<std::string, std::string>> tags{};
    VideoReadResult first{};
    bool pending{};
    explicit Impl(const std::atomic<bool> &flag) : stop{flag} {}
    ~Impl() {
        av_packet_free(&packet);
        avformat_close_input(&format);
    }
    VideoReadResult read() {
        while (true) {
            av_packet_unref(packet);
            if (stop.load()) {
                return {VideoReadStatus::ReadError, {}, "Read interrupted by stop"};
            }
            const int code{av_read_frame(format, packet)};
            if (code < 0) {
                av_packet_unref(packet);
                return {code == AVERROR_EOF ? VideoReadStatus::Eof : VideoReadStatus::ReadError, {}, avError(code)};
            }
            if (packet->stream_index != stream) {
                continue;
            }
            VideoReadResult result{VideoReadStatus::Frame, {{}, packet->pts, packet->pts != AV_NOPTS_VALUE}, {}};
            try {
                timing::Span decode{timing::Stage::Decode};
                auto jpeg{std::make_unique<JpegState>()};
                if (!decodeJpeg(jpeg.get(), packet->data, static_cast<unsigned long>(packet->size), result.sample.gray)) {
                    decode.finish(false);
                    result.status = VideoReadStatus::DecodeError;
                    result.reason = jpeg->message.data();
                } else if (width != 0 && (width != result.sample.gray.cols || height != result.sample.gray.rows)) {
                    result.status = VideoReadStatus::SizeChanged;
                    result.reason = "Decoded video dimensions changed";
                }
            } catch (...) {
                av_packet_unref(packet);
                throw;
            }
            av_packet_unref(packet);
            return result;
        }
    }
};
MjpegVideoReader::MjpegVideoReader(const std::string &path, const std::atomic<bool> &stop)
    : impl_{std::make_unique<Impl>(stop)} {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Input must be a local regular file");
    }
    impl_->format = avformat_alloc_context();
    if (!impl_->format) {
        throw std::bad_alloc{};
    }
    impl_->format->interrupt_callback = {[](void *opaque) {
        return static_cast<Impl *>(opaque)->stop.load() ? 1 : 0;
    }, impl_.get()};
    // 显式限定协议，容器不得隐式打开网络资源。
    AVDictionary *options{};
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const int opened{avformat_open_input(&impl_->format, path.c_str(), nullptr, &options)};
    av_dict_free(&options);
    if (opened < 0) {
        throw std::runtime_error("Open: " + avError(opened));
    }
    const int info{avformat_find_stream_info(impl_->format, nullptr)};
    if (info < 0) {
        throw std::runtime_error("Stream info: " + avError(info));
    }
    for (unsigned int i{0}; i < impl_->format->nb_streams; ++i) {
        if (impl_->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            impl_->stream = static_cast<int>(i);
            break;
        }
    }
    if (impl_->stream < 0 || impl_->format->streams[impl_->stream]->codecpar->codec_id != AV_CODEC_ID_MJPEG) {
        throw std::runtime_error("First video stream must be MJPEG");
    }
    AVDictionaryEntry *tag{};
    while ((tag = av_dict_get(impl_->format->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        impl_->tags.emplace_back(tag->key, tag->value);
    }
    impl_->packet = av_packet_alloc();
    if (!impl_->packet) {
        throw std::bad_alloc{};
    }
    impl_->first = impl_->read();
    if (impl_->first.status != VideoReadStatus::Frame) {
        throw std::runtime_error("First frame: " + impl_->first.reason);
    }
    impl_->width = impl_->first.sample.gray.cols;
    impl_->height = impl_->first.sample.gray.rows;
    impl_->pending = true;
}
MjpegVideoReader::~MjpegVideoReader() = default;
VideoReadResult MjpegVideoReader::next() {
    if (impl_->pending) {
        impl_->pending = false;
        return std::move(impl_->first);
    }
    return impl_->read();
}
std::pair<int, int> MjpegVideoReader::timeBase() const {
    const auto base{impl_->format->streams[impl_->stream]->time_base};
    return {base.num, base.den};
}
const std::vector<std::pair<std::string, std::string>> &MjpegVideoReader::metadata() const {
    return impl_->tags;
}
} // namespace rk3576
