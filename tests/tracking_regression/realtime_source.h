#ifndef TRACKING_REGRESSION_REALTIME_SOURCE_H
#define TRACKING_REGRESSION_REALTIME_SOURCE_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "platform/platform_factory.h"
#include <sensor_msgs/msg/imu.hpp>

namespace tracking_regression {
struct SourceOptions {
    std::filesystem::path fixture{}, output{};
    std::size_t queue_capacity{32};
    std::uint64_t max_supply_lateness_us{}, clock_jump_tolerance_us{5000};
};
struct SupplyRecord {
    std::uint64_t id{}, relative_us{}, planned_ns{}, supplied_ns{}, frame_time_us{};
    bool supplied{}, enqueued{}, residual{};
};
/** @brief Test-only full-clip preparation and independent original-time input scheduler. */
class RealtimeSource final : public FrameSource {
public:
    explicit RealtimeSource(SourceOptions options);
    ~RealtimeSource() override;
    /** @brief Decode and verify every cached frame before models or playback clocks start. */
    bool prepare();
    bool start() override;
    bool stop() override;
    bool isRunning() const override;
    FrameSourceStatus status() const override;
    bool getImageSize(int &width, int &height) const override;
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override;
    bool tryFrame(FrameRef &frame) override;
    void configureRecording(bool need_save, const std::string &, const std::string &) override;
    void setAttitudeCallback(std::function<void(sensor_msgs::msg::Imu::SharedPtr)> callback);

private:
    void decode();
    void supply();
    void fail(const std::string &reason);
    void exportRecords();
    void exportPreparation();
    SourceOptions options_{};
    mutable std::mutex mutex_{};
    std::condition_variable wake_{};
    std::vector<cv::Mat> decoded_{};
    std::vector<std::string> pixel_hashes_{};
    std::deque<FrameRef> queue_{};
    std::vector<SupplyRecord> records_{};
    std::vector<std::pair<std::int64_t, sensor_msgs::msg::Imu>> attitudes_{};
    std::function<void(sensor_msgs::msg::Imu::SharedPtr)> attitude_callback_{};
    std::thread supplier_{};
    std::atomic<bool> stopping_{false};
    FrameSourceStatus status_{};
    bool prepared_{}, preparation_attempted_{}, started_{}, exported_{}, normal_eof_{};
    std::uint64_t preparation_begin_ns_{}, preparation_end_ns_{}, cached_bytes_{};
    std::uint64_t m0_ns_{}, e0_us_{};
};
} // namespace tracking_regression
#endif // TRACKING_REGRESSION_REALTIME_SOURCE_H
