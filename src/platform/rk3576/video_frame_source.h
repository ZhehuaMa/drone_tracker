#ifndef RK3576_VIDEO_FRAME_SOURCE_H
#define RK3576_VIDEO_FRAME_SOURCE_H

#include <functional>
#include <thread>

#include "mjpeg_video_reader.h"
#include "video_timing.h"
#include "platform/platform_factory.h"

namespace rk3576 {
enum class VideoState { Ready, Running, Stopped, Eof, Failed };
struct VideoDiagnostic {
    VideoState state{VideoState::Ready};
    std::string path{};
    std::string reason{};
    std::uint64_t frame_index{};
    std::int64_t pts{};
    bool has_pts{};
    std::uint64_t read{};
    std::uint64_t enqueued{};
    std::uint64_t dropped{};
};
struct VideoDependencies {
    std::function<std::unique_ptr<VideoReader>(const std::string &, const std::atomic<bool> &)> open{};
    std::shared_ptr<VideoClock> clock{};
    std::function<std::thread(std::function<void()>)> launch{};
    std::function<void(const VideoDiagnostic &)> diagnose{};
};
/**
 * @brief 单生产者/单消费者视频源，每次启动从头播放。
 * 控制方串行调用 start/stop；start 和析构前消费者必须静止，stop 可与取帧并发。
 */
class Rk3576VideoFrameSource final : public FrameSource {
public:
    explicit Rk3576VideoFrameSource(const FrameSourceConfig &config, VideoDependencies dependencies = {});
    ~Rk3576VideoFrameSource() override;
    bool start() override;
    bool stop() override;
    bool isRunning() const override;
    bool getImageSize(int &width, int &height) const override;
    bool waitFrame(FrameRef &frame, std::chrono::microseconds timeout) override;
    bool tryFrame(FrameRef &frame) override;
    void configureRecording(bool, const std::string &, const std::string &) override;
    /** @brief 线程安全地复制最近状态与本轮统计；不改变公共接口。 */
    VideoDiagnostic diagnostic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rk3576
#endif // RK3576_VIDEO_FRAME_SOURCE_H
