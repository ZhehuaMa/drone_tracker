#include "camera_timing.h"

#include <linux/videodev2.h>
#include <limits>
#include <stdexcept>

namespace rk3576 {
namespace {
std::int64_t checked(__int128 value) {
    if (value < std::numeric_limits<std::int64_t>::min()
        || value > std::numeric_limits<std::int64_t>::max()) {
        throw std::runtime_error("Camera timestamp overflow");
    }
    return static_cast<std::int64_t>(value);
}
} // namespace
CameraClockPair cameraClockPair(std::int64_t before, std::int64_t realtime, std::int64_t after) {
    if (before < 0 || after < before || realtime <= 0) {
        throw std::runtime_error("Invalid camera clock pair");
    }
    const auto midpoint{before + (after - before) / 2};
    return {checked(static_cast<__int128>(realtime) - midpoint), after - before};
}
std::int64_t cameraTimevalUs(std::int64_t seconds, std::int64_t microseconds) {
    if (seconds < 0 || microseconds < 0 || microseconds >= 1000000) {
        throw std::runtime_error("Invalid camera timeval");
    }
    return checked(static_cast<__int128>(seconds) * 1000000 + microseconds);
}
CameraTiming::CameraTiming(CameraClockPair pair) : pair_{pair} {}
std::int64_t CameraTiming::map(std::int64_t seconds, std::int64_t microseconds,
                             std::uint32_t flags, std::int64_t dequeue_us) {
    const auto raw{cameraTimevalUs(seconds, microseconds)};
    const int strategy{(flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC ? 1 : 0};
    if (strategy_ != -1 && strategy_ != strategy) {
        throw std::runtime_error("Camera timestamp strategy changed within round");
    }
    if (dequeue_us < 0) {
        throw std::runtime_error("Invalid dequeue monotonic timestamp");
    }
    const auto mapped{checked(static_cast<__int128>(strategy == 1 ? raw : dequeue_us) + pair_.offset_us)};
    if (mapped <= 0 || mapped <= previous_) {
        throw std::runtime_error("Camera timestamp nonpositive, repeated or backwards");
    }
    strategy_ = strategy;
    previous_ = mapped;
    return mapped;
}
std::string CameraTiming::strategy() const {
    return strategy_ == -1 ? "undetermined" : strategy_ == 1 ? "driver_monotonic" : "dequeue_fallback";
}
} // namespace rk3576
