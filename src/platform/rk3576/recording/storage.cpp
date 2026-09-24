#include "storage.h"

#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "metadata.h"
#include "session.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}
#include <yaml-cpp/yaml.h>

namespace rk3576::recording {
ssize_t FileIo::write(int fd, const void *data, std::size_t size) { return ::write(fd, data, size); }
int FileIo::dataSync(int fd) { return ::fdatasync(fd); }
int FileIo::directorySync(int fd) { return ::fsync(fd); }
int FileIo::availableBytes(int fd, std::uint64_t &bytes) {
    struct statvfs fs{};
    const int result{::fstatvfs(fd, &fs)};
    if (result == 0) { bytes = static_cast<std::uint64_t>(fs.f_bavail) * fs.f_frsize; }
    return result;
}
namespace {
struct Descriptor {
    int value{-1};
    ~Descriptor() { if (value >= 0) { ::close(value); } }
    void reset(int fd = -1) { if (value >= 0) { ::close(value); } value = fd; }
};
std::runtime_error systemError(const char *operation) {
    return std::runtime_error(std::string{operation} + ": " + std::strerror(errno));
}
void avCheck(int result, const char *operation) {
    if (result < 0) { throw std::runtime_error(std::string{operation} + ": AV error " + std::to_string(result)); }
}
} // namespace
struct Storage::Impl {
    CameraRecordingConfig config{};
    CameraFormat format{};
    std::uint64_t round{};
    RecordingSlots::Header &status;
    std::shared_ptr<FileIo> io{};
    std::string directory{}, callback_error{};
    Descriptor root{}, session{}, video{}, frames{}, commits{}, events{};
    AVFormatContext *mux{};
    AVIOContext *avio{};
    unsigned segment{};
    std::string stem{};
    std::uint64_t used{}, offset{}, frame_count{}, segment_frames{}, synced_frames{};
    std::int64_t segment_start_us{}, last_frame_us{}, last_sync_us{}, last_scan_us{}, last_pts_us{-1};
    std::vector<std::string> pending{};
    bool finished{};

