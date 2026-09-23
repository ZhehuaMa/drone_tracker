#ifndef RK3576_OSD_DMA_BUFFER_H
#define RK3576_OSD_DMA_BUFFER_H

#include <cstddef>
#include <cstdint>

#include <im2d.hpp>

namespace rk3576::osd_backend {
/** @brief 显示专用 DMA 内存；仅由渲染线程使用，硬件同步结束后才能销毁。 */
class DmaBuffer {
public:
    DmaBuffer() = default;
    ~DmaBuffer();
    DmaBuffer(const DmaBuffer &) = delete;
    DmaBuffer &operator=(const DmaBuffer &) = delete;
    void allocate(std::size_t size);
    void reset() noexcept;
    void sync(bool begin);
    rga_buffer_t image(int width, int height, int stride_pixels, int format) const;
    std::uint8_t *pixels() const { return pixels_; }
    int fd() const { return fd_; }
    std::size_t size() const { return size_; }
private:
    int fd_{-1};
    std::size_t size_{};
    std::uint8_t *pixels_{};
    rga_buffer_handle_t handle_{};
};
void requireRga(IM_STATUS status, const char *operation);
} // namespace rk3576::osd_backend
#endif // RK3576_OSD_DMA_BUFFER_H
