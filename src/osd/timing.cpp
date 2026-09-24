#include "osd/timing.h"

#include <algorithm>
#include <sstream>

namespace osd {
void TimingHistogram::add(std::uint64_t microseconds) {
    ++count;
    total_us += microseconds;
    max_us = std::max(max_us, microseconds);
    std::size_t bucket{};
    while (bucket < 27 && microseconds > (std::uint64_t{1} << bucket)) { ++bucket; }
    ++buckets[bucket];
}
std::optional<std::uint64_t> TimingHistogram::percentile(unsigned percent) const {
    if (!count) { return {}; }
    const auto rank{count / 100 * percent + (count % 100 * percent + 99) / 100};
    std::uint64_t cumulative{};
    for (std::size_t i{0}; i < 27; ++i) {
        cumulative += buckets[i];
        if (cumulative >= rank) { return std::uint64_t{1} << i; }
    }
    return {}; // 非空直方图的无上界结果表示溢出桶。
}
void TimingSample::add(TimingStage stage, TimingClock::duration duration) {
    const auto us{std::chrono::duration_cast<std::chrono::microseconds>(duration).count()};
    stages[static_cast<std::size_t>(stage)] = static_cast<std::uint64_t>(std::max<std::int64_t>(0, us));
}
TimingSpan::TimingSpan(TimingSample *sample, TimingStage stage,
                       const std::function<TimingClock::time_point()> &now)
    : sample_{sample}, stage_{stage}, now_{now} {
    if (sample_) { start_ = now_(); }
}
TimingSpan::~TimingSpan() { finish(); }
void TimingSpan::finish() {
    if (sample_) {
        sample_->add(stage_, now_() - start_);
        sample_ = nullptr;
    }
}
void TimingSpan::next(TimingStage stage) {
    if (sample_) {
        const auto end{now_()};
        sample_->add(stage_, end - start_);
        start_ = end;
    }
    stage_ = stage;
}
void TimingWindow::add(const TimingSample &sample, bool new_video, TimingResult result, bool first) {
    auto &group{groups[(new_video ? 0 : 3) + static_cast<std::size_t>(result)]};
    ++group.count;
    if (first) { ++group.first_frames; }
    for (std::size_t i{0}; i < TIMING_STAGES; ++i) {
        if (sample.stages[i]) { group.stages[i].add(*sample.stages[i]); }
    }
}
std::string formatTiming(const TimingWindow &window, RenderPath path) {
    constexpr std::array<const char *, TIMING_STAGES> names{{"source_copy", "scale_convert", "layout", "compose",
        "commit", "flip_wait", "render_total", "pending_wait", "submit_to_display", "new_frame_interval", "other", "source_prepare", "overlay_upload"}};
    constexpr std::array<const char *, 3> results{{"success", "rejected", "failure"}};
    std::ostringstream out{};
    out << "OSD timing round=" << window.round << " window_id=" << window.id << " window_us="
        << std::chrono::duration_cast<std::chrono::microseconds>(window.end - window.start).count()
        << " submitted=" << window.submitted << " replaced=" << window.replaced << " displayed=" << window.displayed
        << " rejected=" << window.rejected << " commits=" << window.commits << " unit=us quantiles=approx_pow2";
    for (std::size_t g{0}; g < window.groups.size(); ++g) {
        const auto &group{window.groups[g]};
        out << "\n  " << (g < 3 ? "new_video" : "repeat") << " result=" << results[g % 3]
            << " count=" << group.count << " first_frames=" << group.first_frames;
        if (!group.count) { out << " samples=none"; continue; }
        for (std::size_t s{0}; s < TIMING_STAGES; ++s) {
            const auto stage{static_cast<TimingStage>(s)};
            if (path == RenderPath::CachedOverlay
                && (stage == TimingStage::SourceCopy || stage == TimingStage::Compose)) { continue; }
            if (path == RenderPath::Legacy
                && (stage == TimingStage::SourcePrepare || stage == TimingStage::OverlayUpload)) { continue; }
            const auto &h{group.stages[s]};
            out << ' ' << names[s] << "{count=" << h.count;
            if (h.count) {
                const auto p95{h.percentile(95)}, p99{h.percentile(99)};
                out << ",mean=" << static_cast<double>(h.total_us / h.count) << ",max=" << h.max_us
                    << ",p95_approx=" << (p95 ? std::to_string(*p95) : ">67108864")
                    << ",p99_approx=" << (p99 ? std::to_string(*p99) : ">67108864");
                // 保存桶计数才能跨窗口合并百分位；窗口百分位本身不能直接平均。
                out << ",buckets=";
                for (std::size_t bucket{0}; bucket < h.buckets.size(); ++bucket) {
                    if (bucket) { out << ':'; }
                    out << h.buckets[bucket];
                }
            }
            out << '}';
        }
    }
    return out.str();
}
} // namespace osd
