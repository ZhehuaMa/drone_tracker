#include "session.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "metadata.h"

extern char **environ;

namespace rk3576::recording {
namespace {
std::atomic<bool> orphan_pending{false};
void closeFd(int &fd) noexcept { if (fd >= 0) { ::close(fd); fd = -1; } }
std::string executablePath() {
    std::array<char, 4096> path{};
    const ssize_t size{::readlink("/proc/self/exe", path.data(), path.size() - 1)};
    if (size <= 0) { throw std::runtime_error("Cannot resolve recording worker location"); }
    return (std::filesystem::path{std::string{path.data(), static_cast<std::size_t>(size)}}.parent_path()
        / "rk3576_recording_worker").string();
}
std::string configJson(const FrameSourceConfig &config, const CameraFormat &f, std::uint64_t round) {
    const auto &c{config.camera_recording};
    utsname board{};
    if (::uname(&board)) { throw std::runtime_error("Recording board identity unavailable"); }
    std::ifstream model_file{"/proc/device-tree/model", std::ios::binary};
    std::array<char, 256> model{};
    model_file.read(model.data(), model.size() - 1);
    std::ostringstream out{};
    out << "{\"directory\":" << quote(c.directory) << ",\"identity\":" << quote(c.identity)
        << ",\"board_hostname\":" << quote(board.nodename) << ",\"board_model\":" << quote(model.data())
        << ",\"kernel\":" << quote(board.release) << ",\"architecture\":" << quote(board.machine)
        << ",\"bitrate_bps\":" << c.bitrate_bps << ",\"segment_seconds\":" << c.segment_seconds
        << ",\"sync_interval_ms\":" << c.sync_interval_ms << ",\"max_total_bytes\":" << c.max_total_bytes
        << ",\"min_free_bytes\":" << c.min_free_bytes << ",\"round\":" << round
        << ",\"width\":" << f.width << ",\"height\":" << f.height << ",\"stride\":" << f.stride
        << ",\"fps\":" << config.fps << ",\"quantization\":" << f.quantization
        << ",\"colorspace\":" << f.colorspace << ",\"ycbcr\":" << f.ycbcr_encoding
        << ",\"transfer\":" << f.transfer_function << ",\"camera\":" << quote(config.path) << '}';
    if (out.str().size() > 131072) { throw std::runtime_error("Recording configuration exceeds 128 KiB"); }
    return out.str();
}
} // namespace

void validateConfig(const FrameSourceConfig &config) {
    const auto &c{config.camera_recording};
    if (!c.enabled) { return; }
    if (config.type != FrameSourceType::Camera || config.camera_sensor_model != CameraSensorModel::Sc233hgs) {
        throw std::invalid_argument("Independent recording requires RK3576 SC233HGS camera");
    }
    if (!std::filesystem::path{c.directory}.is_absolute() || c.bitrate_bps <= 0 || c.bitrate_bps > INT32_MAX
        || c.segment_seconds < 10 || c.segment_seconds > 600
        || (c.sync_interval_ms != 500 && c.sync_interval_ms != 1000 && c.sync_interval_ms != 2000)
        || !c.max_total_bytes || !c.min_free_bytes) {
        throw std::invalid_argument("Invalid recording startup parameters");
    }
    // 最近存在的父目录决定创建后挂载；权限错误属于本轮资源故障，不改变配置合法性。
    auto path{std::filesystem::path{c.directory}};
    struct statfs fs{};
    while (::statfs(path.c_str(), &fs) != 0) {
        if (errno != ENOENT) { return; }
        if (path == path.root_path()) { throw std::invalid_argument("Recording mount cannot be resolved"); }
        path = path.parent_path();
    }
    if (fs.f_type != 0xEF53) { throw std::invalid_argument("Recording directory must be on local ext4"); }
    // ext2/ext3 与 ext4 共用 magic；核对内核当前挂载类型，不能仅凭 magic 接受。
    const auto canonical{std::filesystem::weakly_canonical(path).string()};
    std::ifstream mounts{"/proc/self/mountinfo"};
    std::string line{}, type{};
    std::size_t longest{};
    while (std::getline(mounts, line)) {
        std::istringstream fields{line};
        std::string id{}, parent{}, dev{}, root{}, mount{}, rest{};
        fields >> id >> parent >> dev >> root >> mount;
        // mountinfo 对空格、制表和反斜杠使用八进制转义。
        for (std::size_t pos{}; (pos = mount.find('\\', pos)) != std::string::npos;) {
            if (pos + 3 >= mount.size()) { break; }
            const auto digits{mount.substr(pos + 1, 3)};
            if (digits.find_first_not_of("01234567") != std::string::npos) { ++pos; continue; }
            mount.replace(pos, 4, 1, static_cast<char>(std::stoi(digits, nullptr, 8)));
            ++pos;
        }
        const auto separator{line.find(" - ")};
        if (separator == std::string::npos) { continue; }
        const bool contains{mount == "/" || canonical == mount || canonical.rfind(mount + '/', 0) == 0};
        if (contains && mount.size() >= longest) {
            std::istringstream tail{line.substr(separator + 3)};
            tail >> type;
            longest = mount.size();
        }
    }
    if (type != "ext4") { throw std::invalid_argument("Recording mount type is not verified local ext4"); }
}

struct Session::Impl {
    FrameSourceConfig config{};
    std::string executable{};
    ReapProcess reap{};
    std::unique_ptr<RecordingSlots> slots{};
    mutable std::mutex mutex{};
    std::thread monitor{};
    std::atomic<bool> monitor_stop{false};
    bool orphan{};
    pid_t pid{-1};
    int control{-1};
    std::uint64_t round{};
    std::int64_t terminate_us{};
    CameraRecordingStatus record{};

