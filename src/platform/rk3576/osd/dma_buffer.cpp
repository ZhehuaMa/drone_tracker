#include "dma_buffer.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>

namespace rk3576::osd_backend {
namespace {
void check(int result, const char *operation) {
    if (result < 0) { throw std::runtime_error(std::string{operation} + ": " + std::strerror(errno)); }
}
}
DmaBuffer::~DmaBuffer() { reset(); }
void DmaBuffer::allocate(std::size_t size) {
    reset();
    const int heap{open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC)};
    check(heap, "OSD open dma_heap");
    dma_heap_allocation_data allocation{};
    allocation.len = size;
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    const int result{ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &allocation)};
    const int saved_errno{errno};
    close(heap);
    errno = saved_errno;
    check(result, "OSD DMA allocation");
    fd_ = static_cast<int>(allocation.fd);
    size_ = size;
    void *mapped{mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0)};
    if (mapped == MAP_FAILED) {
        throw std::runtime_error("OSD DMA mapping failed");
    }
    pixels_ = static_cast<std::uint8_t *>(mapped);
    handle_ = importbuffer_fd(fd_, size_);
    if (!handle_) { throw std::runtime_error("OSD RGA DMA import failed"); }
}
void DmaBuffer::reset() noexcept {
    if (handle_) { releasebuffer_handle(handle_); }
    if (pixels_) { munmap(pixels_, size_); }
    if (fd_ >= 0) { close(fd_); }
    handle_ = 0;
    pixels_ = nullptr;
    size_ = 0;
    fd_ = -1;
}
void DmaBuffer::sync(bool begin) {
    dma_buf_sync sync{};
    sync.flags = DMA_BUF_SYNC_RW | (begin ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END);
    check(ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync), "OSD DMA CPU sync");
}
rga_buffer_t DmaBuffer::image(int width, int height, int stride_pixels, int format) const {
    return wrapbuffer_handle_t(handle_, width, height, stride_pixels, height, format);
}
void requireRga(IM_STATUS status, const char *operation) {
    if (status != IM_STATUS_SUCCESS) {
        throw std::runtime_error(std::string{"OSD RGA "} + operation + ": " + imStrError(status));
    }
}
} // namespace rk3576::osd_backend
