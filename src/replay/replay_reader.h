#ifndef REPLAY_READER_H
#define REPLAY_READER_H
#include <cstdint>
#include <memory>
#include <string>
#include <opencv2/core.hpp>
namespace replay {
struct VideoMetadata {
    int width{};
    int height{};
    double fps{};
    std::uint64_t declared_frames{};
    std::string backend{};
};
/** @brief 同步解码边界，可替换为可控输入；不创建生产采集线程。 */
class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual bool read(cv::Mat &image) = 0;
    virtual VideoMetadata metadata() const = 0;
};
std::unique_ptr<VideoSource> openVideo(const std::string &path);
struct DecodedFrame {
    cv::Mat image{};
    double decode_ms{};
};
class ReplayReader {
public:
    ReplayReader(std::unique_ptr<VideoSource> source, double fps, int max_frames);
    bool next(DecodedFrame &frame);
    const VideoMetadata &metadata() const noexcept { return metadata_; }
    std::uint64_t readCount() const noexcept { return read_; }
    const std::string &endReason() const noexcept { return end_; }

private:
    bool decode(DecodedFrame &frame);
    std::unique_ptr<VideoSource> source_;
    VideoMetadata metadata_{};
    DecodedFrame first_{};
    std::uint64_t read_{};
    std::uint64_t delivered_{};
    int max_frames_{};
    std::string end_{};
};
/** @brief 只在输入边界转换 BGR；不缩放，不改变相机内参。 */
cv::Mat grayImage(const cv::Mat &image);
} // namespace replay
#endif // REPLAY_READER_H