    Impl(FrameSourceConfig input, std::string worker, ReapProcess reap_process)
        : config{std::move(input)}, executable{std::move(worker)}, reap{std::move(reap_process)} {
        validateConfig(config);
        record.enabled = config.camera_recording.enabled;
        if (record.enabled) { record.phase = "idle"; }
    }
    void fail(const std::string &reason) {
        record.phase = "failed";
        record.reason = reason;
        if (slots) { slots->fail(reason.c_str()); }
    }
    void poll() {
        std::lock_guard<std::mutex> lock{mutex};
        if (pid <= 0 || !slots) { return; }
        int result{};
        const auto exited{reap ? reap(pid, &result, WNOHANG) : ::waitpid(pid, &result, WNOHANG)};
        auto &h{slots->header()};
        const auto now{RecordingSlots::monotonicUs()};
        if (exited == pid || (exited < 0 && errno == ECHILD)) {
            pid = -1;
            closeFd(control);
            if (!h.finished.load(std::memory_order_acquire) || h.failure.load(std::memory_order_acquire)) {
                if (h.failure.load(std::memory_order_acquire) == 2) { fail(h.reason.data()); }
                else { fail("Recording worker exited without a completed session"); }
            } else { record.phase = "finished"; }
            return;
        }
        const auto syncing{h.sync_started_us.load(std::memory_order_acquire)};
        const auto first{h.first_input_mono_us.load(std::memory_order_acquire)};
        const auto last_sync{h.last_sync_mono_us.load(std::memory_order_acquire)};
        const auto last_idr{h.last_idr_mono_us.load(std::memory_order_acquire)};
        if (syncing && now - syncing > 5000000) { slots->fail("Recording file synchronization exceeded 5 seconds"); }
        if (first && now - std::max(first, last_sync) > 5000000) {
            slots->fail("Recording persistence made no progress for 5 seconds");
        }
        if (first && !h.stopping.load() && now - std::max(first, last_idr) > 2000000) {
            slots->fail("Recording IDR made no progress for 2 seconds");
        }
        if (h.failure.load(std::memory_order_acquire) == 2) {
            record.phase = "failed";
            record.reason = h.reason.data();
            if (!terminate_us) { terminate_us = now; ::kill(pid, SIGTERM); }
            else if (now - terminate_us > 500000) { ::kill(pid, SIGKILL); }
        }
    }
    void spawn(const CameraFormat &format) {
        slots = std::make_unique<RecordingSlots>(format.width, format.height, ++round);
        const auto text{configJson(config, format, round)};
        int config_fd{::memfd_create("recording-config", MFD_CLOEXEC)};
        std::array<int, 2> sockets{{-1, -1}};
        posix_spawn_file_actions_t actions{};
        bool actions_ready{};
        std::array<int, 3> descriptors{{-1, -1, -1}};
        try {
            if (config_fd < 0 || ::write(config_fd, text.data(), text.size()) != static_cast<ssize_t>(text.size())
                || ::lseek(config_fd, 0, SEEK_SET) < 0
                || ::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sockets.data())) {
                throw std::runtime_error("Recording control resources unavailable");
            }
            const std::array<int, 3> sources{{slots->fd(), config_fd, sockets[1]}};
            if (::posix_spawn_file_actions_init(&actions)) { throw std::runtime_error("Recording spawn actions failed"); }
            actions_ready = true;
            for (std::size_t i{}; i < sources.size(); ++i) {
                descriptors[i] = ::fcntl(sources[i], F_DUPFD_CLOEXEC, 10);
                if (descriptors[i] < 0 || ::posix_spawn_file_actions_adddup2(&actions, descriptors[i], 3 + i)) {
                    throw std::runtime_error("Recording spawn descriptor setup failed");
                }
            }
            const auto path{executable.empty() ? executablePath() : executable};
            std::array<char *, 2> args{{const_cast<char *>(path.c_str()), nullptr}};
            const int result{::posix_spawn(&pid, path.c_str(), &actions, nullptr, args.data(), environ)};
            if (result) { pid = -1; throw std::runtime_error("Recording worker spawn: " + std::string{std::strerror(result)}); }
            control = sockets[0]; sockets[0] = -1;
            ::posix_spawn_file_actions_destroy(&actions); actions_ready = false;
            for (auto &fd : descriptors) { closeFd(fd); }
            closeFd(config_fd); closeFd(sockets[0]); closeFd(sockets[1]);
        } catch (...) {
            if (actions_ready) { ::posix_spawn_file_actions_destroy(&actions); }
            for (auto &fd : descriptors) { closeFd(fd); }
            closeFd(config_fd); closeFd(sockets[0]); closeFd(sockets[1]);
            throw;
        }
    }
};

