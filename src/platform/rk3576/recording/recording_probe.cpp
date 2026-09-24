#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <linux/videodev2.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <exception>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "../sc233hgs_camera_device.h"
#include "nv12_copy.h"
#include "shared_slots.h"

#include <rk_mpi.h>
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_venc_cfg.h>
#include <rk_venc_cmd.h>

namespace {
using Clock = std::chrono::steady_clock;
/** @brief 预留有界样本，结束后统计；测量路径不逐帧打印或扩容。 */
class StageTiming {
public:
    explicit StageTiming(std::size_t capacity) { samples_.reserve(capacity); }
    void add(Clock::time_point begin) {
        samples_.push_back(std::chrono::duration<double, std::micro>(Clock::now() - begin).count());
    }
    void report(const char *name) {
        if (samples_.empty()) { return; }
        std::sort(samples_.begin(), samples_.end());
        double total{};
        for (const double value : samples_) { total += value; }
        std::cout << "stage=" << name << " samples=" << samples_.size()
                  << " mean_us=" << total / samples_.size()
                  << " p99_us=" << samples_[(samples_.size() - 1) * 99 / 100]
                  << " max_us=" << samples_.back() << '\n';
    }
private:
    std::vector<double> samples_{};
};
void check(MPP_RET result, const char *operation) {
    if (result != MPP_OK) {
        throw std::runtime_error(std::string{operation} + ": MPP error " + std::to_string(result));
    }
}

/** @brief 独立编码实验拥有全部硬件资源；先销毁上下文，再释放输入缓冲。 */
struct Encoder {
    MppCtx context{};
    MppApi *api{};
    MppEncCfg config{};
    MppBufferGroup group{};
    MppBuffer buffer{};
    MppFrame frame{};
    MppPacket packet{};
    ~Encoder() noexcept {
        if (context) { mpp_destroy(context); }
        if (packet) { mpp_packet_deinit(&packet); }
        if (frame) { mpp_frame_deinit(&frame); }
        if (buffer) { mpp_buffer_put(buffer); }
        if (group) { mpp_buffer_group_put(group); }
        if (config) { mpp_enc_cfg_deinit(config); }
    }
    void set(const char *key, int value) {
        check(mpp_enc_cfg_set_s32(config, key, value), key);
    }
};
struct File {
    int fd{-1};
    explicit File(const char *path) : fd{::open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)} {
        if (fd < 0) { throw std::runtime_error(std::string{"exclusive output: "} + std::strerror(errno)); }
    }
    ~File() noexcept { if (fd >= 0) { ::close(fd); } }
    void write(const void *data, std::size_t size) {
        const auto *bytes{static_cast<const std::uint8_t *>(data)};
        while (size != 0) {
            const ssize_t count{::write(fd, bytes, size)};
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { throw std::runtime_error("output write failed"); }
            bytes += count;
            size -= static_cast<std::size_t>(count);
        }
    }
};
/** @brief 探测器的有界写盘线程；在途包也计入 8 MiB，满时明确失败。 */
class ProbeWriter {
public:
    explicit ProbeWriter(File &file) : file_{file}, thread_{[this] { run(); }} {}
    ~ProbeWriter() { stop(); }
    void submit(std::vector<std::uint8_t> packet) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (error_) { std::rethrow_exception(error_); }
        const std::size_t size{packet.size()};
        if (size > LIMIT - queued_bytes_) { throw std::runtime_error("encoded queue exceeds 8 MiB"); }
        queue_.push_back(std::move(packet));
        queued_bytes_ += size;
        peak_bytes_ = std::max(peak_bytes_, queued_bytes_);
        ready_.notify_one();
    }
    void finish() {
        stop();
        if (error_) { std::rethrow_exception(error_); }
    }
    double seconds() const noexcept { return write_seconds_; }
    std::size_t peakBytes() const noexcept { return peak_bytes_; }