    Impl(CameraRecordingConfig input, CameraFormat image, std::uint64_t id,
         RecordingSlots::Header &shared, std::shared_ptr<FileIo> injected)
        : config{std::move(input)}, format{image}, round{id}, status{shared}, io{std::move(injected)} {
        if (!io) { io = std::make_shared<FileIo>(); }
        pending.reserve(512);
    }
    ~Impl() { closeMux(); }
    void closeMux() noexcept {
        if (mux) { mux->pb = nullptr; avformat_free_context(mux); mux = nullptr; }
        if (avio) { av_freep(&avio->buffer); avio_context_free(&avio); }
    }
    void scanUsage() {
        std::uint64_t total{};
        std::size_t count{};
        for (const auto &entry : std::filesystem::recursive_directory_iterator{config.directory}) {
            if (++count > 100000) { throw std::runtime_error("Recording directory scan exceeds 100000 entries"); }
            if (entry.is_symlink()) { throw std::runtime_error("Recording evidence directory contains symlink"); }
            if (entry.is_regular_file()) {
                const auto size{entry.file_size()};
                if (size > UINT64_MAX - total) { throw std::runtime_error("Recording usage overflow"); }
                total += size;
            }
        }
        used = total;
        last_scan_us = RecordingSlots::monotonicUs();
        status.total_bytes.store(used);
    }
    void quota(std::size_t size, bool reserve) {
        const auto now{RecordingSlots::monotonicUs()};
        if (now - last_scan_us >= 1000000) { scanUsage(); }
        std::uint64_t free{};
        if (io->availableBytes(root.value, free)) { throw systemError("recording free space query"); }
        status.free_bytes.store(free);
        const std::uint64_t allowance{static_cast<std::uint64_t>(size) + (reserve ? 65536U : 0U)};
        if (used > config.max_total_bytes || allowance > config.max_total_bytes - used
            || free < config.min_free_bytes || allowance > free - config.min_free_bytes) {
            throw std::runtime_error("Recording quota or free-space protection reached; history retained");
        }
    }
    void writeAll(int fd, const void *data, std::size_t size, bool reserve = false) {
        quota(size, reserve);
        const auto *bytes{static_cast<const std::uint8_t *>(data)};
        while (size) {
            const ssize_t count{io->write(fd, bytes, size)};
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { throw systemError("recording write"); }
            if (static_cast<std::size_t>(count) > size) { throw std::runtime_error("Invalid write result"); }
            bytes += count; size -= count; used += count;
            status.total_bytes.store(used);
            if (fd == video.value) { offset += count; }
        }
    }
    void text(int fd, const std::string &value) { writeAll(fd, value.data(), value.size()); }
    void syncFile(int fd) {
        int result{};
        do { result = io->dataSync(fd); } while (result < 0 && errno == EINTR);
        if (result) { throw systemError("recording fdatasync"); }
    }
    void syncDirectory(int fd) {
        int result{};
        do { result = io->directorySync(fd); } while (result < 0 && errno == EINTR);
        if (result) { throw systemError("recording directory fsync"); }
    }
    int create(const std::string &name) {
        const int fd{::openat(session.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (fd < 0) { throw systemError("exclusive recording file creation"); }
        return fd;
    }
    void marker(const std::string &name, const std::string &body) {
        Descriptor file{};
        const auto pending_name{name + ".pending"};
        file.value = create(pending_name);
        text(file.value, body + '\n'); syncFile(file.value);
        // 只有完整且已同步的内容才能获得正式名称；linkat 排他创建，不覆盖历史证据。
        if (::linkat(session.value, pending_name.c_str(), session.value, name.c_str(), 0)) {
            throw systemError("publish recording marker");
        }
        try {
            if (::unlinkat(session.value, pending_name.c_str(), 0)) { throw systemError("remove marker staging name"); }
            syncDirectory(session.value);
        } catch (...) {
            ::unlinkat(session.value, name.c_str(), 0);
            throw;
        }
    }
    static int writePacket(void *opaque, std::uint8_t *buffer, int size) noexcept {
        auto &self{*static_cast<Impl *>(opaque)};
        try { self.writeAll(self.video.value, buffer, static_cast<std::size_t>(size), true); return size; }
        catch (const std::exception &error) { self.callback_error = error.what(); return AVERROR(EIO); }
    }
    void flush() {
        avio_flush(avio);
        if (!callback_error.empty()) { throw std::runtime_error(callback_error); }
        avCheck(avio->error, "TS output");
    }
    void openSegment(const EncodedFrame &frame) {
        if (!frame.idr) { throw std::runtime_error("New recording segment requires IDR"); }
        std::ostringstream name{};
        name << std::setw(6) << std::setfill('0') << ++segment;
        stem = name.str(); offset = 0; segment_frames = 0; segment_start_us = frame.pts_us;
        video.reset(create(stem + ".ts")); frames.reset(create(stem + ".frames.jsonl"));
        avCheck(avformat_alloc_output_context2(&mux, nullptr, "mpegts", nullptr), "create TS muxer");
        auto *buffer{static_cast<unsigned char *>(av_malloc(32768))};
        if (!buffer) { throw std::bad_alloc{}; }
        avio = avio_alloc_context(buffer, 32768, 1, this, nullptr, &writePacket, nullptr);
        if (!avio) { av_free(buffer); throw std::bad_alloc{}; }
        mux->pb = avio; mux->flags |= AVFMT_FLAG_CUSTOM_IO; mux->max_delay = 0;
        mux->avoid_negative_ts = 0; // FFmpeg 4.4 的禁用值；保留首帧 PTS=0，不移动时间轴。
        auto *stream{avformat_new_stream(mux, nullptr)};
        if (!stream) { throw std::bad_alloc{}; }
        stream->time_base = {1, 90000};
        auto *codec{stream->codecpar};
        codec->codec_type = AVMEDIA_TYPE_VIDEO; codec->codec_id = AV_CODEC_ID_H264;
        codec->width = format.width; codec->height = format.height;
        codec->color_range = AVCOL_RANGE_JPEG; codec->color_space = AVCOL_SPC_UNSPECIFIED;
        AVDictionary *options{};
        av_dict_set(&options, "mpegts_flags", "+resend_headers", 0);
        av_dict_set(&options, "mpegts_copyts", "1", 0);
        av_dict_set(&options, "muxdelay", "0", 0);
        const int result{avformat_write_header(mux, &options)};
        av_dict_free(&options);
        avCheck(result, "write TS header"); flush();
        syncFile(video.value); syncFile(frames.value); syncDirectory(session.value);
    }
    void sync() {
        const auto begin{RecordingSlots::monotonicUs()};
        status.sync_started_us.store(begin, std::memory_order_release);
        if (video.value >= 0) {
            flush(); syncFile(video.value);
            for (const auto &row : pending) { text(frames.value, row); }
            syncFile(frames.value);
            text(commits.value, "{\"round\":" + std::to_string(round) + ",\"segment\":" + std::to_string(segment)
                + ",\"video_end\":" + std::to_string(offset) + ",\"frames\":" + std::to_string(frame_count)
                + ",\"last_frame_us\":" + std::to_string(last_frame_us) + "}\n");
        }
        syncFile(events.value); syncFile(commits.value);
        const auto end{RecordingSlots::monotonicUs()};
        // 即使 syscall 已返回，超时同步仍不能发布成功水位。
        if (end - begin > 5000000 || status.failure.load()) { throw std::runtime_error("Recording sync failed deadline"); }
        if (frame_count) {
            status.last_synced_frame_us.store(last_frame_us);
            status.synced.store(frame_count, std::memory_order_release);
        }
        status.last_sync_mono_us.store(end, std::memory_order_release);
        status.sync_duration_us.store(end - begin);
        status.sync_started_us.store(0, std::memory_order_release);
        synced_frames = frame_count; pending.clear(); last_sync_us = end;
    }
    void closeSegment() {
        if (!mux) { return; }
        avCheck(av_write_trailer(mux), "TS trailer"); flush(); sync();
        marker(stem + ".complete.json", "{\"frames\":" + std::to_string(segment_frames)
            + ",\"video_end\":" + std::to_string(offset) + "}");
        closeMux(); video.reset(); frames.reset();
    }
};

Storage::Storage(CameraRecordingConfig config, CameraFormat format, std::uint64_t round,
                 std::string manifest, RecordingSlots::Header &status, std::shared_ptr<FileIo> io)
    : impl_{std::make_unique<Impl>(std::move(config), format, round, status, std::move(io))} {
    auto &s{*impl_};
    FrameSourceConfig check{}; check.camera_recording = s.config; check.camera_sensor_model = CameraSensorModel::Sc233hgs;
    validateConfig(check);
    const auto begin{RecordingSlots::monotonicUs()};
    status.sync_started_us.store(begin);
    if (::mkdir(s.config.directory.c_str(), 0700) && errno != EEXIST) {
        throw systemError("create recording root; its parent must already exist");
    }
    s.root.value = ::open(s.config.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (s.root.value < 0) { throw systemError("open recording root"); }
    struct statfs fs{};
    if (::fstatfs(s.root.value, &fs) || fs.f_type != 0xEF53) { throw std::runtime_error("Recording mount changed"); }
    s.scanUsage(); s.quota(0, true);
    std::array<unsigned char, 16> nonce{};
    if (::getrandom(nonce.data(), nonce.size(), 0) != static_cast<ssize_t>(nonce.size())) {
        throw systemError("recording session identity");
    }
    std::ostringstream name{}; name << "session-";
    for (const auto byte : nonce) { name << std::hex << std::setw(2) << std::setfill('0') << unsigned{byte}; }
    if (::mkdirat(s.root.value, name.str().c_str(), 0700)) { throw systemError("exclusive session creation"); }
    s.directory = (std::filesystem::path{s.config.directory} / name.str()).string();
    s.session.value = ::openat(s.root.value, name.str().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (s.session.value < 0) { throw systemError("open session directory"); }
    s.commits.value = s.create("commits.jsonl"); s.events.value = s.create("events.jsonl");
    s.marker("session.json", manifest);
    s.syncFile(s.commits.value); s.syncFile(s.events.value); s.syncDirectory(s.session.value); s.syncDirectory(s.root.value);
    // 新建根目录本身的目录项也同步；已有根目录同样安全。
    Descriptor parent{};
    parent.value = ::open(std::filesystem::path{s.config.directory}.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent.value < 0) { throw systemError("open recording parent directory"); }
    s.syncDirectory(parent.value);
    if (RecordingSlots::monotonicUs() - begin > 5000000) { throw std::runtime_error("Recording initialization sync timeout"); }
    status.sync_started_us.store(0); s.last_sync_us = RecordingSlots::monotonicUs();
}
Storage::~Storage() = default;
const std::string &Storage::directory() const { return impl_->directory; }
void Storage::writeFrame(const EncodedFrame &frame) {
    auto &s{*impl_};
    if (frame.identity.round != s.round || frame.pts_us <= s.last_pts_us) {
        throw std::runtime_error("Recording frame round or PTS order mismatch");
    }
    if (s.mux && frame.idr && frame.pts_us - s.segment_start_us >= s.config.segment_seconds * 1000000LL) {
        s.closeSegment();
    }
    if (!s.mux) { s.openSegment(frame); }
    AVPacket packet{};
    packet.data = const_cast<std::uint8_t *>(frame.bytes.data()); packet.size = frame.bytes.size();
    packet.stream_index = 0;
    packet.pts = av_rescale_q(frame.pts_us, {1, 1000000}, s.mux->streams[0]->time_base);
    packet.dts = packet.pts; packet.pos = -1;
    if (frame.idr) { packet.flags |= AV_PKT_FLAG_KEY; }
    avCheck(av_write_frame(s.mux, &packet), "write complete TS frame");
    s.flush(); // 逐包交付实际 fd 后才登记完整帧边界；不使用编码产包位置冒充文件位置。
    if (s.pending.size() >= 512) { throw std::runtime_error("Unsynced frame metadata exceeds bound"); }
    s.pending.push_back("{" + identityFields(frame.identity) + ",\"pts_us\":" + std::to_string(frame.pts_us)
        + ",\"encoded_monotonic_us\":" + std::to_string(frame.encoded_monotonic_us)
        + ",\"first_qualified_input_monotonic_us\":" + std::to_string(s.status.first_input_mono_us.load())
        + ",\"pts_90k\":" + std::to_string(packet.pts) + ",\"segment\":" + std::to_string(s.segment)
        + ",\"video_end\":" + std::to_string(s.offset) + ",\"idr\":" + (frame.idr ? "true" : "false") + "}\n");
    ++s.frame_count; ++s.segment_frames; s.last_frame_us = frame.identity.timestamp_us; s.last_pts_us = frame.pts_us;
    s.status.written.store(s.frame_count);
    tick();
}
void Storage::writeEvent(const RecordingSlots::Event &event) {
    impl_->text(impl_->events.value, "{" + identityFields(event.identity) + ",\"kind\":"
        + std::to_string(event.kind) + ",\"first_sequence\":" + std::to_string(event.first_sequence)
        + ",\"last_sequence\":" + std::to_string(event.last_sequence)
        + ",\"count\":" + std::to_string(event.count) + "}\n");
}
void Storage::tick() {
    auto &s{*impl_};
    if (RecordingSlots::monotonicUs() - s.last_sync_us >= s.config.sync_interval_ms * 1000LL) {
        // 没有新视频时不虚增视频进展时刻；实际元数据同步仍受超时监控。
        if (s.frame_count != s.synced_frames) { s.sync(); }
    }
}
void Storage::finish(const std::string &camera_summary) {
    auto &s{*impl_};
    const auto camera{YAML::Load(camera_summary)};
    s.closeSegment();
    s.marker("camera.json", camera_summary);
    std::string outcome{"complete"};
    if (camera["failed"].as<bool>()) { outcome = s.frame_count ? "camera_failed" : "camera_warmup_failed"; }
    else if (!s.frame_count) { outcome = "cancelled_before_first_frame"; }
    s.syncFile(s.events.value);
    s.marker("complete.json", "{\"outcome\":" + quote(outcome) + ",\"video_success\":"
        + (outcome == "complete" ? "true" : "false") + ",\"frames\":" + std::to_string(s.frame_count)
        + ",\"accepted\":" + std::to_string(s.status.accepted.load())
        + ",\"attempted\":" + std::to_string(s.status.attempts.load())
        + ",\"dropped\":" + std::to_string(s.status.dropped.load())
        + ",\"cancelled\":" + std::to_string(s.status.cancelled.load()) + "}");
    s.finished = true;
}
} // namespace rk3576::recording