Session::Session(FrameSourceConfig config, std::string executable, ReapProcess reap)
    : impl_{std::make_shared<Impl>(std::move(config), std::move(executable), std::move(reap))} {}
Session::~Session() {
    stop({});
    {
        std::lock_guard<std::mutex> lock{impl_->mutex};
        if (impl_->pid > 0) {
            ::kill(impl_->pid, SIGKILL);
            closeFd(impl_->control);
            // 复用已启动的监控线程，析构时不再分配线程；回收前保留共享映射。
            orphan_pending.store(true);
            impl_->orphan = true;
            impl_->monitor.detach();
            return;
        }
        impl_->monitor_stop.store(true);
    }
    if (impl_->monitor.joinable()) { impl_->monitor.join(); }
}
void Session::start(const CameraFormat &format) noexcept {
    const auto begin{RecordingSlots::monotonicUs()};
    try {
        {
            std::lock_guard<std::mutex> lock{impl_->mutex};
            if (!impl_->record.enabled) { return; }
            if (impl_->pid > 0 || orphan_pending.load()) {
                impl_->record.phase = "failed";
                impl_->record.reason = "Previous recording process is still awaiting reclamation";
                return;
            }
            impl_->record = {};
            impl_->record.enabled = true;
            impl_->record.phase = "preparing";
            impl_->terminate_us = 0;
            if (!impl_->config.camera_recording.preparation_error.empty()) {
                impl_->fail(impl_->config.camera_recording.preparation_error);
                impl_->record.prepare_us = RecordingSlots::monotonicUs() - begin;
                return;
            }
            if (!impl_->monitor.joinable()) {
                impl_->monitor = std::thread{[state = impl_] {
                    while (!state->monitor_stop.load()) {
                        state->poll();
                        {
                            std::lock_guard<std::mutex> lock{state->mutex};
                            if (state->orphan && state->pid <= 0) {
                                orphan_pending.store(false);
                                break;
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds{10});
                    }
                }};
            }
            impl_->spawn(format);
            impl_->record.round = impl_->round;
        }
        while (RecordingSlots::monotonicUs() - begin < 3000000) {
            {
                std::lock_guard<std::mutex> lock{impl_->mutex};
                if (impl_->pid <= 0 || impl_->slots->header().failure.load()) { break; }
                if (impl_->slots->header().ready.load(std::memory_order_acquire)) {
                    impl_->record.phase = "waiting_for_qualified_input";
                    impl_->record.prepare_us = RecordingSlots::monotonicUs() - begin;
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        std::lock_guard<std::mutex> lock{impl_->mutex};
        if (impl_->record.reason.empty()) { impl_->fail("Recording preparation failed or exceeded 3 seconds"); }
        impl_->record.prepare_us = RecordingSlots::monotonicUs() - begin;
    } catch (const std::exception &error) {
        std::lock_guard<std::mutex> lock{impl_->mutex};
        impl_->fail(error.what());
        impl_->record.prepare_us = RecordingSlots::monotonicUs() - begin;
    }
}
void Session::stopAccepting() noexcept {
    if (impl_->slots) { impl_->slots->header().stop_accepting.store(true, std::memory_order_release); }
}
void Session::stop(const FrameSourceStatus &camera) noexcept {
    if (!impl_->record.enabled) { return; }
    const auto begin{RecordingSlots::monotonicUs()};
    stopAccepting();
    try {
        std::lock_guard<std::mutex> lock{impl_->mutex};
        if (impl_->pid <= 0 || !impl_->slots) { return; }
        auto &h{impl_->slots->header()};
        if (h.stopping.load() && impl_->record.stop_us != 0) { return; }
        if (!h.stopping.load()) {
            const auto text{cameraSummary(camera)};
            std::copy(text.begin(), text.end(), h.camera_summary.begin());
            h.summary_size.store(text.size(), std::memory_order_release);
            h.stopping.store(true, std::memory_order_release);
        }
    } catch (const std::exception &error) {
        std::lock_guard<std::mutex> lock{impl_->mutex}; impl_->fail(error.what());
    }
    while (RecordingSlots::monotonicUs() - begin < 3000000) {
        {
            std::lock_guard<std::mutex> lock{impl_->mutex};
            if (impl_->pid <= 0) {
                impl_->record.stop_us = RecordingSlots::monotonicUs() - begin;
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    std::lock_guard<std::mutex> lock{impl_->mutex};
    impl_->fail("Recording shutdown exceeded 3 seconds; old resources isolated");
    if (impl_->pid > 0) { ::kill(impl_->pid, SIGTERM); }
    impl_->record.stop_us = RecordingSlots::monotonicUs() - begin;
}
CameraNv12Sink *Session::sink() noexcept {
    std::lock_guard<std::mutex> lock{impl_->mutex};
    return impl_->record.phase == "waiting_for_qualified_input" ? impl_->slots.get() : nullptr;
}
CameraRecordingStatus Session::status() const {
    std::lock_guard<std::mutex> lock{impl_->mutex};
    auto out{impl_->record};
    if (!impl_->slots) { return out; }
    const auto &h{impl_->slots->header()};
    out.attempted = h.attempts.load(); out.accepted = h.accepted.load(); out.dropped = h.dropped.load();
    out.cancelled = h.cancelled.load(); out.encoded = h.encoded.load(); out.written = h.written.load();
    out.synced = h.synced.load(); out.synced_known = out.synced != 0;
    if (h.ready.load(std::memory_order_acquire)) { out.session = h.session_path.data(); }
    out.total_bytes = h.total_bytes.load(); out.free_bytes = h.free_bytes.load();
    out.latest_frame_us = h.latest_frame_us.load(); out.last_synced_frame_us = h.last_synced_frame_us.load();
    out.last_sync_monotonic_us = h.last_sync_mono_us.load(); out.sync_duration_us = h.sync_duration_us.load();
    if (out.phase == "waiting_for_qualified_input" && out.attempted) {
        out.phase = out.synced_known ? "recording" : "waiting_for_first_sync";
        if (out.synced_known && impl_->config.camera_recording.sync_interval_ms == 1000
            && out.latest_frame_us - out.last_synced_frame_us > 2000000) { out.phase = "degraded"; }
    }
    return out;
}
} // namespace rk3576::recording
