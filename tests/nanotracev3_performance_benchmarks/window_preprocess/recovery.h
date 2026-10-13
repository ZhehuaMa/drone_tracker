#ifndef WINDOW_PREPROCESS_RECOVERY_H
#define WINDOW_PREPROCESS_RECOVERY_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dcf_tracker.h"
#include "nano_tracker.h"
#include "timing_observer.h"
#include "whole_frame_preprocessor.h"

namespace window_preprocess {
/** @brief 仅在计时外运行真实恢复；逐次精确核对实际几何产生的新旧模型输入。 */
class ExactPreprocessor final : public ::NanoTrackPreprocessor {
public:
    PreparedInputResult prepareTemplate(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                        const ModelInputContract &contract) override;
    PreparedInputResult prepareSearch(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                      const ModelInputContract &contract) override;
    void reset() noexcept override;
    const std::vector<int> &sizes() const noexcept { return sizes_; }
    std::size_t elements() const noexcept { return elements_; }
private:
    PreparedInputResult checked(PreparedInputResult current, const PreparedInputResult &reference, int size);
    rk3576::NanoTrackPreprocessor current_{};
    WholeFramePreprocessor reference_{};
    std::vector<int> sizes_{};
    std::size_t elements_{};
};
/** @brief 一个阶段的起止时间；parent=0 表示直接属于本帧恢复操作。 */
struct Interval {
    std::uint32_t id{}, parent{};
    timing::Stage stage{};
    std::int64_t start{}, end{};
    bool complete{};
};
/** @brief 回调不分配内存；完整性错误导致测量失败。 */
class Recorder final : public timing::Observer {
public:
    using Clock = std::int64_t (*)() noexcept;
    explicit Recorder(Clock clock = now) noexcept : clock_{clock} {}
    static std::int64_t now() noexcept;
    std::uint32_t begin(timing::Stage stage) noexcept override;
    void end(std::uint32_t id, bool complete) noexcept override;
    bool valid() const noexcept { return valid_ && top_ == 0; }
    const auto &intervals() const noexcept { return intervals_; }
    std::size_t size() const noexcept { return size_; }
    std::int64_t duration(timing::Stage stage) const;
    int count(timing::Stage stage) const;
    /** @brief 实机按 rknn_run 区间及祖先区分模型；无 Run 的受控替身按模型区间计数。 */
    int modelCalls(timing::Stage stage) const;
private:
    std::array<Interval, 256> intervals_{};
    Clock clock_{};
    std::size_t size_{};
    std::uint32_t top_{};
    bool valid_{true};
};
struct FrameResult {
    std::uint64_t timestamp{};
    bool success{}, initialized{};
    cv::Rect2f box{};
    float score{};
    std::int64_t elapsed{};
    Recorder recorder{};
};
struct BatchTag { bool warmup{}, legacy{}; int pair{}, order{}; };
std::vector<BatchTag> plan(int warmups, int pairs);
struct Batch {
    bool warmup{}, legacy{};
    int pair{}, order{};
    std::vector<FrameResult> frames{};
    std::int64_t elapsed{}, preprocess{};
};
/** @brief 复用真实 NanoDcfTracker，严格执行一次初始化和其后的全部帧更新。 */
Batch recover(NanoDcfTracker &tracker, const std::vector<FrameRef> &frames, const cv::Rect2f &box,
              bool observed = true);
void compare(const Batch &old_batch, const Batch &new_batch, bool check_calls = true);
struct Distribution { double median{}, p95{}; };
Distribution distribution(std::vector<std::int64_t> samples);
/** @brief 性能门槛的统一结论；报告、状态文件和进程退出码由同一次判定生成。 */
struct PerformanceVerdict {
    bool passed{};
    std::string report{}, status{};
    int exit_code{};
};
/** @brief 保持预处理不高于 20%、恢复中位数严格降低、恢复 P95 不增加的门槛。 */
PerformanceVerdict performanceVerdict(const Distribution &old_recovery, const Distribution &new_recovery,
                                      const Distribution &old_preprocess, const Distribution &new_preprocess);
std::vector<std::int64_t> formalSamples(const std::vector<Batch> &batches, bool legacy, bool preprocessing);
struct Config {
    std::filesystem::path source{}, manifest{}, output{}, environment{};
    std::array<std::filesystem::path, 3> models{};
    std::array<std::string, 3> model_hashes{};
    cv::Rect2f box{};
    NanoDcfTracker::Params parameters{};
    int warmups{2}, pairs{10}, cpu{-1}, threads{1};
    std::string board{};
};
/** @brief 创建全新输出目录；已有目录或文件一律拒绝。 */
void createOutputDirectory(const std::filesystem::path &path);
Config loadConfig(const std::filesystem::path &path);
/** @brief 只读加载清单中的 25 张 Gray8 图；核对解码像素而非压缩文件字节。 */
std::vector<FrameRef> loadFrames(const Config &config);
std::string pixelHash(const cv::Mat &gray);
} // namespace window_preprocess
#endif // WINDOW_PREPROCESS_RECOVERY_H
