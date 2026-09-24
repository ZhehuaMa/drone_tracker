#include "encoder.h"

#include <linux/videodev2.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_mpi.h>
#include <rk_venc_cfg.h>
#include <rk_venc_cmd.h>

namespace rk3576::recording {
namespace {
void check(MPP_RET result, const char *operation) {
    if (result != MPP_OK) { throw std::runtime_error(std::string{operation} + ": MPP " + std::to_string(result)); }
}
} // namespace
struct Encoder::Impl {
    CameraFormat format{};
    int stride{}, height{};
    MppCtx context{};
    MppApi *api{};
    MppEncCfg config{};
    MppBufferGroup group{};
    MppBuffer buffer{};
    MppFrame frame{};
    MppPacket packet{};
    ~Impl() {
        if (context) { mpp_destroy(context); }
        if (packet) { mpp_packet_deinit(&packet); }
        if (frame) { mpp_frame_deinit(&frame); }
        if (buffer) { mpp_buffer_put(buffer); }
        if (group) { mpp_buffer_group_put(group); }
        if (config) { mpp_enc_cfg_deinit(config); }
    }
    void set(const char *key, int value) { check(mpp_enc_cfg_set_s32(config, key, value), key); }
    void expect(const char *key, int value) {
        RK_S32 actual{};
        check(mpp_enc_cfg_get_s32(config, key, &actual), key);
        if (actual != value) { throw std::runtime_error(std::string{"MPP altered required parameter: "} + key); }
    }
};
Encoder::Encoder(CameraFormat format, int fps, std::int64_t bitrate) : impl_{std::make_unique<Impl>()} {
    if (!format.width || !format.height || format.width % 2 || format.height % 2 || fps <= 0
        || bitrate <= 0 || bitrate > INT32_MAX / 2) { throw std::invalid_argument("Invalid H264 input configuration"); }
    // 首个经过实机验证的色彩契约；不对未知矩阵或范围做隐式转换。
    if (format.quantization != V4L2_QUANTIZATION_FULL_RANGE || format.colorspace != V4L2_COLORSPACE_DEFAULT
        || format.ycbcr_encoding != 0 || format.transfer_function != 0) {
        throw std::runtime_error("Unsupported recording color metadata; full-range/default matrix required");
    }
    auto &e{*impl_};
    e.format = format;
    e.stride = (format.width + 15) / 16 * 16;
    e.height = (format.height + 15) / 16 * 16;
    check(mpp_create(&e.context, &e.api), "create");
    RK_S64 timeout{1000};
    check(e.api->control(e.context, MPP_SET_OUTPUT_TIMEOUT, &timeout), "output timeout");
    check(mpp_init(e.context, MPP_CTX_ENC, MPP_VIDEO_CodingAVC), "H264 init");
    check(mpp_enc_cfg_init(&e.config), "config init");
    check(e.api->control(e.context, MPP_ENC_GET_CFG, e.config), "get config");
    e.set("prep:width", format.width); e.set("prep:height", format.height);
    e.set("prep:hor_stride", e.stride); e.set("prep:ver_stride", e.height);
    e.set("prep:format", MPP_FMT_YUV420SP); e.set("prep:range", MPP_FRAME_RANGE_JPEG);
    e.set("prep:colorspace", MPP_FRAME_SPC_UNSPECIFIED); e.set("prep:colorprim", MPP_FRAME_PRI_UNSPECIFIED);
    e.set("prep:colortrc", MPP_FRAME_TRC_UNSPECIFIED);
    e.set("rc:mode", MPP_ENC_RC_MODE_CBR); e.set("rc:fps_in_flex", 0); e.set("rc:fps_out_flex", 0);
    e.set("rc:fps_in_num", fps); e.set("rc:fps_in_denorm", 1);
    e.set("rc:fps_out_num", fps); e.set("rc:fps_out_denorm", 1);
    e.set("rc:bps_target", bitrate); e.set("rc:bps_max", bitrate + bitrate / 16);
    e.set("rc:bps_min", bitrate - bitrate / 16);
    // 按采集时间每秒请求；较长的计数 GOP 避免 60.019 fps 下自动与显式 IDR 相邻重复。
    e.set("rc:gop", fps * 2); e.set("rc:drop_mode", 0);
    e.set("h264:profile", 66); e.set("h264:level", 50);
    e.set("h264:cabac_en", 0); e.set("h264:cabac_idc", -1);
    check(e.api->control(e.context, MPP_ENC_SET_CFG, e.config), "apply config");
    check(e.api->control(e.context, MPP_ENC_GET_CFG, e.config), "read config");
    e.expect("prep:width", format.width); e.expect("prep:height", format.height);
    e.expect("prep:hor_stride", e.stride); e.expect("prep:ver_stride", e.height);
    e.expect("prep:range", MPP_FRAME_RANGE_JPEG); e.expect("prep:format", MPP_FMT_YUV420SP);
    e.expect("rc:fps_in_num", fps); e.expect("rc:fps_out_num", fps); e.expect("rc:drop_mode", 0);
    e.expect("rc:bps_target", bitrate); e.expect("rc:gop", fps * 2);
    e.expect("h264:profile", 66); e.expect("h264:level", 50);
    MppEncHeaderMode headers{MPP_ENC_HEADER_MODE_EACH_IDR};
    check(e.api->control(e.context, MPP_ENC_SET_HEADER_MODE, &headers), "repeat headers");
    check(mpp_buffer_group_get_internal(&e.group, MPP_BUFFER_TYPE_DRM), "input group");
    check(mpp_buffer_group_limit_config(e.group, static_cast<std::size_t>(e.stride) * e.height * 3 / 2, 1),
          "input group bound");
    check(mpp_buffer_get(e.group, &e.buffer, static_cast<std::size_t>(e.stride) * e.height * 3 / 2), "input buffer");
    check(mpp_buffer_sync_begin(e.buffer), "initialize input begin");
    std::memset(mpp_buffer_get_ptr(e.buffer), 0, mpp_buffer_get_size(e.buffer));
    check(mpp_buffer_sync_end(e.buffer), "initialize input end");
}
Encoder::~Encoder() = default;
EncodedFrame Encoder::encode(const std::uint8_t *pixels, const RecordingSlots::Identity &identity,
                             std::int64_t pts_us, bool request_idr, const std::function<void()> &copied) {
    auto &e{*impl_};
    if (identity.format.width != e.format.width || identity.format.height != e.format.height
        || identity.format.quantization != e.format.quantization || identity.format.colorspace != e.format.colorspace
        || identity.format.ycbcr_encoding != e.format.ycbcr_encoding
        || identity.format.transfer_function != e.format.transfer_function) {
        throw std::runtime_error("Recording input format changed within session");
    }
    check(mpp_buffer_sync_begin(e.buffer), "input CPU begin");
    auto *destination{static_cast<std::uint8_t *>(mpp_buffer_get_ptr(e.buffer))};
    for (std::uint32_t row{}; row < e.format.height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * e.stride,
                    pixels + static_cast<std::size_t>(row) * e.format.width, e.format.width);
    }
    for (std::uint32_t row{}; row < e.format.height / 2; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(e.height + row) * e.stride,
                    pixels + static_cast<std::size_t>(e.format.height + row) * e.format.width, e.format.width);
    }
    check(mpp_buffer_sync_end(e.buffer), "input CPU end");
    copied();
    check(mpp_frame_init(&e.frame), "frame init");
    mpp_frame_set_width(e.frame, e.format.width); mpp_frame_set_height(e.frame, e.format.height);
    mpp_frame_set_hor_stride(e.frame, e.stride); mpp_frame_set_ver_stride(e.frame, e.height);
    mpp_frame_set_fmt(e.frame, MPP_FMT_YUV420SP); mpp_frame_set_pts(e.frame, pts_us);
    mpp_frame_set_buffer(e.frame, e.buffer);
    if (request_idr) { check(e.api->control(e.context, MPP_ENC_SET_IDR_FRAME, nullptr), "request IDR"); }
    check(e.api->encode_put_frame(e.context, e.frame), "submit input");
    check(e.api->encode_get_packet(e.context, &e.packet), "receive output");
    if (!e.packet || mpp_packet_is_partition(e.packet) || mpp_packet_get_pts(e.packet) != pts_us) {
        throw std::runtime_error("MPP output missing, partitioned or PTS mismatch");
    }
    const auto size{mpp_packet_get_length(e.packet)};
    const auto reserved_output{static_cast<std::size_t>(e.stride) * e.height};
    if (!size || size > reserved_output || size > 8 * 1024 * 1024) {
        throw std::runtime_error("MPP packet exceeds bounded output reservation");
    }
    const auto *data{static_cast<const std::uint8_t *>(mpp_packet_get_pos(e.packet))};
    EncodedFrame out{identity, pts_us, false, std::vector<std::uint8_t>{data, data + size}};
    for (std::size_t i{}; i + 4 < size; ++i) {
        if (out.bytes[i] == 0 && out.bytes[i + 1] == 0 && out.bytes[i + 2] == 1
            && (out.bytes[i + 3] & 31) == 5) { out.idr = true; break; }
    }
    if (request_idr && !out.idr) { throw std::runtime_error("MPP requested IDR missing"); }
    check(mpp_packet_deinit(&e.packet), "release output");
    check(mpp_frame_deinit(&e.frame), "release frame");
    out.encoded_monotonic_us = RecordingSlots::monotonicUs();
    return out;
}
} // namespace rk3576::recording
