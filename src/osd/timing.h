#ifndef OSD_TIMING_H
#define OSD_TIMING_H

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace osd {
using TimingClock = std::chrono::steady_clock;
enum class TimingStage : std::size_t {
    SourceCopy, ScaleConvert, Layout, Compose, Commit, FlipWait, RenderTotal,
    PendingWait, SubmitToDisplay, NewFrameInterval, Other, Count
};
enum class TimingResult : std::size_t { Success, Rejected, Failure, Count };
constexpr std::size_t TIMING_STAGES{static_cast<std::size_t>(TimingStage::Count)};
/** @brief 固定直方图，末桶表示大于 2^26 微秒，分位数返回桶上界。 */
struct TimingHistogram {
    std::uint64_t count{}, max_us{};
    long double total_us{};
    std::array<std::uint64_t, 28> buckets{};
    void add(std::uint64_t microseconds);
    std::optional<std::uint64_t> percentile(unsigned percent) const;
};
/** @brief 单次渲染的线程内记录；未执行的阶段保持无值。 */
struct TimingSample {
    std::array<std::optional<std::uint64_t>, TIMING_STAGES> stages{};
    void add(TimingStage stage, TimingClock::duration duration);
};
/** @brief 异常展开时同样完成当前阶段计时；空记录不读时钟。 */
class TimingSpan {
public:
    TimingSpan(TimingSample *sample, TimingStage stage,
               const std::function<TimingClock::time_point()> &now);
    ~TimingSpan();
    void next(TimingStage stage);
    void finish();
private:
    TimingSample *sample_{};
    TimingStage stage_{};
    const std::function<TimingClock::time_point()> &now_;
    TimingClock::time_point start_{};
};
struct TimingGroup {
    std::uint64_t count{}, first_frames{};
    std::array<TimingHistogram, TIMING_STAGES> stages{};
};
struct TimingWindow {
    std::uint64_t round{}, id{};
    TimingClock::time_point start{}, end{};
    std::array<TimingGroup, 6> groups{};
    std::uint64_t submitted{}, replaced{}, displayed{}, rejected{}, commits{};
    void add(const TimingSample &sample, bool new_video, TimingResult result, bool first);
};
/** @brief 只在快照锁外调用，返回固定规模日志。 */
std::string formatTiming(const TimingWindow &window);
} // namespace osd
#endif // OSD_TIMING_H