private:
    void stop() noexcept {
        { std::lock_guard<std::mutex> lock{mutex_}; stopping_ = true; }
        ready_.notify_one();
        if (thread_.joinable()) { thread_.join(); }
    }
    void run() noexcept {
        try {
            while (true) {
                std::vector<std::uint8_t> packet{};
                {
                    std::unique_lock<std::mutex> lock{mutex_};
                    ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                    if (queue_.empty()) { return; }
                    packet = std::move(queue_.front()); queue_.pop_front();
                }
                const auto begin{std::chrono::steady_clock::now()};
                file_.write(packet.data(), packet.size());
                write_seconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
                { std::lock_guard<std::mutex> lock{mutex_}; queued_bytes_ -= packet.size(); }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock{mutex_}; error_ = std::current_exception();
        }
    }
    static constexpr std::size_t LIMIT{8 * 1024 * 1024};
    File &file_;
    std::mutex mutex_{};
    std::condition_variable ready_{};
    std::deque<std::vector<std::uint8_t>> queue_{};
    std::size_t queued_bytes_{};
    std::size_t peak_bytes_{};
    double write_seconds_{};
    bool stopping_{};
    std::exception_ptr error_{};
    std::thread thread_{};
};
/** @brief 验证采集与编码并行，使用与录像相同的四槽非阻塞提交边界。 */
class ProbeCapture {
public:
    rk3576::RecordingSlots slots{1920, 1200, 1};
    ProbeCapture(rk3576::Sc233hgsCameraDevice &camera, int frames) : camera_{camera} {
        camera_.start();
        try {
            thread_ = std::thread{[this, frames] {
                try {
                    while (!stop_.load() && slots.statistics().published < static_cast<unsigned>(frames)) {
                        camera_.next(100, true, &slots);
                    }
                } catch (...) { error_ = std::current_exception(); }
                done_.store(true, std::memory_order_release);
            }};
        } catch (...) { camera_.finish(); throw; }
    }
    ~ProbeCapture() { stop(); }
    void stop() noexcept {
        stop_.store(true);
        try { camera_.wake(); } catch (...) {}
        if (thread_.joinable()) { thread_.join(); }
    }
    int next() {
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{3}};
        while (true) {
            const int slot{slots.acquire()};
            if (slot >= 0) { return slot; }
            if (done_.load(std::memory_order_acquire)) {
                const int last_slot{slots.acquire()};
                if (last_slot >= 0) { return last_slot; }
                if (error_) { std::rethrow_exception(error_); }
                throw std::runtime_error("camera ended before expected frame count");
            }
            if (std::chrono::steady_clock::now() >= deadline) { throw std::runtime_error("camera frame deadline"); }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
private:
    rk3576::Sc233hgsCameraDevice &camera_;
    std::thread thread_{};
    std::atomic<bool> stop_{false};
    std::atomic<bool> done_{false};
    std::exception_ptr error_{};
};
int positive(const char *text) {
    std::size_t end{};
    const int value{std::stoi(text, &end)};
    if (text[end] != '\0' || value <= 0) { throw std::runtime_error("expected positive integer"); }
    return value;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 5 && argc != 8) {
            throw std::runtime_error("usage: rk3576_recording_probe OUTPUT.h264 FPS BITRATE FRAMES "
                "[CAMERA_DEVICE MEDIA_DEVICE SENSOR_ENTITY] (1920x1200 NV12)");
        }
        const int fps{positive(argv[2])};
        const int bitrate{positive(argv[3])};
        const int frames{positive(argv[4])};
        if (fps > 240 || bitrate > 100000000 || frames > 36000) {
            throw std::runtime_error("probe resource limit: fps<=240, bitrate<=100000000, frames<=36000");
        }
        constexpr int width{1920};
        constexpr int height{1200};
        constexpr int stride{1920};
        constexpr int vertical_stride{1200};
        constexpr std::size_t bytes{stride * vertical_stride * 3 / 2};
        std::unique_ptr<rk3576::Sc233hgsCameraDevice> camera{};
        if (argc == 8) {
            FrameSourceConfig config{};
            config.path = argv[5];
            config.camera_media_device = argv[6];
            config.camera_sensor_entity = argv[7];
            config.width = width;
            config.height = height;
            config.fps = fps;
            config.trigger_mode = false;
            config.camera_sensor_model = CameraSensorModel::Sc233hgs;
            camera = std::make_unique<rk3576::Sc233hgsCameraDevice>(config,
                std::make_shared<rk3576::CameraSystem>());
            camera->prepare();
            const auto format{camera->format()};
            std::cout << "camera_stride=" << format.stride << " colorspace=" << format.colorspace
                      << " quantization=" << format.quantization << " ycbcr=" << format.ycbcr_encoding
                      << " transfer=" << format.transfer_function << std::endl;
            // 本验证固定基线：不把未知矩阵自动解释为 BT.601 或 BT.709。
            if (format.quantization != V4L2_QUANTIZATION_FULL_RANGE
                || format.colorspace != V4L2_COLORSPACE_DEFAULT || format.ycbcr_encoding != 0
                || format.transfer_function != 0) {
                throw std::runtime_error("probe does not support negotiated camera color metadata");
            }
        }
        Encoder encoder{};
        check(mpp_create(&encoder.context, &encoder.api), "create");
        RK_S64 timeout{1000};
        check(encoder.api->control(encoder.context, MPP_SET_OUTPUT_TIMEOUT, &timeout), "output timeout");
        check(mpp_init(encoder.context, MPP_CTX_ENC, MPP_VIDEO_CodingAVC), "H264 init");
        check(mpp_enc_cfg_init(&encoder.config), "config init");
        check(encoder.api->control(encoder.context, MPP_ENC_GET_CFG, encoder.config), "get config");
        encoder.set("prep:width", width);
        encoder.set("prep:height", height);
        encoder.set("prep:hor_stride", stride);
        encoder.set("prep:ver_stride", vertical_stride);
        encoder.set("prep:format", MPP_FMT_YUV420SP);
        encoder.set("prep:range", MPP_FRAME_RANGE_JPEG);
        encoder.set("prep:colorspace", MPP_FRAME_SPC_UNSPECIFIED);
        encoder.set("prep:colorprim", MPP_FRAME_PRI_UNSPECIFIED);
        encoder.set("prep:colortrc", MPP_FRAME_TRC_UNSPECIFIED);
        encoder.set("rc:mode", MPP_ENC_RC_MODE_CBR);
        encoder.set("rc:fps_in_flex", 0);
        encoder.set("rc:fps_in_num", fps);
        encoder.set("rc:fps_in_denorm", 1);
        encoder.set("rc:fps_out_flex", 0);
        encoder.set("rc:fps_out_num", fps);
        encoder.set("rc:fps_out_denorm", 1);
        encoder.set("rc:bps_target", bitrate);
        encoder.set("rc:bps_max", bitrate + bitrate / 16);
        encoder.set("rc:bps_min", bitrate - bitrate / 16);
        encoder.set("rc:gop", fps);
        encoder.set("rc:drop_mode", 0);
        encoder.set("h264:profile", 66); // Baseline 编码不允许 B 帧。
        encoder.set("h264:level", 50);
        encoder.set("h264:cabac_en", 0);
        encoder.set("h264:cabac_idc", -1);
        check(encoder.api->control(encoder.context, MPP_ENC_SET_CFG, encoder.config), "apply config");
        check(encoder.api->control(encoder.context, MPP_ENC_GET_CFG, encoder.config), "read back config");
        for (const auto &entry : std::vector<std::pair<const char *, int>>{
                 {"prep:width", width}, {"prep:height", height}, {"prep:hor_stride", stride},
                 {"prep:ver_stride", vertical_stride}, {"rc:fps_in_num", fps},
                 {"rc:fps_out_num", fps}, {"rc:bps_target", bitrate}, {"h264:profile", 66},
                 {"h264:level", 50}, {"prep:range", MPP_FRAME_RANGE_JPEG}}) {
            RK_S32 actual{};
            check(mpp_enc_cfg_get_s32(encoder.config, entry.first, &actual), entry.first);
            if (actual != entry.second) {
                throw std::runtime_error(std::string{"encoder changed requested parameter "} + entry.first
                    + ": requested=" + std::to_string(entry.second) + " actual=" + std::to_string(actual));
            }
        }
        MppEncHeaderMode header{MPP_ENC_HEADER_MODE_EACH_IDR};
        check(encoder.api->control(encoder.context, MPP_ENC_SET_HEADER_MODE, &header), "repeat headers");
        check(mpp_buffer_group_get_internal(&encoder.group, MPP_BUFFER_TYPE_DRM), "buffer group");
        check(mpp_buffer_get(encoder.group, &encoder.buffer, bytes), "input buffer");
        File output{argv[1]};
        ProbeWriter writer{output};
        const auto begin{std::chrono::steady_clock::now()};
        std::size_t total{};
        std::int64_t first_timestamp{-1};
        std::int64_t last_timestamp{};
        std::int64_t next_idr{};
        std::size_t idr_count{};
        double capture_seconds{};
        double encode_seconds{};
        double max_encode_seconds{};
        std::unique_ptr<ProbeCapture> capture{};
        StageTiming wait_timing{static_cast<std::size_t>(frames)};
        StageTiming sync_begin_timing{static_cast<std::size_t>(frames)};
        StageTiming copy_timing{static_cast<std::size_t>(frames)};
        StageTiming sync_end_timing{static_cast<std::size_t>(frames)};
        StageTiming encode_timing{static_cast<std::size_t>(frames)};
        StageTiming output_timing{static_cast<std::size_t>(frames)};
        if (camera) { capture = std::make_unique<ProbeCapture>(*camera, frames); }
        for (int index{}; index < frames; ++index) {
            const auto capture_begin{std::chrono::steady_clock::now()};
            std::int64_t timestamp{static_cast<std::int64_t>(index) * 1000000 / fps};
            int input_slot{-1};
            if (camera) {
                input_slot = capture->next();
                timestamp = capture->slots.identity(input_slot).timestamp_us;
            }
            capture_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - capture_begin).count();
            wait_timing.add(capture_begin);
            if (first_timestamp < 0) { first_timestamp = timestamp; }
            last_timestamp = timestamp;
            const auto pts{timestamp - first_timestamp};
            auto stage_begin{Clock::now()};
            check(mpp_buffer_sync_begin(encoder.buffer), "input CPU begin");
            sync_begin_timing.add(stage_begin);
            stage_begin = Clock::now();
            auto *pixels{static_cast<std::uint8_t *>(mpp_buffer_get_ptr(encoder.buffer))};
            if (!pixels) { throw std::runtime_error("input not mapped"); }
            if (camera) {
                std::copy_n(capture->slots.pixels(input_slot), bytes, pixels);
            } else {
                for (int y{}; y < height; ++y) {
                    for (int x{}; x < width; ++x) {
                        pixels[y * stride + x] = static_cast<std::uint8_t>((x + y + index) % 256);
                    }
                }
                std::fill(pixels + stride * vertical_stride, pixels + bytes, 128);
            }
            copy_timing.add(stage_begin);
            stage_begin = Clock::now();
            check(mpp_buffer_sync_end(encoder.buffer), "input CPU end");
            sync_end_timing.add(stage_begin);
            if (input_slot >= 0) { capture->slots.release(input_slot); }
            check(mpp_frame_init(&encoder.frame), "frame init");
            mpp_frame_set_width(encoder.frame, width);
            mpp_frame_set_height(encoder.frame, height);
            mpp_frame_set_hor_stride(encoder.frame, stride);
            mpp_frame_set_ver_stride(encoder.frame, vertical_stride);
            mpp_frame_set_fmt(encoder.frame, MPP_FMT_YUV420SP);
            mpp_frame_set_pts(encoder.frame, pts);
            mpp_frame_set_buffer(encoder.frame, encoder.buffer);
            mpp_frame_set_eos(encoder.frame, index == frames - 1);
            if (pts >= next_idr) {
                next_idr = pts + 1000000;
                check(encoder.api->control(encoder.context, MPP_ENC_SET_IDR_FRAME, nullptr), "request IDR");
            }
            const auto encode_begin{std::chrono::steady_clock::now()};
            check(encoder.api->encode_put_frame(encoder.context, encoder.frame), "submit frame");
            check(encoder.api->encode_get_packet(encoder.context, &encoder.packet), "receive packet");
            encode_timing.add(encode_begin);
            stage_begin = Clock::now();
            if (!encoder.packet || mpp_packet_is_partition(encoder.packet)) {
                throw std::runtime_error("missing or partitioned output; input must not be reused");
            }
            const double encode_duration{std::chrono::duration<double>(
                std::chrono::steady_clock::now() - encode_begin).count()};
            encode_seconds += encode_duration;
            max_encode_seconds = std::max(max_encode_seconds, encode_duration);
            const std::size_t length{mpp_packet_get_length(encoder.packet)};
            if (length == 0 || length > 8 * 1024 * 1024) { throw std::runtime_error("invalid packet size"); }
            if (mpp_packet_get_pts(encoder.packet) != pts) { throw std::runtime_error("encoder PTS mismatch"); }
            const auto *device_bytes{static_cast<const std::uint8_t *>(mpp_packet_get_pos(encoder.packet))};
            // MPP 输出可能位于不可缓存的 DMA 映射中。先批量复制一次，再解析 NAL；
            // 逐字节扫描设备映射会使大 IDR 包占用多个相机帧周期。
            std::vector<std::uint8_t> encoded{device_bytes, device_bytes + length};
            bool idr{};
            for (std::size_t offset{}; offset + 4 < length; ++offset) {
                if (encoded[offset] == 0 && encoded[offset + 1] == 0 && encoded[offset + 2] == 1
                    && (encoded[offset + 3] & 31) == 5) { idr = true; }
            }
            if (idr) { ++idr_count; }
            if (pts + 1000000 == next_idr && !idr) { throw std::runtime_error("requested IDR missing"); }
            writer.submit(std::move(encoded));
            total += length;
            check(mpp_packet_deinit(&encoder.packet), "packet release");
            check(mpp_frame_deinit(&encoder.frame), "frame release");
            output_timing.add(stage_begin);
        }
        if (camera) {
            capture->stop();
            const auto status{camera->captureStatus()};
            const auto gaps{camera->sequenceGaps()};
            const auto cleanup{camera->finish()};
            if (!cleanup.empty()) { throw std::runtime_error(cleanup); }
            std::cout << "warmup_discarded=" << status.warmup_discarded
                      << " recording_dropped=" << capture->slots.statistics().full
                      << " recording_invalid=" << capture->slots.statistics().invalid
                      << " first_sequence=" << status.first_sequence << " sequence_gaps=" << gaps
                      << " capture_fps=" << (frames - 1) * 1000000.0 / (last_timestamp - first_timestamp) << '\n';
        }
        writer.finish();
        wait_timing.report("wait_input");
        sync_begin_timing.report("sync_begin");
        copy_timing.report("copy_to_mpp");
        sync_end_timing.report("sync_end");
        encode_timing.report("encode");
        output_timing.report("scan_and_enqueue");
        std::cout << "mpp_input_group_bytes=" << mpp_buffer_group_usage(encoder.group) << '\n';
        rusage usage{};
        if (::getrusage(RUSAGE_SELF, &usage) != 0) { throw std::runtime_error("getrusage failed"); }
        std::cout << "capture_seconds=" << capture_seconds << " encode_seconds=" << encode_seconds
                  << " max_encode_seconds=" << max_encode_seconds << " write_seconds=" << writer.seconds()
                  << " encoded_queue_peak_bytes=" << writer.peakBytes() << '\n';
        std::cout << "max_rss_kib=" << usage.ru_maxrss << " idr_count=" << idr_count << '\n';
        if (::fdatasync(output.fd) != 0) { throw std::runtime_error("output sync failed"); }
        const double seconds{std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()};
        std::cout << "frames=" << frames << " bytes=" << total << " seconds=" << seconds
                  << " throughput_fps=" << frames / seconds << " input_buffer_bytes=" << bytes << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
