#include "metadata.h"

#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>

extern char **environ;
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>

#include <yaml-cpp/yaml.h>

namespace {
using rk3576::recording::quote;
struct Lines {
    std::uint64_t complete{};
    bool damaged{};
    YAML::Node last{};
};
YAML::Node readObject(const std::filesystem::path &path) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)
        || std::filesystem::file_size(path) > 131072) { return {}; }
    std::ifstream file{path, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto begin{text.find_first_not_of(" \r\n\t")};
    const auto end{text.find_last_not_of(" \r\n\t")};
    if (begin == std::string::npos || text[begin] != '{' || text[end] != '}') { return {}; }
    try {
        const auto parsed{YAML::Load(text)};
        return parsed.IsMap() ? parsed : YAML::Node{};
    } catch (const std::exception &) { return {}; }
}
/** @brief 单行最多 64 KiB、最多一百万行；损坏或截断尾部停止，不修复原件。 */
Lines checkLines(const std::filesystem::path &path, const std::function<void(const YAML::Node &, std::uint64_t)> &visit = {}) {
    Lines out{};
    std::ifstream file{path, std::ios::binary};
    if (!file) { out.damaged = true; return out; }
    std::string line{}; line.reserve(65536);
    char byte{};
    while (file.get(byte)) {
        if (byte != '\n') {
            if (line.size() >= 65536) { out.damaged = true; break; }
            line += byte; continue;
        }
        if (++out.complete > 1000000) { out.damaged = true; break; }
        try {
            if (line.empty() || line.front() != '{' || line.back() != '}') {
                throw std::runtime_error("not a JSON object");
            }
            const auto candidate{YAML::Load(line)};
            if (!candidate.IsMap()) { throw std::runtime_error("not an object"); }
            out.last = candidate;
            if (visit) { visit(candidate, out.complete); }
        }
        catch (...) { --out.complete; out.damaged = true; break; }
        line.clear();
        if (out.complete % 1024 == 0) { std::this_thread::sleep_for(std::chrono::milliseconds{1}); }
    }
    if (!line.empty() || file.bad()) { out.damaged = true; }
    return out;
}
std::uint64_t packetPrefix(const std::filesystem::path &path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) { throw std::runtime_error("Cannot read TS evidence"); }
    std::array<char, 188> packet{};
    std::uint64_t prefix{};
    while (file.read(packet.data(), packet.size())) {
        if (static_cast<unsigned char>(packet[0]) != 0x47) { break; }
        prefix += packet.size();
        if (prefix % (188 * 4096) == 0) { std::this_thread::sleep_for(std::chrono::milliseconds{10}); }
    }
    if (file.bad()) { throw std::runtime_error("TS evidence read failed"); }
    return prefix;
}
/** @brief Use the installed general decoder, with bounded output and a 180-second process budget. */
std::uint64_t decodedFrames(const std::filesystem::path &path) {
    std::array<int, 2> pipe{};
    if (::pipe2(pipe.data(), O_CLOEXEC | O_NONBLOCK)) { throw std::runtime_error("Decoder pipe failed"); }
    posix_spawn_file_actions_t actions{};
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, pipe[1], STDOUT_FILENO);
    const auto input{path.string()};
    const char *arguments[]{"ffmpeg", "-v", "error", "-xerror", "-nostdin", "-i", input.c_str(),
        "-map", "0:v:0", "-vsync", "0", "-progress", "pipe:1", "-nostats", "-f", "null", "-", nullptr};
    pid_t pid{};
    const int spawned{::posix_spawnp(&pid, "ffmpeg", &actions, nullptr,
        const_cast<char **>(arguments), environ)};
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(pipe[1]);
    if (spawned) { ::close(pipe[0]); throw std::runtime_error("ffmpeg unavailable for video readback"); }
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{180}};
    std::string output{};
    int status{};
    bool exited{};
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<char, 4096> buffer{};
        const auto size{::read(pipe[0], buffer.data(), buffer.size())};
        if (size > 0) {
            output.append(buffer.data(), size);
            if (output.size() > 1048576) { break; }
        }
        if (::waitpid(pid, &status, WNOHANG) == pid) { exited = true; break; }
        pollfd wait{pipe[0], POLLIN, 0};
        ::poll(&wait, 1, 20);
    }
    // Drain the final progress record after process exit.
    std::array<char, 4096> buffer{};
    ssize_t size{};
    while ((size = ::read(pipe[0], buffer.data(), buffer.size())) > 0 && output.size() <= 1048576) {
        output.append(buffer.data(), size);
    }
    ::close(pipe[0]);
    if (!exited) { ::kill(pid, SIGKILL); ::waitpid(pid, &status, 0); }
    if (!exited || !WIFEXITED(status) || WEXITSTATUS(status) || output.size() > 1048576) {
        throw std::runtime_error("Video readback failed or exceeded its bound");
    }
    const auto key{output.rfind("frame=")};
    if (key == std::string::npos || output.find("progress=end") == std::string::npos) {
        throw std::runtime_error("Decoder returned no completed frame count");
    }
    return std::stoull(output.substr(key + 6));
}
std::string inspectSession(const std::filesystem::path &path, bool verify_video) {
    const auto commits{checkLines(path / "commits.jsonl")};
    const auto events{checkLines(path / "events.jsonl")};
    const auto manifest{readObject(path / "session.json")};
    const auto completion{readObject(path / "complete.json")};
    const auto camera{readObject(path / "camera.json")};
    const auto startup{camera.IsMap() && camera["startup"] ? camera["startup"] : YAML::Node{}};
    std::array<YAML::Node, 2> sources{{startup.IsMap() && startup["first_yolo"] ? startup["first_yolo"] : YAML::Node{},
                                    startup.IsMap() && startup["first_initialization"] ? startup["first_initialization"] : YAML::Node{}}};
    std::array<std::string, 2> coverage{{"unknown", "unknown"}};
    std::array<unsigned, 2> matches{};
    std::array<bool, 2> readable{};
    if (startup.IsMap() && startup["request_generation"] && startup["request_generation"].as<std::uint64_t>()) {
        for (std::size_t i{}; i < sources.size(); ++i) {
            const char *source_key{i ? "first_initialization" : "first_yolo"};
            if (!startup[source_key]) { continue; }
            if (sources[i].IsMap()) {
                bool valid{true};
                for (const char *key : {"stream_id", "tracking_round", "request_generation", "capture_id",
                                         "timestamp_us", "sequence", "accepted"}) {
                    if (!sources[i][key] || !sources[i][key].IsScalar()) { valid = false; }
                }
                if (valid) { coverage[i] = sources[i]["accepted"].as<bool>() ? "accepted" : "failed"; }
                else { sources[i] = YAML::Node{}; }
            }
            else if (sources[i].IsNull()) { coverage[i] = "not-applicable"; }
        }
    }
    std::ostringstream out{};
    out << "{\"session\":" << quote(path.filename().string())
        << ",\"normal_marker_present\":" << (std::filesystem::exists(path / "complete.json") ? "true" : "false")
        << ",\"manifest_valid_object\":" << (manifest.IsMap() ? "true" : "false")
        << ",\"completion_valid_object\":" << (completion.IsMap() ? "true" : "false")
        << ",\"camera_summary_valid_object\":" << (camera.IsMap() ? "true" : "false")
        << ",\"event_tail_damaged\":" << (events.damaged ? "true" : "false")
        << ",\"complete_event_records\":" << events.complete
        << ",\"complete_commit_records\":" << commits.complete << ",\"commit_tail_damaged\":"
        << (commits.damaged ? "true" : "false") << ",\"prior_sync_success_verified\":false"
        << ",\"decoded\":false,\"last_parseable_commit\":";
    if (commits.last && commits.last["segment"] && commits.last["video_end"] && commits.last["last_frame_us"]) {
        try {
            const auto segment{commits.last["segment"].as<unsigned>()};
            const auto end{commits.last["video_end"].as<std::uint64_t>()};
            const auto timestamp{commits.last["last_frame_us"].as<std::int64_t>()};
            std::ostringstream name{};
            name << std::setw(6) << std::setfill('0') << segment << ".ts";
            const auto video{path / name.str()};
            const bool boundary_exists{std::filesystem::is_regular_file(video)
                && !std::filesystem::is_symlink(video) && end <= std::filesystem::file_size(video)
                && end % 188 == 0};
            out << "{\"segment\":" << segment << ",\"video_end\":" << end
                << ",\"last_frame_us\":" << timestamp << ",\"video_boundary_exists\":"
                << (boundary_exists ? "true" : "false") << '}';
        } catch (...) { out << "null"; }
    } else { out << "null"; }
    out << ",\"files\":[";
    bool first{true}; std::size_t count{};
    for (const auto &entry : std::filesystem::directory_iterator{path}) {
        if (++count > 10000) { throw std::runtime_error("Session inspection exceeds 10000 files"); }
        if (entry.is_symlink()) { throw std::runtime_error("Symlink in recording evidence rejected"); }
        if (!entry.is_regular_file()) { continue; }
        const auto name{entry.path().filename().string()};
        const bool video{entry.path().extension() == ".ts"};
        const bool frames{name.size() >= 13 && name.substr(name.size() - 13) == ".frames.jsonl"};
        if (!video && !frames) { continue; }
        if (!first) { out << ','; } first = false;
        out << "{\"name\":" << quote(name) << ",\"bytes\":" << entry.file_size();
        if (video) { out << ",\"ts_packet_prefix_bytes\":" << packetPrefix(entry.path()); }
        else {
            std::array<bool, 2> segment_matches{};
            const auto lines{checkLines(entry.path(), [&](const YAML::Node &frame, std::uint64_t) {
                for (std::size_t i{}; i < sources.size(); ++i) {
                    if (!sources[i].IsMap()) { continue; }
                    bool same{true};
                    for (const char *key : {"stream_id", "tracking_round", "request_generation", "capture_id",
                                             "timestamp_us", "sequence"}) {
                        if (!frame[key] || !sources[i][key]) {
                            same = false; break;
                        }
                        if (frame[key].as<std::uint64_t>() != sources[i][key].as<std::uint64_t>()) { same = false; }
                    }
                    for (const char *key : {"stream_id", "tracking_round", "request_generation"}) {
                        if (!manifest[key] || !startup[key] || sources[i][key].as<std::uint64_t>()
                            != startup[key].as<std::uint64_t>() || sources[i][key].as<std::uint64_t>()
                            != manifest[key].as<std::uint64_t>()) { same = false; }
                    }
                    if (!sources[i]["accepted"] || !sources[i]["accepted"].as<bool>()) { same = false; }
                    if (same) { ++matches[i]; segment_matches[i] = true; }
                }
            })};
            if (verify_video && !lines.damaged && (segment_matches[0] || segment_matches[1])) {
                const auto video{entry.path().parent_path() / (name.substr(0, name.size() - 13) + ".ts")};
                if (!std::filesystem::is_symlink(video) && decodedFrames(video) == lines.complete) {
                    for (std::size_t i{}; i < sources.size(); ++i) { readable[i] = readable[i] || segment_matches[i]; }
                }
            }
            out << ",\"complete_records\":" << lines.complete << ",\"tail_damaged\":" << (lines.damaged ? "true" : "false");
        }
        out << '}';
        if (out.tellp() > 1024 * 1024) { throw std::runtime_error("Session inspection report exceeds 1 MiB"); }
    }
    out << "],\"startup_coverage\":{";
    for (std::size_t i{}; i < sources.size(); ++i) {
        if (sources[i].IsMap()) {
            if (matches[i] == 1) { coverage[i] = readable[i] ? "verified-written" : "metadata-matched"; }
            if (matches[i] > 1 || (matches[i] == 0 && completion.IsMap())) { coverage[i] = "failed"; }
            if (completion.IsMap() && completion["video_success"] && !completion["video_success"].as<bool>()) {
                coverage[i] = "failed";
            }
            if (startup["outcome"] && startup["outcome"].as<std::string>() == "unavailable") {
                coverage[i] = "failed";
            }
        }
        if (i) { out << ','; }
        out << quote(i ? "first_initialization" : "first_yolo") << ':' << quote(coverage[i]);
    }
    out << "}}\n";
    return out.str();
}
} // namespace
int main(int argc, char **argv) {
    int fd{-1};
    try {
        if (argc != 3 && (argc != 4 || std::string{argv[3]} != "--verify-video")) {
            throw std::invalid_argument("usage: recording_inspect DIRECTORY NEW_REPORT.jsonl [--verify-video]");
        }
        const std::filesystem::path root{argv[1]}, report{argv[2]};
        if (!root.is_absolute() || !report.is_absolute()) { throw std::invalid_argument("Use absolute paths"); }
        fd = ::open(report.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) { throw std::runtime_error("Report must be a new exclusive file"); }
        std::size_t count{}, report_bytes{};
        for (const auto &entry : std::filesystem::directory_iterator{root}) {
            if (++count > 100000) { throw std::runtime_error("Inspection exceeds 100000 root entries"); }
            if (entry.is_symlink()) { throw std::runtime_error("Symlink in recording root rejected"); }
            if (!entry.is_directory() || entry.path().filename().string().rfind("session-", 0) != 0) { continue; }
            std::string line{};
            try { line = inspectSession(entry.path(), argc == 4); }
            catch (const std::exception &error) {
                line = "{\"session\":" + quote(entry.path().filename().string()) + ",\"inspection_error\":" + quote(error.what()) + "}\n";
            }
            if (line.size() > 16 * 1024 * 1024 - report_bytes) {
                throw std::runtime_error("Inspection report exceeds 16 MiB; inspect fewer sessions");
            }
            report_bytes += line.size();
            std::size_t offset{};
            while (offset < line.size()) {
                const auto result{::write(fd, line.data() + offset, line.size() - offset)};
                if (result < 0 && errno == EINTR) { continue; }
                if (result <= 0) { throw std::runtime_error("Inspection report write failed"); }
                offset += result;
            }
        }
        if (::fdatasync(fd)) { throw std::runtime_error("Inspection report sync failed"); }
        ::close(fd); fd = -1;
        const int parent{::open(report.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
        if (parent < 0) { throw std::runtime_error("Inspection report parent unavailable"); }
        const int result{::fsync(parent)}; ::close(parent);
        if (result) { throw std::runtime_error("Inspection report directory sync failed"); }
        std::cout << "Inspection complete; originals unchanged; see per-source startup coverage\n";
        return 0;
    } catch (const std::exception &error) {
        if (fd >= 0) { ::close(fd); }
        std::cerr << error.what() << '\n'; return 1;
    }
}
