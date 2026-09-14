#ifndef RK3576_MJPEG_VIDEO_READER_H
#define RK3576_MJPEG_VIDEO_READER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

namespace rk3576 {
enum class VideoReadStatus { Frame, Eof, ReadError, DecodeError, SizeChanged };
struct VideoSample {
    cv::Mat gray{};
    std::int64_t pts{};
    bool has_pts{};
};
struct VideoReadResult {
    VideoReadStatus status{VideoReadStatus::ReadError};
    VideoSample sample{};
    std::string reason{};
};
/** @brief 平台内部读取边界；工厂可注入受控读取器，图像在下一次读取前有效。 */
class VideoReader {
public:
    virtual ~VideoReader() = default;
    virtual VideoReadResult next() = 0;
    virtual std::pair<int, int> timeBase() const = 0;
    virtual const std::vector<std::pair<std::string, std::string>> &metadata() const = 0;
};
/** @brief 本地 MJPEG 解复用与亮度解码；构造预读并保留首帧，失败抛出普通异常。 */
class MjpegVideoReader final : public VideoReader {
public:
    MjpegVideoReader(const std::string &path, const std::atomic<bool> &stop);
    ~MjpegVideoReader() override;
    VideoReadResult next() override;
    std::pair<int, int> timeBase() const override;
    const std::vector<std::pair<std::string, std::string>> &metadata() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rk3576
#endif // RK3576_MJPEG_VIDEO_READER_H
