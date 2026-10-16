#include "client.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace diagnostics {
Client::~Client() noexcept {
    if (recorder_) {
        active.store(nullptr, std::memory_order_release);
    }
    if (region_) {
        ::munmap(region_, sizeof(SharedRegion));
    }
    for (int fd : {memory_, notification_, connection_, consumer_pidfd_}) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
}
bool Client::prepare(Mode mode, const std::string &endpoint, std::uint64_t run_id, const std::string &identity) {
    if (mode == Mode::Off) {
        return true;
    }
    if (region_ || endpoint.size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
    }
    if (identity.size() >= Control{}.identity.size()) {
        return false;
    }
    endpoint_ = endpoint;
    identity_ = identity;
    memory_ = static_cast<int>(::syscall(SYS_memfd_create, "drone-diagnostics", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    notification_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (memory_ < 0 || notification_ < 0 || ::ftruncate(memory_, sizeof(SharedRegion)) != 0) {
        return false;
    }
    void *mapping{::mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, memory_, 0)};
    if (mapping == MAP_FAILED) {
        return false;
    }
    region_ = new (mapping) SharedRegion{};
    region_->run_id = run_id ? run_id : (monotonicNs() ^ (static_cast<std::uint64_t>(::getpid()) << 32));
    // 构造时填满槽已经预触及全部页；只有创建方构造共享原子对象。
    if (::fcntl(memory_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0) {
        return false;
    }
    recorder_ = std::make_unique<Recorder>(*region_, notification_, mode);
    active.store(recorder_.get(), std::memory_order_release);
    const bool connected{connectConsumer()};
    auto started{event(Type::RunStart, connected ? Reason::None : Reason::Incomplete, !connected)};
    started.values[6] = 2; // Append-only source-context and request-disposition interpretation.
    recorder_->submit(Producer::Control, started);
    return connected;
}
bool Client::connectConsumer() noexcept {
    last_connect_ns_ = monotonicNs();
    if (connection_ >= 0) {
        ::close(connection_);
        connection_ = -1;
    }
    const auto deadline{std::chrono::steady_clock::now() + std::chrono::milliseconds{500}};
    int socket{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (socket < 0) {
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint_.data(), endpoint_.size());
    if (::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 && errno != EINPROGRESS) {
        ::close(socket);
        return false;
    }
    Control control{};
    control.run_id = region_->run_id;
    control.pid = static_cast<std::uint32_t>(::getpid());
    std::copy(identity_.begin(), identity_.end(), control.identity.begin());
    std::array<char, CMSG_SPACE(2 * sizeof(int))> ancillary{};
    iovec data{&control, sizeof(control)};
    msghdr message{};
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    message.msg_control = ancillary.data();
    message.msg_controllen = ancillary.size();
    cmsghdr *header{CMSG_FIRSTHDR(&message)};
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(2 * sizeof(int));
    const std::array<int, 2> descriptors{memory_, notification_};
    std::memcpy(CMSG_DATA(header), descriptors.data(), sizeof(descriptors));
    if (::sendmsg(socket, &message, MSG_NOSIGNAL) != sizeof(control)) {
        ::close(socket);
        return false;
    }
    pollfd ready{socket, POLLIN, 0};
    const auto remaining{
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())};
    if (remaining.count() <= 0 || ::poll(&ready, 1, static_cast<int>(remaining.count())) <= 0 ||
        ::recv(socket, &control, sizeof(control), 0) != sizeof(control) || !valid(control) || control.result != 0) {
        ::close(socket);
        return false;
    }
    consumer_pidfd_ = static_cast<int>(::syscall(SYS_pidfd_open, control.pid, 0));
    if (consumer_pidfd_ < 0) {
        ::close(socket);
        return false;
    }
    connection_ = socket;
    return true;
}
bool Client::reconnect() noexcept {
    if (!region_ || monotonicNs() - last_connect_ns_ < 5000000000ULL) {
        return false;
    }
    if (consumer_pidfd_ >= 0) {
        pollfd old{consumer_pidfd_, POLLIN, 0};
        if (::poll(&old, 1, 0) <= 0) {
            return false;
        }
        ::close(consumer_pidfd_);
        consumer_pidfd_ = -1;
        region_->consumer_pid.store(0, std::memory_order_release);
    } else if (region_->consumer_pid.load(std::memory_order_acquire) != 0) {
        return false;
    }
    return connectConsumer();
}
bool Client::finish(std::chrono::milliseconds budget) noexcept {
    if (!region_) {
        return true;
    }
    recorder_->submit(Producer::Control, event(Type::RunEnd));
    region_->closing.store(1, std::memory_order_release);
    const std::uint64_t one{1};
    const auto ignored{::write(notification_, &one, sizeof(one))};
    (void)ignored;
    const auto deadline{std::chrono::steady_clock::now() + std::min(budget, std::chrono::milliseconds{2000})};
    while (!region_->drained.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    active.store(nullptr, std::memory_order_release);
    return region_->drained.load(std::memory_order_acquire) != 0;
}
} // namespace diagnostics
