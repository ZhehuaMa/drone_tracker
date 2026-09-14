#ifndef REPLAY_REPORT_H
#define REPLAY_REPORT_H
#include <array>
#include <fstream>
#include <map>
#include <vector>
#include "replay_config.h"
#include "replay_reader.h"
namespace replay {
std::string sha256(const std::filesystem::path &path);
std::string csvEscape(const std::string &text);
/** @brief CSV 中整数直接写入；时间统计单位为毫秒，提交时刻单位为纳秒。 */
struct FrameRecord {
    std::uint64_t index{};
    std::int64_t timestamp_us{};
    TrackerOutput output{};
    cv::Rect2f nano{};
    float nano_score{-1};
    double decode_ms{};
    double convert_copy_ms{};
    double update_ms{};
    double wait_ms{};
    std::int64_t submit_ns{};
    double interval_ms{};
    double lag_ms{};
};
class ReplayReport {
public:
    explicit ReplayReport(ReplayConfig config);
    void identify(const std::filesystem::path &executable);
    void input(VideoMetadata metadata);
    void models(std::string description);
    void frame(const FrameRecord &record);
    void diagnostic(const std::string &message);
    void finish(const std::string &reason, std::uint64_t read_count, double elapsed_ms);
    const std::filesystem::path &directory() const noexcept { return config_.directory; }

private:
    void summary(const std::string &reason, std::uint64_t read_count, double elapsed_ms);
    ReplayConfig config_;
    VideoMetadata metadata_{};
    std::map<std::string, std::string> hashes_{};
    std::string description_{};
    std::ofstream csv_{};
    std::ofstream log_{};
    std::array<std::vector<double>, 6> timings_{};
    std::array<std::uint64_t, 4> states_{};
    std::vector<std::pair<std::uint64_t, int>> transitions_{};
    std::uint64_t submitted_{};
    std::uint64_t accepted_{};
};
/** @brief 同时捕获 C/C++ stdout/stderr；仅主流程创建，检测线程结束后恢复。 */
class DiagnosticCapture {
public:
    explicit DiagnosticCapture(const std::filesystem::path &path);
    ~DiagnosticCapture();
    void check();
    DiagnosticCapture(const DiagnosticCapture &) = delete;
    DiagnosticCapture &operator=(const DiagnosticCapture &) = delete;

private:
    int stdout_{-1};
    int stderr_{-1};
    int log_{-1};
};
} // namespace replay
#endif // REPLAY_REPORT_H
