#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <opencv2/imgcodecs.hpp>

namespace {
struct Device {
    int fd{open("/dev/dri/card0", O_RDWR | O_CLOEXEC)};
    ~Device() { if (fd >= 0) { close(fd); } }
};
struct Mapping {
    void *address{MAP_FAILED};
    std::size_t size{};
    ~Mapping() { if (address != MAP_FAILED) { munmap(address, size); } }
};
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2) { throw std::runtime_error("usage: capture_planes OUTPUT_PREFIX"); }
        Device device{};
        if (device.fd < 0 || drmSetClientCap(device.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
            throw std::runtime_error("cannot open DRM device");
        }
        std::unique_ptr<drmModePlaneRes, decltype(&drmModeFreePlaneResources)> planes{
            drmModeGetPlaneResources(device.fd), drmModeFreePlaneResources};
        if (!planes) { throw std::runtime_error("cannot enumerate planes"); }
        int captured{};
        for (std::uint32_t index{}; index < planes->count_planes; ++index) {
            std::unique_ptr<drmModePlane, decltype(&drmModeFreePlane)> plane{
                drmModeGetPlane(device.fd, planes->planes[index]), drmModeFreePlane};
            if (!plane || !plane->fb_id || !plane->crtc_id) { continue; }
            std::unique_ptr<drmModeFB2, decltype(&drmModeFreeFB2)> framebuffer{
                drmModeGetFB2(device.fd, plane->fb_id), drmModeFreeFB2};
            if (!framebuffer || !framebuffer->handles[0]) { throw std::runtime_error("cannot obtain framebuffer"); }
            if (framebuffer->pixel_format != DRM_FORMAT_ARGB8888
                && framebuffer->pixel_format != DRM_FORMAT_XRGB8888) { throw std::runtime_error("unexpected format"); }
            drm_mode_map_dumb request{};
            request.handle = framebuffer->handles[0];
            if (drmIoctl(device.fd, DRM_IOCTL_MODE_MAP_DUMB, &request)) { throw std::runtime_error("cannot map dumb buffer"); }
            Mapping mapping{};
            mapping.size = static_cast<std::size_t>(framebuffer->pitches[0]) * framebuffer->height;
            mapping.address = mmap(nullptr, mapping.size, PROT_READ, MAP_SHARED, device.fd, request.offset);
            if (mapping.address == MAP_FAILED) { throw std::runtime_error("mmap failed"); }
            const cv::Mat pixels{static_cast<int>(framebuffer->height), static_cast<int>(framebuffer->width),
                CV_8UC4, mapping.address, framebuffer->pitches[0]};
            const std::string file{std::string{argv[1]} + "-plane-" + std::to_string(plane->plane_id) + ".png"};
            if (!cv::imwrite(file, pixels)) { throw std::runtime_error("image write failed"); }
            std::cout << file << " framebuffer=" << plane->fb_id << " format=" << framebuffer->pixel_format << '\n';
            ++captured;
        }
        return captured ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
