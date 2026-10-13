#include "realtime_source.h"

#include <array>
#include <cerrno>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <sys/resource.h>
#include <openssl/evp.h>
#include "diagnostics/recorder.h"
#include "platform/rk3576/host_frame.h"
#include <yaml-cpp/yaml.h>
#include <rclcpp/time.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace tracking_regression {
namespace {
std::string jsonString(const std::string &text) {
    std::ostringstream output{};
    output << '"';
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\') { output << '\\' << character; }
        else if (character < 32) {
            output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned{character};
        } else { output << character; }
    }
    output << '"';
    return output.str();
}
std::uint64_t systemUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::chrono::steady_clock::time_point deadline(std::uint64_t ns) {
    return std::chrono::steady_clock::time_point{std::chrono::nanoseconds{ns}};
}
} // namespace
RealtimeSource::RealtimeSource(SourceOptions options) : options_{std::move(options)} {
    if (options_.queue_capacity == 0 || options_.max_supply_lateness_us == 0) {
        throw std::invalid_argument{"missing queue capacity or lateness budget"};
    }
    std::ifstream frames{options_.fixture / "frames.jsonl"};
    std::string line{};
    while (std::getline(frames, line)) {
        const auto row{YAML::Load(line)};
        records_.push_back({row["fixture_frame_id"].as<std::uint64_t>(), row["relative_us"].as<std::uint64_t>()});
        pixel_hashes_.push_back(row["pixel_sha256"].as<std::string>());
    }
    if (records_.size() != 1200 || records_.front().relative_us != 15721
        || records_.back().relative_us != 19992663) {
        throw std::invalid_argument{"fixed source frame boundaries are invalid"};
    }
    std::ifstream attitudes{options_.fixture / "attitude.jsonl"};
    while (std::getline(attitudes, line)) {
        const auto row{YAML::Load(line)};
        sensor_msgs::msg::Imu message{};
        message.header.frame_id = row["frame_id"].as<std::string>();
        const auto q{row["orientation"]};
        message.orientation.x = q[0].as<double>(); message.orientation.y = q[1].as<double>();
        message.orientation.z = q[2].as<double>(); message.orientation.w = q[3].as<double>();
        for (std::size_t i{}; i < 9; ++i) {
            message.orientation_covariance[i] = row["orientation_covariance"][i].as<double>();
            message.angular_velocity_covariance[i] = row["angular_velocity_covariance"][i].as<double>();
            message.linear_acceleration_covariance[i] = row["linear_acceleration_covariance"][i].as<double>();
        }
        const auto angular{row["angular_velocity"]}, linear{row["linear_acceleration"]};
        message.angular_velocity.x = angular[0].as<double>(); message.angular_velocity.y = angular[1].as<double>();
        message.angular_velocity.z = angular[2].as<double>(); message.linear_acceleration.x = linear[0].as<double>();
        message.linear_acceleration.y = linear[1].as<double>(); message.linear_acceleration.z = linear[2].as<double>();
        attitudes_.emplace_back(row["relative_us"].as<std::int64_t>(), std::move(message));
    }
    if (attitudes_.empty() || attitudes_.front().first >= 0) {
        throw std::invalid_argument{"initial attitude missing"};
    }
    status_.state = FrameSourceState::Ready;
    status_.path = (options_.fixture / "clip.mkv").string();
}
RealtimeSource::~RealtimeSource() { stop(); }
void RealtimeSource::setAttitudeCallback(std::function<void(sensor_msgs::msg::Imu::SharedPtr)> callback) {
    attitude_callback_ = std::move(callback);
}
bool RealtimeSource::prepare() {
    if (prepared_) { return true; }
    if (preparation_attempted_) { return false; }
    preparation_attempted_ = true;
    preparation_begin_ns_ = diagnostics::monotonicNs();
    exportPreparation();
    try {
        decoded_.reserve(records_.size());
        decode();
        if (stopping_) { throw std::runtime_error{"preparation interrupted"}; }
        prepared_ = true;
    } catch (const std::exception &error) {
        fail(std::string{"preparation: "} + error.what());
        decoded_.clear();
    }
    preparation_end_ns_ = diagnostics::monotonicNs();
    exportPreparation();
    return prepared_;
}
bool RealtimeSource::start() {
    if (started_ || stopping_) { return false; }
    started_ = true;
    if (!prepare()) { return false; }
    {
        std::lock_guard<std::mutex> lock{mutex_};
        status_.state = FrameSourceState::Running;
    }
    // Source and prediction clock origins are established only after model construction and complete verified preparation.
    m0_ns_ = diagnostics::monotonicNs(); e0_us_ = systemUs();
    for (auto &record : records_) {
        record.planned_ns = m0_ns_ + record.relative_us * 1000;
        record.frame_time_us = e0_us_ + record.relative_us;
    }
    std::ofstream clock{options_.output / "input-clock.json"};
    clock << "{\"m0_ns\":" << m0_ns_ << ",\"e0_us\":" << e0_us_ << "}\n";
    if (!clock) { fail("clock output failed"); return false; }
    if (attitude_callback_) {
        auto message{std::make_shared<sensor_msgs::msg::Imu>(attitudes_.front().second)};
        message->header.stamp = rclcpp::Time{static_cast<std::int64_t>((e0_us_ + attitudes_.front().first) * 1000)};
        attitude_callback_(std::move(message));
    }
    supplier_ = std::thread{[this] {
        try { supply(); }
        catch (const std::exception &error) { fail(std::string{"supply: "} + error.what()); }
    }};
    return true;
}
void RealtimeSource::fail(const std::string &reason) {
    std::lock_guard<std::mutex> lock{mutex_};
    status_.state = FrameSourceState::Failed;
    if (status_.reason.empty()) { status_.reason = reason; }
    stopping_ = true;
    wake_.notify_all();
}
void RealtimeSource::decode() {
    struct FormatDeleter {
        void operator()(AVFormatContext *value) const noexcept { avformat_close_input(&value); }
    };
    struct CodecDeleter {
        void operator()(AVCodecContext *value) const noexcept { avcodec_free_context(&value); }
    };
    struct FrameDeleter {
        void operator()(AVFrame *value) const noexcept { av_frame_free(&value); }
    };
    struct PacketDeleter {
        void operator()(AVPacket *value) const noexcept { av_packet_free(&value); }
    };
    AVFormatContext *opened{avformat_alloc_context()};
    if (!opened) { throw std::bad_alloc{}; }
    opened->interrupt_callback.callback = [](void *opaque) {
        return static_cast<RealtimeSource *>(opaque)->stopping_.load() ? 1 : 0;
    };
    opened->interrupt_callback.opaque = this;
    const std::string path{(options_.fixture / "clip.mkv").string()};
    if (avformat_open_input(&opened, path.c_str(), nullptr, nullptr) < 0) {
        if (opened) { avformat_close_input(&opened); }
        throw std::runtime_error{"FFV1 source open failed"};
    }
    std::unique_ptr<AVFormatContext, FormatDeleter> format{opened};
    if (avformat_find_stream_info(format.get(), nullptr) < 0) {
        throw std::runtime_error{"FFV1 stream metadata failed"};
    }
    const int stream{av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)};
    if (stream < 0 || format->streams[stream]->codecpar->codec_id != AV_CODEC_ID_FFV1) {
        throw std::invalid_argument{"fixture requires FFV1 video"};
    }
    const AVCodec *decoder{avcodec_find_decoder(AV_CODEC_ID_FFV1)};
    std::unique_ptr<AVCodecContext, CodecDeleter> codec{avcodec_alloc_context3(decoder)};
    std::unique_ptr<AVFrame, FrameDeleter> frame{av_frame_alloc()};
    std::unique_ptr<AVPacket, PacketDeleter> packet{av_packet_alloc()};
    if (!codec || !frame || !packet) { throw std::bad_alloc{}; }
    if (avcodec_parameters_to_context(codec.get(), format->streams[stream]->codecpar) < 0) {
        throw std::runtime_error{"FFV1 codec parameters failed"};
    }
    codec->thread_count = 8;
    codec->thread_type = FF_THREAD_SLICE;
    if (avcodec_open2(codec.get(), decoder, nullptr) < 0) {
        throw std::runtime_error{"FFV1 decoder initialization failed"};
    }
    std::size_t count{};
    auto drain{[&] {
        while (!stopping_) {
            const int result{avcodec_receive_frame(codec.get(), frame.get())};
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) { return; }
            if (result < 0) { throw std::runtime_error{"FFV1 frame decode failed at " + std::to_string(count)}; }
            if (frame->format != AV_PIX_FMT_GRAY8 || frame->width != 1920 || frame->height != 1200
                || frame->linesize[0] < frame->width) {
                throw std::invalid_argument{"FFV1 frame must be 1920x1200 native Gray8"};
            }
            cv::Mat pixels{cv::Size{1920, 1200}, CV_8UC1};
            for (int row{}; row < frame->height; ++row) {
                std::memcpy(pixels.ptr(row), frame->data[0] + row * frame->linesize[0], 1920);
            }
            av_frame_unref(frame.get());
            if (count >= records_.size()) { throw std::runtime_error{"FFV1 contains extra frames"}; }
            std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
            unsigned digest_size{};
            if (EVP_Digest(pixels.data, pixels.total(), digest.data(), &digest_size, EVP_sha256(), nullptr) != 1) {
                throw std::runtime_error{"cached pixel SHA-256 failed"};
            }
            std::ostringstream actual{};
            actual << std::hex << std::setfill('0');
            for (unsigned index{}; index < digest_size; ++index) {
                actual << std::setw(2) << unsigned{digest[index]};
            }
            if (actual.str() != pixel_hashes_[count]) {
                throw std::runtime_error{"cached pixel checksum mismatch at frame " + std::to_string(count)};
            }
            decoded_.push_back(std::move(pixels));
            cached_bytes_ += 1920ULL * 1200;
            ++count;
        }
    }};
    while (!stopping_) {
        const int result{av_read_frame(format.get(), packet.get())};
        if (result == AVERROR_EOF) { break; }
        if (result < 0) { if (stopping_) { break; } throw std::runtime_error{"FFV1 packet read failed"}; }
        if (packet->stream_index == stream) {
            int sent{avcodec_send_packet(codec.get(), packet.get())};
            if (sent == AVERROR(EAGAIN)) { drain(); sent = avcodec_send_packet(codec.get(), packet.get()); }
            if (sent < 0 && !stopping_) { throw std::runtime_error{"FFV1 packet submission failed"}; }
            drain();
        }
        av_packet_unref(packet.get());
    }
    if (!stopping_) {
        if (avcodec_send_packet(codec.get(), nullptr) < 0) { throw std::runtime_error{"FFV1 EOF drain failed"}; }
        drain();
        if (count != records_.size()) { throw std::runtime_error{"FFV1 decoded count mismatch: " + std::to_string(count)}; }
    }

}
void RealtimeSource::supply() {
    std::size_t attitude_index{1};
    for (auto &record : records_) {
        if (stopping_) { break; }
        record.planned_ns = m0_ns_ + record.relative_us * 1000;
        record.frame_time_us = e0_us_ + record.relative_us;
        while (attitude_index < attitudes_.size()
            && attitudes_[attitude_index].first <= static_cast<std::int64_t>(record.relative_us)) {
            const auto &attitude{attitudes_[attitude_index++]};
            std::unique_lock<std::mutex> lock{mutex_};
            wake_.wait_until(lock, deadline(m0_ns_ + attitude.first * 1000), [&] { return stopping_.load(); });
            if (stopping_) { break; }
            lock.unlock();
            if (attitude_callback_) {
                auto message{std::make_shared<sensor_msgs::msg::Imu>(attitude.second)};
                message->header.stamp = rclcpp::Time{static_cast<std::int64_t>((e0_us_ + attitude.first) * 1000)};
                attitude_callback_(std::move(message));
            }
        }
        std::unique_lock<std::mutex> lock{mutex_};
        wake_.wait_until(lock, deadline(record.planned_ns), [&] { return stopping_.load(); });
        if (diagnostics::monotonicNs() >= m0_ns_ + 20000000000ULL) {
            status_.reason = "input_deadline_exceeded";
            status_.state = FrameSourceState::Failed;
            stopping_ = true;
            wake_.notify_all();
            break;
        }
        if (stopping_) { break; }
        const cv::Mat &pixels{decoded_.at(record.id)};
        lock.unlock();
        auto frame{rk3576::copyGrayFrame(pixels, record.frame_time_us)};
        lock.lock();
        record.supplied_ns = diagnostics::monotonicNs(); record.supplied = true; ++status_.read;
        const auto elapsed_us{(record.supplied_ns - m0_ns_) / 1000};
        const auto system_elapsed{systemUs() - e0_us_};
        const auto clock_delta{elapsed_us > system_elapsed ? elapsed_us - system_elapsed : system_elapsed - elapsed_us};
        if (clock_delta > options_.clock_jump_tolerance_us && status_.reason.empty()) { status_.reason = "clock_jump"; }
        if ((record.supplied_ns - record.planned_ns) / 1000 > options_.max_supply_lateness_us
            && status_.reason.empty()) { status_.reason = "supply_lateness_exceeded"; }
        if (queue_.size() < options_.queue_capacity) {
            queue_.push_back(std::move(frame)); record.enqueued = true; ++status_.enqueued;
        } else { ++status_.dropped; }
        status_.frame_index = record.id; wake_.notify_all();
    }
    while (!stopping_ && attitude_index < attitudes_.size()) {
        const auto &attitude{attitudes_[attitude_index++]};
        if (attitude.first >= 20000000) { break; }
        std::unique_lock<std::mutex> lock{mutex_};
        wake_.wait_until(lock, deadline(m0_ns_ + attitude.first * 1000), [&] { return stopping_.load(); });
        if (stopping_) { break; }
        lock.unlock();
        if (attitude_callback_) {
            auto message{std::make_shared<sensor_msgs::msg::Imu>(attitude.second)};
            message->header.stamp = rclcpp::Time{static_cast<std::int64_t>((e0_us_ + attitude.first) * 1000)};
            attitude_callback_(std::move(message));
        }
    }
    std::unique_lock<std::mutex> lock{mutex_};
    wake_.wait_until(lock, deadline(m0_ns_ + 20000000000ULL), [&] { return stopping_.load(); });
    if (!stopping_) {
        normal_eof_ = true;
        status_.state = FrameSourceState::Eof;
    }
    wake_.notify_all();
}
bool RealtimeSource::stop() {
    stopping_ = true; wake_.notify_all();
    if (supplier_.joinable()) { supplier_.join(); }

    std::lock_guard<std::mutex> lock{mutex_};
    for (const auto &frame : queue_) {
        for (auto &record : records_) {
            if (record.frame_time_us == frame->timestampUs()) { record.residual = true; }
        }
    }
    queue_.clear(); decoded_.clear();
    if ((started_ || preparation_attempted_) && !exported_) { exportRecords(); exported_ = true; }
    if (status_.state != FrameSourceState::Failed && status_.state != FrameSourceState::Eof) {
        status_.state = FrameSourceState::Stopped;
    }
    return true;
}
void RealtimeSource::exportRecords() {
    std::ofstream input{options_.output / "input.jsonl"};
    for (const auto &record : records_) {
        input << "{\"fixture_frame_id\":" << record.id << ",\"relative_us\":" << record.relative_us
            << ",\"planned_mono_ns\":" << record.planned_ns << ",\"supplied_mono_ns\":" << record.supplied_ns
            << ",\"frame_time_us\":" << record.frame_time_us << ",\"supplied\":" << (record.supplied ? "true" : "false")
            << ",\"enqueued\":" << (record.enqueued ? "true" : "false")
            << ",\"residual\":" << (record.residual ? "true" : "false") << "}\n";
    }
    std::ofstream completion{options_.output / "source-completion.json"};
    completion << "{\"normal_eof\":" << (normal_eof_ ? "true" : "false")
        << ",\"reason\":" << jsonString(status_.reason) << ",\"read\":" << status_.read
        << ",\"enqueued\":" << status_.enqueued << ",\"rejected\":" << status_.dropped << "}\n";
}
void RealtimeSource::exportPreparation() {
    struct rusage usage{};
    ::getrusage(RUSAGE_SELF, &usage);
    std::uint64_t memory_total_kib{}, memory_available_kib{}, resident_kib{};
    std::string name{}, line{};
    std::uint64_t value{};
    std::ifstream memory{"/proc/meminfo"};
    while (std::getline(memory, line)) {
        std::istringstream row{line};
        row >> name >> value;
        if (name == "MemTotal:") { memory_total_kib = value; }
        if (name == "MemAvailable:") { memory_available_kib = value; }
    }
    std::ifstream process{"/proc/self/status"};
    while (std::getline(process, line)) {
        std::istringstream row{line};
        row >> name >> value;
        if (name == "VmRSS:") { resident_kib = value; }
    }
    std::ofstream output{options_.output / "preparation.json"};
    output << "{\"schema_version\":1,\"input_mode\":\"predecode_all\",\"ready\":"
        << (prepared_ ? "true" : "false") << ",\"begin_mono_ns\":" << preparation_begin_ns_
        << ",\"end_mono_ns\":" << preparation_end_ns_ << ",\"duration_ms\":"
        << (preparation_end_ns_ ? (preparation_end_ns_ - preparation_begin_ns_) / 1000000.0 : 0)
        << ",\"verified_frame_count\":" << (prepared_ ? decoded_.size() : cached_bytes_ / (1920ULL * 1200))
        << ",\"cached_pixel_bytes\":" << cached_bytes_ << ",\"process_rss_kib\":" << resident_kib
        << ",\"process_peak_rss_kib\":" << usage.ru_maxrss << ",\"system_total_kib\":" << memory_total_kib
        << ",\"system_available_kib\":" << memory_available_kib
        << ",\"reason\":" << jsonString(status_.reason) << "}\n";
    if (!output) { throw std::runtime_error{"preparation evidence output failed"}; }
}
bool RealtimeSource::isRunning() const { return status().state == FrameSourceState::Running; }
FrameSourceStatus RealtimeSource::status() const { std::lock_guard<std::mutex> lock{mutex_}; return status_; }
bool RealtimeSource::getImageSize(int &width, int &height) const { width = 1920; height = 1200; return true; }
bool RealtimeSource::waitFrame(FrameRef &frame, std::chrono::microseconds timeout) {
    std::unique_lock<std::mutex> lock{mutex_};
    wake_.wait_for(lock, timeout, [&] { return !queue_.empty() || status_.state != FrameSourceState::Running; });
    if (queue_.empty()) { return false; }
    frame = std::move(queue_.front()); queue_.pop_front(); return true;
}
bool RealtimeSource::tryFrame(FrameRef &frame) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (queue_.empty()) { return false; }
    frame = std::move(queue_.front()); queue_.pop_front(); return true;
}
void RealtimeSource::configureRecording(bool need_save, const std::string &, const std::string &) {
    if (need_save) { throw std::invalid_argument{"regression recording must be disabled"}; }
}
} // namespace tracking_regression
