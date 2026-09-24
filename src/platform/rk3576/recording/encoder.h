#ifndef RK3576_RECORDING_ENCODER_H
#define RK3576_RECORDING_ENCODER_H

#include <functional>
#include <memory>
#include <vector>
#include "shared_slots.h"

namespace rk3576::recording {
struct EncodedFrame {
    RecordingSlots::Identity identity{};
    std::int64_t pts_us{};
    bool idr{};
    std::vector<std::uint8_t> bytes{};
    std::int64_t encoded_monotonic_us{};
};
/** @brief 工作进程独占的同步硬件编码器；一次只允许一帧在途。 */
class Encoder final {
public:
    Encoder(CameraFormat format, int fps, std::int64_t bitrate);
    ~Encoder();
    EncodedFrame encode(const std::uint8_t *pixels, const RecordingSlots::Identity &identity,
                        std::int64_t pts_us, bool idr, const std::function<void()> &copied);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rk3576::recording
#endif // RK3576_RECORDING_ENCODER_H
