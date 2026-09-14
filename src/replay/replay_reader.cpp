#include "replay_reader.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
namespace replay {
namespace {
class OpenCvSource final : public VideoSource {
public:
    explicit OpenCvSource(const std::string &path) : capture_{path} {
        if (!capture_.isOpened()) {
            throw std::runtime_error("Cannot open video: " + path);
        }
        metadata_.fps = capture_.get(cv::CAP_PROP_FPS);
        const double count{capture_.get(cv::CAP_PROP_FRAME_COUNT)};
        if (std::isfinite(count) && count > 0 && std::floor(count) == count && count < 9007199254740992.0) {
            metadata_.declared_frames = static_cast<std::uint64_t>(count);
        }
        metadata_.backend = capture_.getBackendName();
    }
    bool read(cv::Mat &image) override { return capture_.read(image); }
    VideoMetadata metadata() const override { return metadata_; }

private:
    cv::VideoCapture capture_;
    VideoMetadata metadata_{};
};
} // namespace
std::unique_ptr<VideoSource> openVideo(const std::string &path) { return std::make_unique<OpenCvSource>(path); }
ReplayReader::ReplayReader(std::unique_ptr<VideoSource> source, double fps, int max_frames)
    : source_{std::move(source)}, max_frames_{max_frames} {
    if (!source_ || !std::isfinite(fps) || fps <= 0 || fps > 1000000 || max_frames < 0) {
        throw std::invalid_argument("Invalid reader configuration");
    }
    metadata_ = source_->metadata();
    if (std::isfinite(metadata_.fps) && metadata_.fps > 0 &&
        std::abs(metadata_.fps - fps) > std::max(0.01, fps * 0.001)) {
        throw std::runtime_error("input.fps conflicts with video metadata");
    }
    if (!decode(first_)) {
        throw std::runtime_error("No decodable first frame: " + end_);
    }
}
bool ReplayReader::decode(DecodedFrame &frame) {
    const auto start{std::chrono::steady_clock::now()};
    frame = {};
    bool success{};
    try {
        success = source_->read(frame.image);
    } catch (const std::exception &) {
        end_ = "decode_error";
        throw;
    }
    frame.decode_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!success || frame.image.empty()) {
        end_ = metadata_.declared_frames == 0      ? "end_unverified"
               : read_ < metadata_.declared_frames ? "decode_error"
                                                   : "eof";
        return false;
    }
    ++read_;
    if (frame.image.type() != CV_8UC1 && frame.image.type() != CV_8UC3) {
        end_ = "decode_error";
        throw std::runtime_error("Unsupported decoded pixel type at frame " + std::to_string(read_ - 1));
    }
    if (read_ == 1) {
        metadata_.width = frame.image.cols;
        metadata_.height = frame.image.rows;
    } else if (frame.image.cols != metadata_.width || frame.image.rows != metadata_.height) {
        end_ = "decode_error";
        throw std::runtime_error("Decoded dimensions changed at frame " + std::to_string(read_ - 1));
    }
    return true;
}
bool ReplayReader::next(DecodedFrame &frame) {
    if (max_frames_ > 0 && delivered_ >= static_cast<std::uint64_t>(max_frames_)) {
        end_ = "frame_limit";
        return false;
    }
    if (delivered_ == 0) {
        frame = std::move(first_);
    } else if (!decode(frame)) {
        return false;
    }
    ++delivered_;
    return true;
}
cv::Mat grayImage(const cv::Mat &image) {
    if (image.empty()) {
        throw std::runtime_error("Empty decoded image");
    }
    if (image.type() == CV_8UC1) {
        return image;
    }
    if (image.type() != CV_8UC3) {
        throw std::runtime_error("Expected Gray8 or BGR8");
    }
    cv::Mat gray{};
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}
} // namespace replay
