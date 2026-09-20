#include "result_writer.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <sys/utsname.h>
#include <unistd.h>
namespace baseline {
std::string jsonString(const std::string &value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out << '\\' << c;
        } else if (c < 32) {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
            out << c;
        }
    }
    out << '"';
    return out.str();
}
std::string csvString(const std::string &value) {
    std::string result{"\""};
    for (const char c : value) {
        if (c == '"') {
            result += '"';
        }
        result += c;
    }
    return result + '"';
}
std::string number(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}
std::string readText(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("读取失败: " + path.string());
    }
    std::ostringstream out;
    out << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("读取失败: " + path.string());
    }
    return out.str();
}
std::string sha256(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!input || !context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("文件指纹初始化失败: " + path.string());
    }
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        if (EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(input.gcount())) != 1) {
            throw std::runtime_error("文件指纹更新失败");
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("文件指纹读取失败");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length{};
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1) {
        throw std::runtime_error("指纹结束失败");
    }
    std::ostringstream out;
    for (unsigned int i{0}; i < length; ++i) {
        out << std::hex << std::setw(2) << std::setfill('0') << int(digest[i]);
    }
    return out.str();
}
ResultWriter::ResultWriter(const Config &config) : config_{config} {
    if (!std::filesystem::create_directory(config.output)) {
        throw std::runtime_error("不能新建输出目录");
    }
    const auto open{[&](std::ofstream &out, const char *name) {
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.open(config.output / name, std::ios::binary);
    }};
    open(frames_, "frames.csv");
    open(spans_, "spans.csv");
    open(environment_, "environment.csv");
    open(diagnostics_, "diagnostics.log");
    frames_ << "schema_version,run_id,repeat_id,phase,operation,frame_index,pts,pts_valid,status,bbox_valid,"
               "x,y,width,height,score,decode_ns,frame_copy_ns,preprocess_ns,inference_ns,postprocess_ns,other_ns,"
               "decode_to_result_ns,error,bbox_kind\n";
    spans_ << "schema_version,run_id,repeat_id,phase,operation,frame_index,span_id,parent_id,stage,model_role,"
              "start_ns,end_ns,complete,residual_ns\n";
    environment_ << "schema_version,time_ns,field,value,missing_reason\n";
    frames_.flush();
    spans_.flush();
    environment_.flush();
    const auto started{
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count()};
    identity["started_unix_ns"] = std::to_string(started);
    identity["run_id"] = std::to_string(started) + "-" + std::to_string(getpid());
    identity["board"] = config.board;
    struct utsname system{};
    if (uname(&system) == 0) {
        identity["system"] = system.sysname;
        identity["kernel"] = system.release;
        identity["machine"] = system.machine;
        identity["hostname"] = system.nodename;
    } else {
        identity["system_missing_reason"] = "uname 失败";
    }
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    if (sched_getaffinity(0, sizeof(cpus), &cpus) == 0) {
        for (int i{0}; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &cpus)) {
                identity["cpu_affinity"] += std::to_string(i) + " ";
            }
        }
    } else {
        identity["cpu_affinity_missing_reason"] = "sched_getaffinity 失败";
    }
    manifest("running", "preparing");
}
void ResultWriter::diagnostic(const std::string &message) {
    diagnostics_ << message << '\n';
    diagnostics_.flush();
}
void ResultWriter::writeFrame(const FrameRecord &frame, const TimingRecorder &recorder) {
    if (!recorder.valid()) {
        throw std::runtime_error("计时区间错误或缓冲容量不足");
    }
    const std::string phase{frame.warmup ? "warmup" : "measure"};
    const std::string operation{frame.init ? "init" : "track"};
    std::ostringstream prefix;
    prefix << "1," << csvString(identity.at("run_id")) << "," << frame.repeat << ',' << phase << ',' << operation << ','
           << frame.index << ',';
    frames_ << prefix.str();
    if (frame.pts_valid) {
        frames_ << frame.pts;
    }
    frames_ << ',' << frame.pts_valid << ',' << frame.status << ',' << frame.bbox_valid << ',';
    for (const auto value : {frame.bbox.x, frame.bbox.y, frame.bbox.width, frame.bbox.height}) {
        if (frame.bbox_valid && std::isfinite(value)) {
            frames_ << number(value);
        }
        frames_ << ',';
    }
    if (frame.score && std::isfinite(*frame.score)) {
        frames_ << number(*frame.score);
    }
    frames_ << ',';
    std::int64_t stages{};
    for (const auto stage :
         {Stage::Decode, Stage::FrameCopy, Stage::Preprocess, Stage::Inference, Stage::Postprocess}) {
        const auto value{recorder.duration(stage)};
        if (value) {
            frames_ << *value;
            stages += *value;
        }
        frames_ << ',';
    }
    const auto total{recorder.duration(Stage::DecodeToResult)};
    if (total) {
        if (*total < stages) {
            throw std::runtime_error("顶层计时重复或超出总区间");
        }
        frames_ << *total - stages;
    }
    frames_ << ',';
    if (total) {
        frames_ << *total;
    }
    frames_ << ',' << csvString(frame.error) << ',' << (frame.init ? "input" : "prediction") << '\n';
    for (std::size_t i{0}; i < recorder.size(); ++i) {
        const auto &span{recorder.spans()[i]};
        std::int64_t children{};
        for (std::size_t j{0}; j < recorder.size(); ++j) {
            const auto &child{recorder.spans()[j]};
            if (child.parent == span.id) {
                children += child.end - child.start;
            }
        }
        if (children > span.end - span.start) {
            throw std::runtime_error("子区间超出父区间");
        }
        spans_ << prefix.str() << serial_ + span.id << ',';
        if (span.parent) {
            spans_ << serial_ + span.parent;
        }
        spans_ << ',' << stageName(span.stage) << ',' << modelRole(recorder, span) << ',' << span.start << ','
               << span.end << ',' << span.complete << ',' << span.end - span.start - children << '\n';
    }
    serial_ += recorder.size();
    frames_.flush();
    spans_.flush();
}
void ResultWriter::environment(std::int64_t now, bool force) {
    if (!force && last_environment_ >= 0 &&
        now - last_environment_ < std::int64_t(config_.environment_interval_ms) * 1000000) {
        return;
    }
    last_environment_ = now;
    const auto sample{[&](const std::filesystem::path &path) {
        std::string value, reason;
        try {
            value = readText(path);
        } catch (const std::exception &error) {
            reason = error.what();
        }
        environment_ << "1," << now << ',' << csvString(path.string()) << ',' << csvString(value) << ','
                     << csvString(reason) << '\n';
    }};
    for (const auto &root : {"/sys/class/thermal", "/sys/devices/system/cpu/cpufreq", "/sys/class/devfreq"}) {
        std::error_code error;
        std::filesystem::directory_iterator entries(root, error);
        if (error) {
            environment_ << "1," << now << ',' << csvString(root) << ",," << csvString(error.message()) << '\n';
            continue;
        }
        for (const auto &entry : entries) {
            const std::string name{entry.path().filename().string()};
            if (name.rfind("thermal_zone", 0) == 0) {
                sample(entry.path() / "type");
                sample(entry.path() / "temp");
            } else if (name.rfind("policy", 0) == 0) {
                for (const char *field :
                     {"scaling_governor", "scaling_cur_freq", "scaling_min_freq", "scaling_max_freq"}) {
                    sample(entry.path() / field);
                }
            } else if (std::string(root) == "/sys/class/devfreq") {
                for (const char *field : {"governor", "cur_freq", "min_freq", "max_freq"}) {
                    sample(entry.path() / field);
                }
            }
        }
    }
    environment_.flush();
}
void ResultWriter::manifest(const std::string &status, const std::string &reason) {
    const auto temporary{config_.output / "manifest.tmp"};
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(temporary);
    out << "{\n\"schema_version\":1,\"status\":" << jsonString(status) << ",\"reason\":" << jsonString(reason)
        << ",\"config\":{\"schema_version\":1,\"source\":" << jsonString(config_.source.string())
        << ",\"video\":" << jsonString(config_.video.string()) << ",\"output\":" << jsonString(config_.output.string())
        << ",\"precision_evidence\":" << jsonString(config_.evidence.string()) << ",\"models\":[";
    for (int i{0}; i < 3; ++i) {
        if (i) {
            out << ',';
        }
        out << jsonString(config_.models[i].string());
    }
    out << "],\"bbox\":[" << number(config_.bbox.x) << ',' << number(config_.bbox.y) << ','
        << number(config_.bbox.width) << ',' << number(config_.bbox.height)
        << "],\"warmup_updates\":" << config_.warmup_updates << ",\"max_updates\":" << config_.max_updates
        << ",\"repeats\":" << config_.repeats << ",\"environment_interval_ms\":" << config_.environment_interval_ms
        << ",\"detailed_observation\":" << (config_.detailed ? "true" : "false")
        << ",\"board\":" << jsonString(config_.board) << "},\"identity\":{";
    bool first{true};
    for (const auto &entry : identity) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << jsonString(entry.first) << ':' << jsonString(entry.second);
    }
    out << "}}\n";
    out.close();
    std::filesystem::rename(temporary, config_.output / "manifest.json");
}
} // namespace baseline
