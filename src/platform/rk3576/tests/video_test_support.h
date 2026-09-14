#ifndef RK3576_VIDEO_TEST_SUPPORT_H
#define RK3576_VIDEO_TEST_SUPPORT_H

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
extern "C" {
#include <jpeglib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace video_test {
inline void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template <typename Work> void rejects(Work work, const std::string &message) {
    bool rejected{};
    try {
        work();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, message);
}
inline std::vector<unsigned char> bytes(const std::string &path) {
    std::ifstream input{path, std::ios::binary};
    require(static_cast<bool>(input), "Cannot read fixture " + path);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
/** @brief 可再生 JPEG，输入图案含高频和边缘，以验证 IDCT 与有效像素裁剪。 */
inline std::vector<unsigned char> jpeg(const std::string &path, int width, int height, int sampling, int seed) {
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file{std::fopen(path.c_str(), "wb"), &std::fclose};
    require(static_cast<bool>(file), "Cannot write JPEG fixture");
    jpeg_compress_struct encoder{};
    jpeg_error_mgr error{};
    encoder.err = jpeg_std_error(&error);
    jpeg_create_compress(&encoder);
    jpeg_stdio_dest(&encoder, file.get());
    encoder.image_width = width;
    encoder.image_height = height;
    encoder.input_components = sampling == 0 ? 1 : 3;
    encoder.in_color_space = sampling == 0 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&encoder);
    if (sampling != 0) {
        encoder.comp_info[0].h_samp_factor = sampling == 444 ? 1 : 2;
        encoder.comp_info[0].v_samp_factor = sampling == 420 ? 2 : 1;
    }
    encoder.dct_method = JDCT_ISLOW;
    jpeg_set_quality(&encoder, 91, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * encoder.input_components);
    while (encoder.next_scanline < encoder.image_height) {
        for (std::size_t x{0}; x < row.size(); ++x) {
            row[x] = static_cast<unsigned char>((x * 19 + encoder.next_scanline * 13 + seed * 47) % 256);
        }
        JSAMPROW pointer{row.data()};
        jpeg_write_scanlines(&encoder, &pointer, 1);
    }
    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);
    file.reset();
    return bytes(path);
}
/** @brief 独立 scanline 灰度参考；与生产 raw MCU 路径使用相同整数 IDCT。 */
inline cv::Mat reference(const std::vector<unsigned char> &data) {
    jpeg_decompress_struct decoder{};
    jpeg_error_mgr error{};
    decoder.err = jpeg_std_error(&error);
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, data.data(), data.size());
    jpeg_read_header(&decoder, TRUE);
    decoder.out_color_space = JCS_GRAYSCALE;
    decoder.dct_method = JDCT_ISLOW;
    decoder.do_fancy_upsampling = FALSE;
    jpeg_start_decompress(&decoder);
    cv::Mat image{static_cast<int>(decoder.output_height), static_cast<int>(decoder.output_width),
                  CV_8UC1, cv::Scalar{0}};
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row{image.ptr(static_cast<int>(decoder.output_scanline))};
        jpeg_read_scanlines(&decoder, &row, 1);
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return image;
}
/** @brief 直接封装压缩帧；不依赖 ffmpeg 命令行，也不重新编码像素。 */
inline void movie(const std::string &path, const std::vector<std::vector<unsigned char>> &frames,
                  int width = 641, int height = 479, bool audio = false,
                  std::int64_t offset = 0, std::int64_t interval = 33333,
                  const std::string &artist = "1700000000000000", AVCodecID codec = AV_CODEC_ID_MJPEG) {
    AVFormatContext *format{};
    require(avformat_alloc_output_context2(&format, nullptr, "mov", path.c_str()) >= 0 && format, "MOV allocation");
    auto cleanup{[&] {
        avio_closep(&format->pb);
        avformat_free_context(format);
    }};
    try {
        AVStream *video{avformat_new_stream(format, nullptr)};
        require(video != nullptr, "MOV video stream");
        video->time_base = {1, 1000000};
        video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        video->codecpar->codec_id = codec;
        video->codecpar->width = width;
        video->codecpar->height = height;
        AVStream *sound{};
        if (audio) {
            sound = avformat_new_stream(format, nullptr);
            require(sound != nullptr, "MOV audio stream");
            sound->time_base = {1, 8000};
            sound->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            sound->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
            sound->codecpar->sample_rate = 8000;
            sound->codecpar->channels = 1;
            sound->codecpar->bits_per_coded_sample = 16;
        }
        av_dict_set(&format->metadata, "Artist", artist.c_str(), 0);
        require(avio_open(&format->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0, "MOV open");
        require(avformat_write_header(format, nullptr) >= 0, "MOV header");
        for (std::size_t i{0}; i < frames.size(); ++i) {
            AVPacket packet{};
            packet.data = const_cast<unsigned char *>(frames[i].data());
            packet.size = static_cast<int>(frames[i].size());
            packet.stream_index = video->index;
            packet.pts = av_rescale_q(offset + static_cast<std::int64_t>(i) * interval,
                                     AVRational{1, 1000000}, video->time_base);
            packet.dts = packet.pts;
            packet.duration = av_rescale_q(interval, AVRational{1, 1000000}, video->time_base);
            packet.flags = AV_PKT_FLAG_KEY;
            require(av_interleaved_write_frame(format, &packet) >= 0, "MOV packet");
            if (sound) {
                std::vector<unsigned char> silence(534, 0);
                AVPacket pcm{};
                pcm.data = silence.data();
                pcm.size = static_cast<int>(silence.size());
                pcm.stream_index = sound->index;
                pcm.pts = static_cast<std::int64_t>(i) * 267;
                pcm.dts = pcm.pts;
                pcm.duration = 267;
                require(av_interleaved_write_frame(format, &pcm) >= 0, "MOV audio packet");
            }
        }
        require(av_write_trailer(format) >= 0, "MOV trailer");
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}
} // namespace video_test
#endif // RK3576_VIDEO_TEST_SUPPORT_H
