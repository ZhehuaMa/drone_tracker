#ifndef PLATFORM_CAMERA_RECORDING_H
#define PLATFORM_CAMERA_RECORDING_H

#include <cstdint>
#include <string>
#include <vector>

/** @brief 独立相机录像启动配置；单位为字节、秒及毫秒，不含平台 SDK 类型。 */
struct CameraRecordingConfig {
    bool enabled{false};
    std::string directory{"/var/lib/drone_tracker/recordings"};
    std::int64_t bitrate_bps{12000000};
    int segment_seconds{60};
    int sync_interval_ms{1000};
    std::uint64_t max_total_bytes{21474836480ULL};
    std::uint64_t min_free_bytes{5368709120ULL};
    /** @brief 初始化时取得的程序/模型身份及完整参数文本；不在采集路径读取文件。 */
    std::string identity{};
    /** @brief 身份等录像专属准备失败；本轮报告失败，不阻止健康的跟踪启动。 */
    std::string preparation_error{};
};

/** @brief 与跟踪状态独立的录像快照；同步未知时不得把落后量当作零。 */
struct CameraRecordingStatus {
    bool enabled{};
    std::string phase{"disabled"};
    std::string reason{};
    std::string session{};
    std::uint64_t round{};
    std::uint64_t attempted{};
    std::uint64_t accepted{};
    std::uint64_t dropped{};
    std::uint64_t cancelled{};
    std::uint64_t encoded{};
    std::uint64_t written{};
    std::uint64_t synced{};
    std::uint64_t total_bytes{};
    std::uint64_t free_bytes{};
    bool synced_known{};
    std::int64_t latest_frame_us{};
    std::int64_t last_synced_frame_us{};
    std::int64_t last_sync_monotonic_us{};
    std::int64_t sync_duration_us{};
    std::int64_t prepare_us{};
    std::int64_t stop_us{};
};

#endif // PLATFORM_CAMERA_RECORDING_H
