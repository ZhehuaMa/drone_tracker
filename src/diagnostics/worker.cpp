#include "client.h"
#include "storage.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

namespace diagnostics {
namespace {
volatile std::sig_atomic_t stopping{};
void stop(int) {
    stopping = 1;
    ::alarm(2);
}
struct Session {
    SharedRegion *region{};
    int memory{-1}, notification{-1}, connection{-1}, pidfd{-1};
    std::unique_ptr<Storage> storage{};
    std::array<std::uint64_t, PRODUCERS> drops{};
    bool disconnected{};
    std::uint64_t last_health_ns{};
    ~Session() {
        if (region) {
            region->consumer_pid.store(0, std::memory_order_release);
            ::munmap(region, sizeof(SharedRegion));
        }
        for (int fd : {memory, notification, connection, pidfd}) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
    }
};
std::unique_ptr<Session> acceptSession(int listener, const StorageConfig &config, bool full) {
    auto session{std::make_unique<Session>()};
    session->connection = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (session->connection < 0) {
        return {};
    }
    struct ucred peer{};
    socklen_t peer_size{sizeof(peer)};
    if (::getsockopt(session->connection, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0 || peer.uid != ::geteuid()) {
        return {};
    }
    Control control{};
    std::array<char, CMSG_SPACE(3 * sizeof(int))> ancillary{};
    iovec data{&control, sizeof(control)};
    msghdr message{};
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    message.msg_control = ancillary.data();
    message.msg_controllen = ancillary.size();
    pollfd ready{session->connection, POLLIN, 0};
    if (::poll(&ready, 1, 100) <= 0) {
        return {};
    }
    const auto received{::recvmsg(session->connection, &message, MSG_CMSG_CLOEXEC)};
    std::array<int, 2> descriptors{-1, -1};
    unsigned found{}, received_descriptors{};
    for (cmsghdr *header{CMSG_FIRSTHDR(&message)}; header; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
            const std::size_t count{(header->cmsg_len - CMSG_LEN(0)) / sizeof(int)};
            const auto *fds{reinterpret_cast<const int *>(CMSG_DATA(header))};
            for (std::size_t index{}; index < count; ++index) {
                ++received_descriptors;
                if (found < 2) {
                    descriptors[found++] = fds[index];
                } else {
                    ::close(fds[index]);
                }
            }
        }
    }
    session->memory = descriptors[0];
    session->notification = descriptors[1];
    control.result = EPROTO;
    struct stat information{};
    const int seals{::fcntl(session->memory, F_GET_SEALS)};
    const int notify_flags{::fcntl(session->notification, F_GETFL)};
    std::array<char, 128> link{};
    const std::string notification_path{"/proc/self/fd/" + std::to_string(session->notification)};
    const auto link_size{::readlink(notification_path.c_str(), link.data(), link.size() - 1)};
    const bool notification_valid{
        link_size > 0 && std::string(link.data(), static_cast<std::size_t>(link_size)) == "anon_inode:[eventfd]" &&
        (notify_flags & O_NONBLOCK) != 0};
    if (received == sizeof(control) && (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) == 0 && found == 2
        && received_descriptors == 2 &&
        valid(control) && control.pid == static_cast<std::uint32_t>(peer.pid) &&
        ::fstat(session->memory, &information) == 0 && information.st_size == sizeof(SharedRegion) && seals >= 0 &&
        (seals & (F_SEAL_SHRINK | F_SEAL_GROW)) == (F_SEAL_SHRINK | F_SEAL_GROW) && notification_valid && !full) {
        void *mapping{::mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, session->memory, 0)};
        if (mapping != MAP_FAILED) {
            session->region = static_cast<SharedRegion *>(mapping);
            auto &region{*session->region};
            std::uint32_t expected{};
            if (region.magic == MAGIC && region.endian_marker == ENDIAN_MARKER && region.version == FORMAT_VERSION &&
                region.event_size == sizeof(Event) && region.region_size == sizeof(SharedRegion) &&
                region.run_id == control.run_id &&
                region.consumer_pid.compare_exchange_strong(expected, static_cast<std::uint32_t>(::getpid()))) {
                session->pidfd = static_cast<int>(::syscall(SYS_pidfd_open, peer.pid, 0));
                session->storage = std::make_unique<Storage>(config, region);
                if (session->pidfd >= 0 && session->storage->open() &&
                    session->storage->identity(control.identity.data(),
                                               strnlen(control.identity.data(), control.identity.size()))) {
                    control.result = 0;
                }
            } else {
                ::munmap(session->region, sizeof(SharedRegion));
                session->region = nullptr;
            }
        }
    }
    control.pid = static_cast<std::uint32_t>(::getpid());
    const auto ignored{::send(session->connection, &control, sizeof(control), MSG_NOSIGNAL)};
    (void)ignored;
    if (control.result != 0) {
        return {};
    }
    return session;
}
/** @brief 单生产者两环按尝试序号合并；释放位置仅在整个批次成功写入后推进。 */
bool collect(Session &session, std::array<Event, 2048> &batch) {
    auto &region{*session.region};
    std::array<std::uint64_t, PRODUCERS> normal{}, critical{}, dropped_snapshot{};
    std::size_t count{};
    for (std::size_t index{}; index < PRODUCERS; ++index) {
        auto &buffer{region.producers[index]};
        normal[index] = buffer.normal.read.load(std::memory_order_relaxed);
        critical[index] = buffer.critical.read.load(std::memory_order_relaxed);
        const auto normal_end{buffer.normal.write.load(std::memory_order_acquire)};
        const auto critical_end{buffer.critical.write.load(std::memory_order_acquire)};
        while (count < batch.size() - PRODUCERS * (GAP_RANGES + 1) &&
               (normal[index] < normal_end || critical[index] < critical_end)) {
            const bool use_normal{critical[index] == critical_end ||
                                  (normal[index] < normal_end &&
                                   buffer.normal.events[normal[index] % NORMAL_CAPACITY].producer_seq <
                                       buffer.critical.events[critical[index] % CRITICAL_CAPACITY].producer_seq)};
            batch[count++] = use_normal ? buffer.normal.events[normal[index]++ % NORMAL_CAPACITY]
                                        : buffer.critical.events[critical[index]++ % CRITICAL_CAPACITY];
        }
        const auto gap_epoch{buffer.position.gap_epoch.load(std::memory_order_seq_cst)};
        const auto gap_start{count};
        const auto dropped{buffer.position.dropped.load(std::memory_order_acquire)};
        dropped_snapshot[index] = dropped;
        if (dropped != session.drops[index] && count < batch.size()) {
            auto gap{event(Type::Gap, Reason::BufferFull, true)};
            gap.run_id = region.run_id;
            gap.round_id = region.round_id.load();
            gap.producer_id = static_cast<std::uint32_t>(index);
            gap.producer_seq = 0; // 收集器摘要不是生产者尝试；不参与去重序号。
            gap.mono_ns = monotonicNs();
            gap.values[0] = dropped;
            gap.values[1] = buffer.position.first_dropped.load();
            gap.values[2] = buffer.position.last_dropped.load();
            gap.values[3] = buffer.position.range_count.load();
            gap.values[4] = gap.values[3] > GAP_RANGES ? 1 : 0;
            batch[count++] = gap;
            const auto ranges{std::min(gap.values[3], static_cast<std::uint64_t>(GAP_RANGES))};
            for (std::size_t range{}; range < ranges && count < batch.size(); ++range) {
                auto detail{gap};
                detail.values[5] = 1;
                detail.values[1] = buffer.position.ranges[range].first.load(std::memory_order_acquire);
                detail.values[2] = buffer.position.ranges[range].last.load(std::memory_order_acquire);
                batch[count++] = detail;
            }
            if ((gap_epoch & 1) || gap_epoch != buffer.position.gap_epoch.load(std::memory_order_seq_cst)) {
                count = gap_start;
                dropped_snapshot[index] = session.drops[index];
            }
        }
    }
    const auto now{monotonicNs()};
    if (now - session.last_health_ns >= 1000000000ULL || region.closing.load()) {
        for (std::size_t index{}; index < PRODUCERS && count < batch.size(); ++index) {
            auto health{event(Type::Health)};
            health.run_id = region.run_id;
            health.round_id = region.round_id.load();
            health.producer_id = static_cast<std::uint32_t>(index);
            health.mono_ns = now;
            const auto &position{region.producers[index].position};
            health.values[0] = position.generated.load();
            health.values[1] = position.written.load();
            health.values[2] = position.synced.load();
            health.values[3] = position.dropped.load();
            const auto oldest{region.oldest_unsynced_ns.load()};
            health.values[4] = oldest ? now - oldest : 0;
            const auto sync_start{region.sync_started_ns.load()}, sync_end{region.sync_finished_ns.load()};
            health.values[5] = sync_end >= sync_start ? sync_end - sync_start : 0;
            health.values[6] = region.error.load();
            if (sync_end) {
                health.flags |= Observed;
            }
            if (!health.values[6] && health.values[4] <= 500000000ULL) {
                health.flags |= Complete;
            }
            batch[count++] = health;
        }
        session.last_health_ns = now;
    }
    if (!session.storage->append(batch.data(), count)) {
        return false;
    }
    for (std::size_t index{}; index < PRODUCERS; ++index) {
        region.producers[index].normal.read.store(normal[index], std::memory_order_release);
        region.producers[index].critical.read.store(critical[index], std::memory_order_release);
        session.drops[index] = dropped_snapshot[index];
    }
    return count != 0;
}
bool empty(const SharedRegion &region) {
    for (const auto &buffer : region.producers) {
        if (buffer.normal.read.load() != buffer.normal.write.load() ||
            buffer.critical.read.load() != buffer.critical.write.load()) {
            return false;
        }
    }
    return true;
}
} // namespace
} // namespace diagnostics
int main(int argc, char **argv) {
    using namespace diagnostics;
    if (argc == 2 && std::string{argv[1]} == "--protocol-version") {
        std::cout << FORMAT_VERSION << '\n';
        return 0;
    }
    std::string endpoint{"/run/drone_tracker/diagnostics.sock"};
    StorageConfig config{};
    try {
        for (int index{1}; index < argc; ++index) {
            const std::string option{argv[index]};
            if (index + 1 >= argc) {
                throw std::runtime_error{"缺少参数值"};
            }
            const std::string value{argv[++index]};
            if (option == "--socket") {
                endpoint = value;
            } else if (option == "--directory") {
                config.directory = value;
            } else if (option == "--sync-ms") {
                config.sync_ms = static_cast<unsigned>(std::stoul(value));
            } else if (option == "--minimum-free-bytes") {
                config.minimum_free_bytes = std::stoull(value);
            } else if (option == "--total-bytes") {
                config.total_bytes = std::stoull(value);
            } else if (option == "--segment-bytes") {
                config.segment_bytes = std::stoull(value);
            } else {
                throw std::runtime_error{"未知参数"};
            }
        }
        if (endpoint.size() >= sizeof(sockaddr_un::sun_path) || (config.sync_ms != 100 && config.sync_ms != 200)) {
            throw std::runtime_error{"端点或同步周期无效"};
        }
        ::signal(SIGINT, stop);
        ::signal(SIGTERM, stop);
        const std::string lock_path{endpoint + ".lock"};
        const int ownership{::open(lock_path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
        if (ownership < 0 || ::flock(ownership, LOCK_EX | LOCK_NB) != 0) {
            throw std::runtime_error{"已有诊断消费者，拒绝第二实例"};
        }
        struct stat existing{};
        if (::lstat(endpoint.c_str(), &existing) == 0) {
            if (!S_ISSOCK(existing.st_mode) || existing.st_uid != ::geteuid()) {
                throw std::runtime_error{"已有端点类型或所有者不符"};
            }
            ::unlink(endpoint.c_str()); // 持有唯一锁，清理崩溃留下的同用户套接字。
        }
        const int listener{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, endpoint.data(), endpoint.size());
        // 唯一锁保护端点；第二实例在 bind 之前失败，避免夺取健康服务。
        if (listener < 0 || ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
            ::chmod(endpoint.c_str(), 0600) != 0 || ::listen(listener, 2) != 0) {
            throw std::runtime_error{"诊断端点创建失败"};
        }
        std::array<std::unique_ptr<Session>, 2> sessions{};
        std::array<Event, 2048> batch{}; // 512 KiB，低于 1 MiB。
        while (!stopping) {
            pollfd ready{listener, POLLIN, 0};
            ::poll(&ready, 1, 10);
            if (ready.revents & POLLIN) {
                auto slot{std::find_if(sessions.begin(), sessions.end(), [](const auto &session) { return !session; })};
                auto accepted{acceptSession(listener, config, slot == sessions.end())};
                if (accepted && slot != sessions.end()) {
                    *slot = std::move(accepted);
                }
            }
            for (auto &session : sessions) {
                if (!session) {
                    continue;
                }
                std::uint64_t notification{};
                const auto ignored{::read(session->notification, &notification, sizeof(notification))};
                (void)ignored;
                pollfd lifetime[2]{{session->connection, POLLIN | POLLHUP, 0}, {session->pidfd, POLLIN, 0}};
                ::poll(lifetime, 2, 0);
                session->disconnected =
                    session->disconnected || (lifetime[0].revents & POLLHUP) || (lifetime[1].revents & POLLIN);
                collect(*session, batch);
                if (session->storage->due(monotonicNs())) {
                    session->storage->synchronize();
                }
                const bool closing{session->region->closing.load(std::memory_order_acquire) != 0};
                if ((closing || session->disconnected) && (empty(*session->region) || session->storage->error())) {
                    if (session->disconnected && !closing) {
                        auto abnormal{event(Type::AbnormalEnd, Reason::Incomplete, true)};
                        abnormal.run_id = session->region->run_id;
                        abnormal.mono_ns = monotonicNs();
                        session->storage->append(&abnormal, 1);
                    }
                    const bool finished{session->storage->finish(closing)};
                    session->region->drained.store(finished ? 1U : 0U, std::memory_order_release);
                    session.reset();
                }
            }
        }
        for (auto &session : sessions) {
            if (session) {
                collect(*session, batch);
                session->storage->finish(false);
            }
        }
        ::close(listener);
        ::unlink(endpoint.c_str());
        ::close(ownership);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
