#ifndef NANOTRACK_BASELINE_RESULT_WRITER_H
#define NANOTRACK_BASELINE_RESULT_WRITER_H
#include "config.h"
#include "timing_recorder.h"
#include <fstream>
#include <map>
#include <optional>
#include <vector>
namespace baseline {
std::string jsonString(const std::string &value);
std::string csvString(const std::string &value);
std::string sha256(const std::filesystem::path &path);
std::string readText(const std::filesystem::path &path);
std::string number(double value);
struct FrameRecord {
    int repeat{}, index{};
    bool warmup{}, init{}, pts_valid{}, bbox_valid{};
    std::int64_t pts{};
    cv::Rect2f bbox{};
    std::optional<double> score{};
    std::string status{"failed"}, error{};
};
/** @brief 每帧结束后同步排空固定容量区间；写入失败抛异常交由运行器处理。 */
class ResultWriter final {
  public:
    explicit ResultWriter(const Config &config);
    void writeFrame(const FrameRecord &frame, const TimingRecorder &recorder);
    void environment(std::int64_t now, bool force = false);
    void manifest(const std::string &status, const std::string &reason);
    void diagnostic(const std::string &message);
    std::map<std::string, std::string> identity{};

  private:
    const Config &config_;
    std::ofstream frames_{}, spans_{}, environment_{}, diagnostics_{};
    std::int64_t last_environment_{-1};
    std::uint64_t serial_{};
};
} // namespace baseline
#endif // NANOTRACK_BASELINE_RESULT_WRITER_H
