#ifndef RK3576_RECORDING_NV12_COPY_H
#define RK3576_RECORDING_NV12_COPY_H

#include <cstring>
#include "../rk3576_camera_capture_base.h"

namespace rk3576 {
/** @brief 验证完整 Y/UV 平面后复制有效行；失败时不写目标，不分配内存。 */
inline bool copyNv12(const CameraNv12View &view, std::uint8_t *destination, std::size_t capacity) noexcept {
    const auto &format{view.format};
    if (!view.pixels || !destination || !format.width || !format.height
        || format.width % 2 || format.height % 2 || format.stride < format.width) { return false; }
    const std::uint64_t rows{static_cast<std::uint64_t>(format.height) * 3 / 2};
    if (rows > (64U * 1024U * 1024U) / format.width) { return false; }
    const std::uint64_t required{(rows - 1) * format.stride + format.width};
    const std::uint64_t packed{rows * format.width};
    if (required > view.available_bytes || packed > capacity || packed > 64U * 1024U * 1024U) { return false; }
    for (std::uint64_t row{}; row < rows; ++row) {
        std::memcpy(destination + row * format.width, view.pixels + row * format.stride, format.width);
    }
    return true;
}
} // namespace rk3576
#endif // RK3576_RECORDING_NV12_COPY_H
