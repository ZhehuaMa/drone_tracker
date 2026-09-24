#ifndef RK3576_OSD_PIPELINE_STATE_H
#define RK3576_OSD_PIPELINE_STATE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace rk3576::osd_backend {
struct PlanePair { int primary{}, overlay{}; std::uint64_t generation{}; };
/** @brief 提交与翻页确认分离；未知设备状态只能关闭，不能再次写入。 */
class PlaneState {
public:
    int videoBack() const { requireIdle(); return 1 - displayed_.primary; }
    PlanePair prepare(bool new_video) {
        requireIdle();
        prepared_ = PlanePair{new_video ? 1 - displayed_.primary : displayed_.primary,
                              1 - displayed_.overlay, ++generation_};
        return *prepared_;
    }
    void submitted() {
        if (!prepared_ || in_flight_ || failed_) { throw std::logic_error("OSD invalid submit state"); }
        in_flight_ = true;
    }
    bool confirm(std::uint64_t generation) {
        if (failed_ || !in_flight_ || !prepared_ || prepared_->generation != generation) { return false; }
        displayed_ = *prepared_; prepared_.reset(); in_flight_ = false;
        return true;
    }
    void discard() noexcept { if (!in_flight_) { prepared_.reset(); } }
    void fail() noexcept { failed_ = true; prepared_.reset(); }
    PlanePair displayed() const noexcept { return displayed_; }
private:
    void requireIdle() const {
        if (failed_ || in_flight_) { throw std::logic_error("OSD scanout state is not writable"); }
    }
    PlanePair displayed_{};
    std::optional<PlanePair> prepared_{};
    std::uint64_t generation_{};
    bool in_flight_{}, failed_{};
};
struct SourceLayout {
    int channels{}, stride_pixels{};
    bool repack{}, expand_gray{};
};
/** @brief RGA 步长使用整数像素；不兼容布局须复制有效像素，禁止取整原始步长。 */
inline SourceLayout sourceLayout(int width, int height, int channels, std::size_t bytes) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192
        || (channels != 1 && channels != 3) || bytes < static_cast<std::size_t>(width) * channels) {
        throw std::invalid_argument("OSD invalid source layout");
    }
    const bool expand{channels == 1 && (width % 2 || height % 2)};
    const bool direct{!expand && bytes % channels == 0 && bytes / channels <= 8192
                      && (bytes / channels) % 16 == 0};
    return {expand ? 3 : channels, direct ? static_cast<int>(bytes / channels) : (width + 15) / 16 * 16,
            !direct, expand};
}
} // namespace rk3576::osd_backend
#endif // RK3576_OSD_PIPELINE_STATE_H
