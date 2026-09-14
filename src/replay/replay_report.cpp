#include "replay_report.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

#ifndef REPLAY_GIT_COMMIT
#define REPLAY_GIT_COMMIT "unknown"
#endif
#ifndef REPLAY_GIT_DIRTY
#define REPLAY_GIT_DIRTY "unknown"
#endif
namespace replay {
namespace {
void checkStream(std::ostream &stream) {
    stream.flush();
    if (!stream) {
        throw std::runtime_error("Report output write failed");
    }
}
void box(std::ostream &out, const BBoxRect &b) {
    out << ',' << (b.is_valid ? 1 : 0);
    if (b.is_valid) {
        out << ',' << b.cx << ',' << b.cy << ',' << b.width << ',' << b.height;
    } else {
        out << ",,,,";
    }
}
// OpenCV 不支持 int64 原生输出；报告 schema 规定所有计数和时间整数均用十进制字符串。
void integer(cv::FileStorage &out, const std::string &key, std::uint64_t value) { out << key << std::to_string(value); }
} // namespace
std::string sha256(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Cannot fingerprint file: " + path.string());
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 initialization failed");
    }
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        if (EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(input.gcount())) != 1) {
            throw std::runtime_error("SHA-256 update failed");
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Fingerprint read failed: " + path.string());
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size{};
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &size) != 1) {
        throw std::runtime_error("SHA-256 failed");
    }
    std::ostringstream result{};
    result << std::hex << std::setfill('0');
    for (unsigned int i{0}; i < size; ++i) {
        result << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return result.str();
}
std::string csvEscape(const std::string &text) {
    std::string result{"\""};
    for (char ch : text) {
        if (ch == '"') {
            result += '"';
        }
        result += ch;
    }
    return result + '"';
}
ReplayReport::ReplayReport(ReplayConfig config) : config_{std::move(config)} {
    if (!std::filesystem::create_directories(config_.directory)) {
        throw std::runtime_error("output.directory already exists");
    }
    csv_.open(config_.directory / "frames.csv");
    log_.open(config_.directory / "diagnostics.log", std::ios::app);
    csv_ << "frame_index,timestamp_us,now_us,consumed,tracking,state,display_valid,display_cx,display_cy,"
            "display_width,display_height,realtime_valid,realtime_cx,realtime_cy,realtime_width,realtime_height,"
            "nano_valid,nano_x,nano_y,nano_width,nano_height,nano_score,info,decode_ms,convert_copy_ms,update_ms,"
            "wait_ms,submit_ns,interval_ms,lag_ms\n";
    csv_ << std::setprecision(17);
    checkStream(csv_);
    checkStream(log_);
    summary("running", 0, 0);
}
void ReplayReport::identify(const std::filesystem::path &executable) {
    for (const auto &entry :
         std::map<std::string, std::filesystem::path>{{"program", executable},
                                                      {"configuration", config_.file},
                                                      {"yolo", config_.models.yolo},
                                                      {"template_backbone", config_.models.template_backbone},
                                                      {"search_backbone", config_.models.search_backbone},
                                                      {"head", config_.models.head},
                                                      {"video", config_.video}}) {
        hashes_[entry.first] = sha256(entry.second);
    }
    summary("running", 0, 0);
}
void ReplayReport::input(VideoMetadata metadata) {
    metadata_ = std::move(metadata);
    if (!std::isfinite(metadata_.fps) || metadata_.fps <= 0) {
        diagnostic("Video FPS metadata missing; using configuration");
    }
    summary("running", 0, 0);
}
void ReplayReport::models(std::string description) {
    description_ = std::move(description);
    summary("running", 0, 0);
}
void ReplayReport::frame(const FrameRecord &r) {
    csv_ << r.index << ',' << r.timestamp_us << ',' << r.timestamp_us << ',' << r.output.consumed << ','
         << r.output.tracking << ',' << static_cast<int>(r.output.state);
    box(csv_, r.output.display_box);
    box(csv_, r.output.realtime_box);
    const bool valid{r.nano.width > 0 && r.nano.height > 0 && std::isfinite(r.nano.x) && std::isfinite(r.nano.y) &&
                     std::isfinite(r.nano.width) && std::isfinite(r.nano.height)};
    csv_ << ',' << valid;
    if (valid) {
        csv_ << ',' << r.nano.x << ',' << r.nano.y << ',' << r.nano.width << ',' << r.nano.height;
    } else {
        csv_ << ",,,,";
    }
    csv_ << ',';
    if (std::isfinite(r.nano_score)) {
        csv_ << r.nano_score;
    }
    csv_ << ',' << csvEscape(r.output.info) << ',' << r.decode_ms << ',' << r.convert_copy_ms << ',' << r.update_ms
         << ',' << r.wait_ms << ',' << r.submit_ns << ',' << r.interval_ms << ',' << r.lag_ms << '\n';
    checkStream(csv_);
    ++submitted_;
    if (r.output.consumed) {
        ++accepted_;
    }
    const int state{static_cast<int>(r.output.state)};
    ++states_.at(static_cast<std::size_t>(state));
    if (transitions_.empty() || transitions_.back().second != state) {
        transitions_.emplace_back(r.index, state);
    }
    const std::array<double, 6> values{r.decode_ms, r.convert_copy_ms, r.update_ms, r.wait_ms, r.interval_ms, r.lag_ms};
    // 仅保留计时样本用于精确 P95，不保留图像或完整结果行。
    for (std::size_t i{0}; i < 6; ++i) {
        timings_[i].push_back(values[i]);
    }
}
void ReplayReport::diagnostic(const std::string &message) {
    log_ << message << '\n';
    checkStream(log_);
}
void ReplayReport::summary(const std::string &reason, std::uint64_t read_count, double elapsed_ms) {
    cv::FileStorage out{".json", cv::FileStorage::WRITE | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON};
    out << "report_version" << 1 << "integer_encoding" << "decimal strings for counters, frame indices and timestamps"
        << "status" << reason << "end_reason" << reason << "platform" << "rk3576"
        << "git_commit" << REPLAY_GIT_COMMIT << "git_dirty_at_build" << REPLAY_GIT_DIRTY << "opencv_version"
        << CV_VERSION << "model_runtime_attributes" << "[";
    std::istringstream description{description_};
    std::string attribute{};
    while (std::getline(description, attribute)) {
        // 每行独立保存，避免 OpenCV 对长字符串的内部长度限制。
        for (std::size_t offset{0}; offset < attribute.size(); offset += 1000) {
            out << attribute.substr(offset, 1000);
        }
    }
    out << "]";
    out << "sha256" << "{";
    for (const auto &hash : hashes_) {
        out << hash.first << hash.second;
    }
    out << "}" << "configuration" << "{";
    writeConfig(out, config_);
    out << "}" << "input" << "{" << "width" << metadata_.width << "height" << metadata_.height << "decoder"
        << metadata_.backend << "metadata_fps_available"
        << static_cast<int>(std::isfinite(metadata_.fps) && metadata_.fps > 0) << "metadata_fps"
        << (std::isfinite(metadata_.fps) ? metadata_.fps : 0);
    integer(out, "declared_frames", metadata_.declared_frames);
    integer(out, "read_frames", read_count);
    integer(out, "submitted_frames", submitted_);
    integer(out, "accepted_frames", accepted_);
    out << "}" << "state_counts" << "{";
    for (std::size_t i{0}; i < states_.size(); ++i) {
        integer(out, "state_" + std::to_string(i), states_[i]);
    }
    out << "}" << "state_transitions" << "[";
    for (const auto &entry : transitions_) {
        out << "{";
        integer(out, "frame_index", entry.first);
        out << "state" << entry.second << "}";
    }
    out << "]" << "elapsed_wall_ms" << elapsed_ms << "timing_note"
        << "ms; decode/convert_copy/update exclude wait; asynchronous YOLO is not total update time"
        << "timings" << "{";
    const std::array<const char *, 6> names{"decode", "convert_copy", "update", "wait", "interval", "lag"};
    for (std::size_t i{0}; i < names.size(); ++i) {
        out << names[i] << "{";
        if (timings_[i].empty()) {
            // FileStorage 无 null 标量接口；先用唯一哨兵，序列化后替换为 JSON null。
            out << "mean_ms" << "__REPLAY_NULL__" << "p95_ms" << "__REPLAY_NULL__";
        } else {
            auto values{timings_[i]};
            std::sort(values.begin(), values.end());
            long double sum{};
            for (double value : values) {
                sum += value;
            }
            out << "mean_ms" << static_cast<double>(sum / values.size()) << "p95_ms"
                << values[static_cast<std::size_t>(std::ceil(0.95L * values.size())) - 1];
        }
        out << "}";
    }
    out << "}";
    auto json{out.releaseAndGetString()};
    for (const std::string key : {"mean_ms", "p95_ms"}) {
        const std::string marker{"\"" + key + "\": \"__REPLAY_NULL__\""};
        const std::string replacement{"\"" + key + "\": null"};
        for (std::size_t at{json.find(marker)}; at != std::string::npos;
             at = json.find(marker, at + replacement.size())) {
            json.replace(at, marker.size(), replacement);
        }
    }
    const auto temporary{config_.directory / "run.json.tmp"};
    std::ofstream file{temporary, std::ios::trunc};
    file << json;
    checkStream(file);
    file.close();
    if (!file) {
        throw std::runtime_error("Report close failed");
    }
    std::filesystem::rename(temporary, config_.directory / "run.json");
}
void ReplayReport::finish(const std::string &reason, std::uint64_t read_count, double elapsed_ms) {
    checkStream(csv_);
    checkStream(log_);
    csv_.close();
    log_.close();
    if (!csv_ || !log_) {
        throw std::runtime_error("Report close failed");
    }
    summary(reason, read_count, elapsed_ms);
}
DiagnosticCapture::DiagnosticCapture(const std::filesystem::path &path) {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    stdout_ = ::dup(STDOUT_FILENO);
    stderr_ = ::dup(STDERR_FILENO);
    log_ = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
    if (stdout_ < 0 || stderr_ < 0 || log_ < 0 || ::dup2(log_, STDOUT_FILENO) < 0 || ::dup2(log_, STDERR_FILENO) < 0) {
        if (stdout_ >= 0) {
            ::dup2(stdout_, STDOUT_FILENO);
            ::close(stdout_);
        }
        if (stderr_ >= 0) {
            ::dup2(stderr_, STDERR_FILENO);
            ::close(stderr_);
        }
        if (log_ >= 0) {
            ::close(log_);
        }
        throw std::runtime_error("Cannot capture diagnostics");
    }
}
void DiagnosticCapture::check() {
    std::cout.flush();
    std::cerr.flush();
    if (std::fflush(nullptr) != 0 || std::ferror(stdout) || std::ferror(stderr) || !std::cout || !std::cerr ||
        ::fsync(log_) != 0) {
        throw std::runtime_error("Diagnostic output write failed");
    }
}
DiagnosticCapture::~DiagnosticCapture() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    ::dup2(stdout_, STDOUT_FILENO);
    ::dup2(stderr_, STDERR_FILENO);
    ::close(stdout_);
    ::close(stderr_);
    ::close(log_);
    std::clearerr(stdout);
    std::clearerr(stderr);
    std::cout.clear();
    std::cerr.clear();
}
} // namespace replay
