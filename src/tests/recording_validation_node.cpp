#include "drone_tracker_node.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

/** @brief 仅验收目标使用已有测试访问边界，不向生产参数添加测量开关。 */
struct DroneTrackerNodeTestAccess {
    static void observe(DroneTrackerNode &node,
                        std::function<void(std::int64_t, std::int64_t, bool)> callback) {
        node.update_timing_observer_ = std::move(callback);
    }
    static FrameSourceStatus status(const DroneTrackerNode &node) { return node.mv_camera_->status(); }
};
int main(int argc, char **argv) {
    struct Sample { std::int64_t begin_us{}, duration_us{}; bool consumed{}; };
    constexpr std::size_t LIMIT{200000};
    std::vector<Sample> samples{};
    std::atomic<bool> overflow{false};
    int descriptor{-1};
    int status_descriptor{-1};
    struct StatusSample {
        std::int64_t now_us{}, latest_us{}, synced_us{}, sync_duration_us{}, prepare_us{}, stop_us{};
        std::uint64_t read{}, attempted{}, accepted{}, dropped{}, encoded{}, written{}, synced{};
        std::uint64_t total_bytes{}, free_bytes{}, warmup_discarded{};
        bool known{}, failed{};
    };
    std::vector<StatusSample> statuses{};
    rclcpp::init(argc, argv);
    try {
        const char *output{std::getenv("DRONE_RECORDING_TIMING_FILE")};
        if (!output || !std::filesystem::path{output}.is_absolute()) {
            throw std::invalid_argument("DRONE_RECORDING_TIMING_FILE must be an absolute new CSV path");
        }
        descriptor = ::open(output, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (descriptor < 0) { throw std::runtime_error("Cannot exclusively create timing evidence"); }
        status_descriptor = ::open((std::string{output} + ".status.csv").c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (status_descriptor < 0) { throw std::runtime_error("Cannot exclusively create status evidence"); }
        // 预触及固定样本内存，避免采样页逐渐驻留被误认为生产资源泄漏。
        samples.resize(LIMIT);
        samples.clear();
        statuses.resize(40000);
        statuses.clear();
        auto node{std::make_shared<DroneTrackerNode>()};
        DroneTrackerNodeTestAccess::observe(*node, [&](std::int64_t begin, std::int64_t elapsed, bool consumed) {
            if (samples.size() == LIMIT) { overflow = true; return; }
            samples.push_back({begin, elapsed, consumed});
        });
        const auto collect{[&] {
            if (statuses.size() == 40000) { overflow = true; return; }
            const auto state{DroneTrackerNodeTestAccess::status(*node)};
            const auto &r{state.recording};
            const auto now{std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()};
            statuses.push_back({now, r.latest_frame_us, r.last_synced_frame_us, r.sync_duration_us,
                r.prepare_us, r.stop_us, state.read, r.attempted, r.accepted, r.dropped, r.encoded,
                r.written, r.synced, r.total_bytes, r.free_bytes, state.camera.warmup_discarded,
                r.synced_known, r.phase == "failed"});
        }};
        auto timer{node->create_wall_timer(std::chrono::milliseconds{100}, collect)};
        rclcpp::spin(node);
        timer->cancel();
        node->stop();
        collect();
        node.reset();
        FILE *file{::fdopen(descriptor, "w")};
        if (!file) { throw std::runtime_error("Cannot open timing evidence stream"); }
        descriptor = -1;
        bool failed{std::fprintf(file, "monotonic_us,update_us,consumed\n") < 0};
        for (const auto &sample : samples) {
            if (std::fprintf(file, "%lld,%lld,%d\n", static_cast<long long>(sample.begin_us),
                static_cast<long long>(sample.duration_us), sample.consumed ? 1 : 0) < 0) { failed = true; break; }
        }
        if (std::fflush(file) || ::fdatasync(::fileno(file))) { failed = true; }
        if (std::fclose(file)) { failed = true; }
        FILE *status_file{::fdopen(status_descriptor, "w")};
        if (!status_file) { throw std::runtime_error("Cannot open status evidence stream"); }
        status_descriptor = -1;
        if (std::fprintf(status_file, "monotonic_us,latest_frame_us,synced_frame_us,sync_duration_us,prepare_us,"
            "stop_us,read,attempted,accepted,dropped,encoded,written,synced,total_bytes,free_bytes,"
            "warmup_discarded,sync_known,failed\n") < 0) { failed = true; }
        for (const auto &s : statuses) {
            if (std::fprintf(status_file, "%lld,%lld,%lld,%lld,%lld,%lld,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
                "%llu,%llu,%llu,%d,%d\n", static_cast<long long>(s.now_us), static_cast<long long>(s.latest_us),
                static_cast<long long>(s.synced_us), static_cast<long long>(s.sync_duration_us),
                static_cast<long long>(s.prepare_us), static_cast<long long>(s.stop_us),
                static_cast<unsigned long long>(s.read), static_cast<unsigned long long>(s.attempted),
                static_cast<unsigned long long>(s.accepted), static_cast<unsigned long long>(s.dropped),
                static_cast<unsigned long long>(s.encoded), static_cast<unsigned long long>(s.written),
                static_cast<unsigned long long>(s.synced), static_cast<unsigned long long>(s.total_bytes),
                static_cast<unsigned long long>(s.free_bytes), static_cast<unsigned long long>(s.warmup_discarded),
                s.known ? 1 : 0, s.failed ? 1 : 0) < 0) { failed = true; break; }
        }
        if (std::fflush(status_file) || ::fdatasync(::fileno(status_file))) { failed = true; }
        if (std::fclose(status_file)) { failed = true; }
        if (failed || overflow) { throw std::runtime_error("Timing evidence incomplete or sample bound exceeded"); }
        rclcpp::shutdown();
        return 0;
    } catch (const std::exception &error) {
        if (descriptor >= 0) { ::close(descriptor); }
        if (status_descriptor >= 0) { ::close(status_descriptor); }
        std::cerr << error.what() << '\n';
        rclcpp::shutdown();
        return 1;
    }
}
