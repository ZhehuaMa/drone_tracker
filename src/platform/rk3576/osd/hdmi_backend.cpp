#include "hdmi_backend.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>

#include "dma_buffer.h"
#include "osd/geometry.h"
#include "osd/layout.h"

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace rk3576 {
namespace {
using osd_backend::DmaBuffer;
using osd_backend::requireRga;
void check(int result, const std::string &operation) {
    if (result < 0) { throw std::runtime_error("OSD " + operation + ": " + std::strerror(errno)); }
}
void requireExclusiveDisplay(int fd, const std::string &device) {
    const auto reject{[&](const std::string &reason) {
        throw std::runtime_error("OSD " + device + ": " + reason
            + "; stop the desktop display service and confirm the device is released before retrying");
    }};
    if (drmSetMaster(fd) != 0) { reject("cannot acquire DRM master: " + std::string{std::strerror(errno)}); }
    // 本板 root 下两个 libdrm 控制权接口均可能误报成功；必须核对内核实际客户端。
    struct stat information{};
    if (fstat(fd, &information) != 0 || !S_ISCHR(information.st_mode)) {
        reject("cannot identify DRM character device");
    }
    const auto device_minor{minor(information.st_rdev)};
    // DRM 在 open 时保存调用线程的 PID；显示设备由渲染线程打开。
    const long owner_tid{syscall(SYS_gettid)};
    const std::string path{"/sys/kernel/debug/dri/" + std::to_string(device_minor) + "/clients"};
    std::ifstream clients{path};
    std::string line{};
    if (!std::getline(clients, line)) { reject("cannot read exclusive ownership evidence " + path); }
    std::istringstream header{line};
    std::string command{}, pid_name{}, dev_name{}, master_name{}, authenticated_name{}, uid_name{}, magic_name{};
    header >> command >> pid_name >> dev_name >> master_name >> authenticated_name >> uid_name >> magic_name;
    if (command != "command" || pid_name != "pid" || dev_name != "dev" || master_name != "master"
        || authenticated_name != "a" || uid_name != "uid" || magic_name != "magic") {
        reject("unrecognized DRM client evidence format " + path);
    }
    unsigned int count{};
    while (std::getline(clients, line)) {
        std::istringstream entry{line};
        long long pid{}, dev{}, uid{}, magic{};
        std::string name{}, master{}, authenticated{}, extra{};
        if (!(entry >> name >> pid >> dev >> master >> authenticated >> uid >> magic) || (entry >> extra)) {
            reject("unrecognized DRM client entry " + path);
        }
        ++count;
        if (pid != owner_tid || dev != device_minor || master != "y") {
            reject("exclusive DRM ownership not confirmed; client " + name + " pid=" + std::to_string(pid)
                + " master=" + master);
        }
    }
    if (!clients.eof() || count != 1) { reject("expected exactly one owned DRM client in " + path); }
    std::cerr << "OSD exclusive DRM ownership verified: device=" << device << " pid=" << getpid()
              << " owner_tid=" << owner_tid
              << " master=y clients=1 evidence=" << path << '\n';
}
struct Scanout {
    std::uint32_t framebuffer{}, handle{}, pitch{};
    std::size_t size{};
    void *pixels{};
    int prime{-1};
    rga_buffer_handle_t rga{};
    void allocate(int device, int width, int height, std::uint32_t format) {
        drm_mode_create_dumb create{};
        create.width = width; create.height = height; create.bpp = 32;
        check(drmIoctl(device, DRM_IOCTL_MODE_CREATE_DUMB, &create), "allocate scanout");
        handle = create.handle; pitch = create.pitch; size = create.size;
        std::array<std::uint32_t, 4> handles{{handle, 0, 0, 0}}, pitches{{pitch, 0, 0, 0}}, offsets{};
        check(drmModeAddFB2(device, width, height, format, handles.data(), pitches.data(), offsets.data(),
                           &framebuffer, 0), "register framebuffer");
        drm_mode_map_dumb mapping{};
        mapping.handle = handle;
        check(drmIoctl(device, DRM_IOCTL_MODE_MAP_DUMB, &mapping), "map scanout");
        void *memory{mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, device, mapping.offset)};
        if (memory == MAP_FAILED) { throw std::runtime_error("OSD scanout mmap failed"); }
        pixels = memory;
        std::memset(pixels, 0, size);
        check(drmPrimeHandleToFD(device, handle, DRM_CLOEXEC | DRM_RDWR, &prime), "export scanout");
        rga = importbuffer_fd(prime, size);
        if (!rga) { throw std::runtime_error("OSD scanout RGA import failed"); }
    }
    void release(int device) noexcept {
        if (rga) { releasebuffer_handle(rga); }
        if (pixels) { munmap(pixels, size); }
        if (prime >= 0) { close(prime); }
        if (framebuffer && device >= 0) { drmModeRmFB(device, framebuffer); }
        if (handle && device >= 0) {
            drm_mode_destroy_dumb destroy{}; destroy.handle = handle;
            drmIoctl(device, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        *this = Scanout{};
    }
    rga_buffer_t image(int width, int height) const {
        return wrapbuffer_handle_t(rga, width, height, pitch / 4, height, RK_FORMAT_BGRA_8888);
    }
};
struct Property { std::uint32_t id{}; std::uint64_t value{}; };
using Properties = std::map<std::string, Property>;
Properties properties(int fd, std::uint32_t object, std::uint32_t type) {
    std::unique_ptr<drmModeObjectProperties, decltype(&drmModeFreeObjectProperties)> list{
        drmModeObjectGetProperties(fd, object, type), drmModeFreeObjectProperties};
    if (!list) { throw std::runtime_error("OSD cannot enumerate DRM properties"); }
    Properties result{};
    for (std::uint32_t i{0}; i < list->count_props; ++i) {
        std::unique_ptr<drmModePropertyRes, decltype(&drmModeFreeProperty)> property{
            drmModeGetProperty(fd, list->props[i]), drmModeFreeProperty};
        if (property) { result.emplace(property->name, Property{property->prop_id, list->prop_values[i]}); }
    }
    return result;
}
class HdmiBackend final : public osd::DisplayBackend {
public:
    explicit HdmiBackend(osd::DisplayConfig config) : config_{std::move(config)}, layout_{config_.craft_name} {
        wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        check(wake_fd_, "eventfd");
    }
    ~HdmiBackend() override { shutdown(); if (wake_fd_ >= 0) { close(wake_fd_); } }
    void initialize() override;
    void prepareVideo(const Frame &frame) override;
    bool present(const osd::PresentationState &state, const std::function<bool()> &authorize) override;
    void discardPrepared() noexcept override { prepared_ = false; }
    void interrupt() noexcept override {
        interrupted_ = true;
        const std::uint64_t value{1};
        const auto ignored{write(wake_fd_, &value, sizeof(value))};
        (void)ignored;
    }
    void shutdown() noexcept override;
private:
    using Request = std::unique_ptr<drmModeAtomicReq, decltype(&drmModeAtomicFree)>;
    Request request() const {
        Request result{drmModeAtomicAlloc(), drmModeAtomicFree};
        if (!result) { throw std::bad_alloc{}; }
        return result;
    }
    void add(drmModeAtomicReq *request, std::uint32_t object, const char *name, std::uint64_t value) {
        const auto &p{properties_.at(object).at(name)};
        check(drmModeAtomicAddProperty(request, object, p.id, value), std::string{"atomic property "} + name);
    }
    void plane(drmModeAtomicReq *request, std::uint32_t id, std::uint32_t framebuffer) {
        add(request, id, "FB_ID", framebuffer); add(request, id, "CRTC_ID", crtc_);
        add(request, id, "SRC_X", 0); add(request, id, "SRC_Y", 0);
        add(request, id, "SRC_W", static_cast<std::uint64_t>(config_.width) << 16);
        add(request, id, "SRC_H", static_cast<std::uint64_t>(config_.height) << 16);
        add(request, id, "CRTC_X", 0); add(request, id, "CRTC_Y", 0);
        add(request, id, "CRTC_W", config_.width); add(request, id, "CRTC_H", config_.height);
    }
    void waitCompletion();
    osd::DisplayConfig config_;
    osd::OsdLayout layout_;
    int fd_{-1}, wake_fd_{-1};
    std::atomic<bool> interrupted_{false};
    std::uint32_t connector_{}, crtc_{}, primary_{}, overlay_{}, mode_blob_{}, old_mode_blob_{};
    std::map<std::uint32_t, Properties> properties_{};
    drmModeModeInfo mode_{};
    std::array<Scanout, 2> primary_buffers_{}, overlay_buffers_{};
    std::array<DmaBuffer, 2> video_{};
    DmaBuffer input_{}, scratch_{}, hud_{};
    osd::VideoGeometry live_geometry_{}, prepared_geometry_{};
    int scanout_{}, live_video_{}, input_width_{}, input_height_{}, input_stride_{};
    FramePixelFormat input_format_{FramePixelFormat::Gray8};
    bool prepared_{}, modeset_{}, flip_done_{};
};
void HdmiBackend::initialize() {
    if (config_.width < 2 || config_.height < 2 || config_.width > 4096 || config_.height > 4096
        || config_.width % 2 || config_.height % 2 || !std::isfinite(config_.refresh_hz)
        || config_.refresh_hz <= 0) {
        throw std::invalid_argument("OSD requires even dimensions 2..4096 and positive refresh");
    }
    fd_ = open(config_.drm_device.c_str(), O_RDWR | O_CLOEXEC);
    check(fd_, "open " + config_.drm_device);
    try {
        requireExclusiveDisplay(fd_, config_.drm_device);
    } catch (...) {
        close(fd_);
        fd_ = -1;
        throw;
    }
    check(drmSetClientCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1), "universal planes");
    check(drmSetClientCap(fd_, DRM_CLIENT_CAP_ATOMIC, 1), "atomic interface");
    std::unique_ptr<drmModeRes, decltype(&drmModeFreeResources)> resources{
        drmModeGetResources(fd_), drmModeFreeResources};
    if (!resources) { throw std::runtime_error("OSD cannot enumerate DRM resources"); }
    int crtc_index{-1};
    for (int i{0}; i < resources->count_connectors; ++i) {
        std::unique_ptr<drmModeConnector, decltype(&drmModeFreeConnector)> connector{
            drmModeGetConnector(fd_, resources->connectors[i]), drmModeFreeConnector};
        if (!connector) { continue; }
        const std::string name{std::string{drmModeGetConnectorTypeName(connector->connector_type)}
            + "-" + std::to_string(connector->connector_type_id)};
        if (name != config_.connector || connector->connection != DRM_MODE_CONNECTED) { continue; }
        connector_ = connector->connector_id;
        double best{1.0};
        for (int j{0}; j < connector->count_modes; ++j) {
            const auto &mode{connector->modes[j]};
            if (mode.hdisplay != config_.width || mode.vdisplay != config_.height
                || mode.flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN)) { continue; }
            const double refresh{mode.clock * 1000.0 / mode.htotal / mode.vtotal};
            const double difference{std::abs(refresh - config_.refresh_hz)};
            if (difference < best && difference < 0.1) { best = difference; mode_ = mode; }
        }
        for (int j{0}; j < connector->count_encoders && !crtc_; ++j) {
            std::unique_ptr<drmModeEncoder, decltype(&drmModeFreeEncoder)> encoder{
                drmModeGetEncoder(fd_, connector->encoders[j]), drmModeFreeEncoder};
            if (!encoder) { continue; }
            for (int k{0}; k < resources->count_crtcs; ++k) {
                if (encoder->possible_crtcs & (1U << k)) {
                    crtc_ = resources->crtcs[k]; crtc_index = k; break;
                }
            }
        }
        break;
    }
    if (!connector_ || !crtc_ || !mode_.hdisplay) {
        throw std::runtime_error("OSD requested connector or progressive HDMI mode unavailable");
    }
    properties_[connector_] = properties(fd_, connector_, DRM_MODE_OBJECT_CONNECTOR);
    properties_[crtc_] = properties(fd_, crtc_, DRM_MODE_OBJECT_CRTC);
    std::unique_ptr<drmModeCrtc, decltype(&drmModeFreeCrtc)> old_crtc{drmModeGetCrtc(fd_, crtc_), drmModeFreeCrtc};
    if (old_crtc && old_crtc->mode_valid) {
        check(drmModeCreatePropertyBlob(fd_, &old_crtc->mode, sizeof(old_crtc->mode), &old_mode_blob_), "save old mode");
        properties_[crtc_].at("MODE_ID").value = old_mode_blob_;
    }
    std::unique_ptr<drmModePlaneRes, decltype(&drmModeFreePlaneResources)> planes{
        drmModeGetPlaneResources(fd_), drmModeFreePlaneResources};
    if (!planes) { throw std::runtime_error("OSD no plane resources"); }
    for (std::uint32_t i{0}; i < planes->count_planes; ++i) {
        std::unique_ptr<drmModePlane, decltype(&drmModeFreePlane)> candidate{
            drmModeGetPlane(fd_, planes->planes[i]), drmModeFreePlane};
        if (!candidate || !(candidate->possible_crtcs & (1U << crtc_index))) { continue; }
        const auto props{properties(fd_, candidate->plane_id, DRM_MODE_OBJECT_PLANE)};
        const auto supports{[&](std::uint32_t format) {
            return std::find(candidate->formats, candidate->formats + candidate->count_formats, format)
                != candidate->formats + candidate->count_formats;
        }};
        if (!primary_ && props.at("type").value == DRM_PLANE_TYPE_PRIMARY && supports(DRM_FORMAT_XRGB8888)) {
            primary_ = candidate->plane_id; properties_[primary_] = props;
        } else if (!overlay_ && props.at("type").value == DRM_PLANE_TYPE_OVERLAY
                   && !candidate->crtc_id && supports(DRM_FORMAT_ARGB8888) && props.count("pixel blend mode")) {
            overlay_ = candidate->plane_id; properties_[overlay_] = props;
        }
    }
    if (!primary_ || !overlay_) { throw std::runtime_error("OSD needs primary and free ARGB overlay planes"); }
    check(drmModeCreatePropertyBlob(fd_, &mode_, sizeof(mode_), &mode_blob_), "create mode blob");
    const std::size_t rgba_size{static_cast<std::size_t>(config_.width) * config_.height * 4};
    for (int i{0}; i < 2; ++i) {
        primary_buffers_[i].allocate(fd_, config_.width, config_.height, DRM_FORMAT_XRGB8888);
        overlay_buffers_[i].allocate(fd_, config_.width, config_.height, DRM_FORMAT_ARGB8888);
        video_[i].allocate(rgba_size);
        video_[i].sync(true); std::memset(video_[i].pixels(), 0, rgba_size); video_[i].sync(false);
    }
    scratch_.allocate(static_cast<std::size_t>(config_.width) * config_.height * 3 / 2);
    hud_.allocate(rgba_size);
    live_geometry_ = osd::fitVideo(config_.width, config_.height, config_.width, config_.height);
    auto initial{request()};
    add(initial.get(), connector_, "CRTC_ID", crtc_);
    add(initial.get(), crtc_, "MODE_ID", mode_blob_); add(initial.get(), crtc_, "ACTIVE", 1);
    plane(initial.get(), primary_, primary_buffers_[0].framebuffer);
    plane(initial.get(), overlay_, overlay_buffers_[0].framebuffer);
    if (properties_[overlay_].count("alpha")) { add(initial.get(), overlay_, "alpha", 65535); }
    add(initial.get(), overlay_, "pixel blend mode", 1); // DRM Coverage：使用直通 alpha。
    if (properties_[primary_].count("zpos") && properties_[overlay_].count("zpos")) {
        add(initial.get(), primary_, "zpos", 0); add(initial.get(), overlay_, "zpos", 1);
    }
    check(drmModeAtomicCommit(fd_, initial.get(), DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_TEST_ONLY, nullptr),
          "atomic two-plane validation");
    check(drmModeAtomicCommit(fd_, initial.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr), "initial modeset");
    modeset_ = true;
    std::cerr << "OSD HDMI " << config_.connector << ' ' << config_.width << 'x' << config_.height
              << " refresh_hz=" << mode_.clock * 1000.0 / mode_.htotal / mode_.vtotal << '\n';
}
void HdmiBackend::prepareVideo(const Frame &frame) {
    osd::TimingSpan timing{timing_sample_, osd::TimingStage::SourceCopy, timing_now_};
    const int width{frame.width()}, height{frame.height()};
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        throw std::invalid_argument("OSD source dimensions invalid");
    }
    const int source_channels{frame.pixelFormat() == FramePixelFormat::Gray8 ? 1 : 3};
    // RGA 的 Y400 路径要求偶数尺寸。奇数灰度输入复制为等值 BGR，保留完整几何。
    const bool expand_gray{source_channels == 1 && (width % 2 || height % 2)};
    const int channels{expand_gray ? 3 : source_channels};
    if (input_width_ != width || input_height_ != height || input_format_ != frame.pixelFormat()) {
        input_stride_ = (width + 15) / 16 * 16;
        input_.allocate(static_cast<std::size_t>(input_stride_) * height * channels);
        input_width_ = width; input_height_ = height; input_format_ = frame.pixelFormat();
    }
    {
        auto view{frame.mapRead()};
        if (!view || view->width() != width || view->height() != height
            || view->pixelFormat() != frame.pixelFormat() || view->mat().step != view->rowStrideBytes()
            || view->mat().type() != (source_channels == 1 ? CV_8UC1 : CV_8UC3)
            || view->rowStrideBytes() < static_cast<std::size_t>(width * source_channels)) {
            throw std::runtime_error("OSD invalid source read view");
        }
        input_.sync(true);
        for (int row{0}; row < height; ++row) {
            auto *destination{input_.pixels() + static_cast<std::size_t>(row) * input_stride_ * channels};
            const auto *source{view->mat().ptr(row)};
            if (expand_gray) {
                for (int column{0}; column < width; ++column) {
                    std::fill_n(destination + column * 3, 3, source[column]);
                }
            } else {
                std::memcpy(destination, source, static_cast<std::size_t>(width) * channels);
            }
        }
        input_.sync(false);
    }
    timing.next(osd::TimingStage::ScaleConvert);
    prepared_geometry_ = osd::fitVideo(width, height, config_.width, config_.height);
    const auto &r{prepared_geometry_.destination};
    const im_rect rectangle{r.x, r.y, r.width, r.height};
    auto output{video_[1 - live_video_].image(config_.width, config_.height, config_.width, RK_FORMAT_BGRA_8888)};
    video_[1 - live_video_].sync(true);
    std::memset(video_[1 - live_video_].pixels(), 0, video_[1 - live_video_].size());
    video_[1 - live_video_].sync(false);
    if (channels == 1) {
        const std::size_t luma_size{static_cast<std::size_t>(config_.width) * config_.height};
        scratch_.sync(true);
        std::memset(scratch_.pixels(), 0, luma_size);
        std::memset(scratch_.pixels() + luma_size, 128, luma_size / 2);
        scratch_.sync(false);
        auto input{input_.image(width, height, input_stride_, RK_FORMAT_YCbCr_400)};
        auto luma{scratch_.image(config_.width, config_.height, config_.width, RK_FORMAT_YCbCr_400)};
        requireRga(improcess(input, luma, {}, {}, rectangle, {}, -1, nullptr, nullptr, IM_SYNC), "gray scale");
        auto yuv{scratch_.image(config_.width, config_.height, config_.width, RK_FORMAT_YCbCr_420_SP)};
        imsetColorSpace(&yuv, static_cast<IM_COLOR_SPACE_MODE>(IM_YUV_BT601_FULL_RANGE));
        requireRga(improcess(yuv, output, {}, {}, {}, {}, -1, nullptr, nullptr, IM_SYNC), "neutral gray conversion");
    } else {
        auto input{input_.image(width, height, input_stride_, RK_FORMAT_BGR_888)};
        requireRga(improcess(input, output, {}, {}, rectangle, {}, -1, nullptr, nullptr, IM_SYNC), "BGR scale");
    }
    prepared_ = true;
}
void HdmiBackend::waitCompletion() {
    const auto deadline{osd::SteadyClock::now() + std::chrono::milliseconds{250}};
    drmEventContext context{}; context.version = 2;
    context.page_flip_handler = [](int, unsigned int, unsigned int, unsigned int, void *data) {
        static_cast<HdmiBackend *>(data)->flip_done_ = true;
    };
    while (!flip_done_) {
        const auto left{std::chrono::duration_cast<std::chrono::milliseconds>(deadline - osd::SteadyClock::now()).count()};
        if (left <= 0) { throw std::runtime_error("OSD atomic display completion timeout"); }
        std::array<pollfd, 2> wait{{{fd_, POLLIN, 0}, {wake_fd_, POLLIN, 0}}};
        const int ready{poll(wait.data(), wait.size(), static_cast<int>(left))};
        if (ready < 0 && errno == EINTR) { continue; }
        check(ready, "poll display completion");
        if (wait[1].revents & POLLIN) { throw std::runtime_error("OSD display interrupted"); }
        if (wait[0].revents & (POLLERR | POLLHUP | POLLNVAL)) { throw std::runtime_error("OSD display disconnected"); }
        if (wait[0].revents & POLLIN) { check(drmHandleEvent(fd_, &context), "display event"); }
    }
}
bool HdmiBackend::present(const osd::PresentationState &state, const std::function<bool()> &authorize) {
    if (state.new_video && !prepared_) { throw std::logic_error("OSD missing prepared video"); }
    const int next{1 - scanout_};
    const auto &geometry{state.new_video ? prepared_geometry_ : live_geometry_};
    osd::TimingSpan timing{timing_sample_, osd::TimingStage::Layout, timing_now_};
    const auto layers{layout_.draw(state, geometry, config_.width, config_.height, std::chrono::system_clock::now())};
    timing.next(osd::TimingStage::Compose);
    hud_.sync(true);
    std::memcpy(hud_.pixels(), layers.hud.data, hud_.size());
    hud_.sync(false);
    cv::Mat target{config_.height, config_.width, CV_8UC4, overlay_buffers_[next].pixels, overlay_buffers_[next].pitch};
    layers.target.copyTo(target);
    auto video{video_[state.new_video ? 1 - live_video_ : live_video_].image(
        config_.width, config_.height, config_.width, RK_FORMAT_BGRA_8888)};
    auto output{primary_buffers_[next].image(config_.width, config_.height)};
    requireRga(imcopy(video, output, 1), "base copy");
    auto hud{hud_.image(config_.width, config_.height, config_.width, RK_FORMAT_BGRA_8888)};
    requireRga(imblend(hud, output, IM_ALPHA_BLEND_SRC_OVER, 1), "HUD blend");
    timing.next(osd::TimingStage::Commit);
    auto update{request()};
    add(update.get(), primary_, "FB_ID", primary_buffers_[next].framebuffer);
    add(update.get(), overlay_, "FB_ID", overlay_buffers_[next].framebuffer);
    if (interrupted_ || !authorize()) { return false; }
    flip_done_ = false;
    check(drmModeAtomicCommit(fd_, update.get(), DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, this),
          "paired plane commit");
    timing.next(osd::TimingStage::FlipWait);
    waitCompletion();
    timing.finish();
    scanout_ = next;
    if (state.new_video) {
        live_video_ = 1 - live_video_;
        live_geometry_ = prepared_geometry_;
    }
    prepared_ = false;
    return true;
}
void HdmiBackend::shutdown() noexcept {
    if (fd_ < 0) { return; }
    if (modeset_) {
        try {
            auto restore{request()};
            for (const auto id : {primary_, overlay_}) {
                for (const char *name : {"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
                    "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H", "alpha", "pixel blend mode", "zpos"}) {
                    const auto found{properties_.at(id).find(name)};
                    if (found != properties_.at(id).end()) { add(restore.get(), id, name, found->second.value); }
                }
            }
            add(restore.get(), connector_, "CRTC_ID", properties_.at(connector_).at("CRTC_ID").value);
            add(restore.get(), crtc_, "MODE_ID", properties_.at(crtc_).at("MODE_ID").value);
            add(restore.get(), crtc_, "ACTIVE", properties_.at(crtc_).at("ACTIVE").value);
            if (drmModeAtomicCommit(fd_, restore.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr) < 0) {
                // 恢复失败时先同步禁用自有层；内核持有扫描引用直到禁用完成。
                auto disable{request()};
                for (const auto id : {primary_, overlay_}) {
                    add(disable.get(), id, "FB_ID", 0); add(disable.get(), id, "CRTC_ID", 0);
                }
                check(drmModeAtomicCommit(fd_, disable.get(), DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr), "disable owned planes");
            }
        } catch (const std::exception &error) {
            std::cerr << "OSD cleanup: " << error.what() << '\n';
            // RMFB/关闭 DRM 文件会移除本文件创建的帧缓冲；内核管理剩余设备引用。
        }
    }
    for (auto &buffer : primary_buffers_) { buffer.release(fd_); }
    for (auto &buffer : overlay_buffers_) { buffer.release(fd_); }
    if (mode_blob_) { drmModeDestroyPropertyBlob(fd_, mode_blob_); }
    if (old_mode_blob_) { drmModeDestroyPropertyBlob(fd_, old_mode_blob_); }
    close(fd_); fd_ = -1;
    mode_blob_ = 0; old_mode_blob_ = 0; modeset_ = false;
    for (auto &buffer : video_) { buffer.reset(); }
    input_.reset(); scratch_.reset(); hud_.reset();
}
} // namespace
std::unique_ptr<osd::DisplayBackend> createHdmiBackend(osd::DisplayConfig config) {
    return std::make_unique<HdmiBackend>(std::move(config));
}
} // namespace rk3576
