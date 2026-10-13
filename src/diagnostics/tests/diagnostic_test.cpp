#include "diagnostics/client.h"
#include "diagnostics/storage.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <future>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <cstring>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace diagnostics;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}
std::filesystem::path temporary() {
    auto path{std::filesystem::temp_directory_path() / ("diagnostic-tests-" + std::to_string(::getpid()))};
    std::filesystem::remove_all(path);
    std::filesystem::create_directory(path);
    return path;
}
void ringTest() {
    auto region{std::make_unique<SharedRegion>()};
    region->run_id = 1;
    int notification{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
    Recorder recorder{*region, notification, Mode::Basic};
    for (std::size_t index{}; index < NORMAL_CAPACITY; ++index) {
        check(recorder.submit(Producer::Node, event(Type::Capture)), "normal fill");
    }
    check(!recorder.submit(Producer::Node, event(Type::Capture)), "normal must reject full");
    check(recorder.submit(Producer::Node, event(Type::Decision, Reason::HistoryMiss, true)), "reserved critical");
    auto &buffer{region->producers[0]};
    check(buffer.position.dropped.load() == 1 && buffer.position.first_dropped.load() == NORMAL_CAPACITY + 1,
          "gap identity");
    buffer.normal.read.store(NORMAL_CAPACITY);
    for (std::size_t index{}; index < NORMAL_CAPACITY; ++index) {
        check(recorder.submit(Producer::Node, event(Type::Capture)), "ring wrap");
    }
    for (std::size_t index{1}; index < CRITICAL_CAPACITY; ++index) {
        check(recorder.submit(Producer::Node, event(Type::Decision, Reason::None, true)), "critical fill");
    }
    check(!recorder.submit(Producer::Node, event(Type::Decision, Reason::None, true)), "critical full");
    Control control{};
    control.run_id = 1;
    check(valid(control), "valid control");
    control.version++;
    check(!valid(control), "reject version");
    control.version--;
    control.length--;
    check(!valid(control), "reject length");
    ::close(notification);
}
void visibilityTest() {
    void *mapping{::mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)};
    check(mapping != MAP_FAILED, "mapping");
    auto *region{new (mapping) SharedRegion{}};
    region->run_id = 2;
    const pid_t child{::fork()};
    check(child >= 0, "fork");
    if (child == 0) {
        auto &ring{region->producers[0].normal};
        for (std::uint64_t index{}; index < 50000; ++index) {
            while (ring.write.load(std::memory_order_acquire) == index) {
                std::this_thread::yield();
            }
            const auto &value{ring.events[index % NORMAL_CAPACITY]};
            if (value.values[0] != index || value.values[1] != ~index) {
                ::_exit(1);
            }
            ring.read.store(index + 1, std::memory_order_release);
        }
        ::_exit(0);
    }
    auto &ring{region->producers[0].normal};
    for (std::uint64_t index{}; index < 50000; ++index) {
        while (index - ring.read.load(std::memory_order_acquire) >= NORMAL_CAPACITY) {
            std::this_thread::yield();
        }
        auto value{event(Type::Capture)};
        value.values[0] = index;
        value.values[1] = ~index;
        ring.events[index % NORMAL_CAPACITY] = value;
        ring.write.store(index + 1, std::memory_order_release);
    }
    // Half-filled event has no release publication and is deliberately invisible.
    ring.events[50000 % NORMAL_CAPACITY].values[0] = 999;
    int status{};
    ::waitpid(child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "atomic visibility");
    check(ring.write.load() == 50000, "half event unpublished");
    ::munmap(mapping, sizeof(SharedRegion));
}
StorageConfig config(const std::filesystem::path &path) {
    StorageConfig value{};
    value.directory = path;
    value.minimum_free_bytes = 0;
    return value;
}
void storageTest(const std::filesystem::path &path) {
    auto region{std::make_unique<SharedRegion>()};
    region->run_id = 10;
    StorageIo io{};
    io.write = [](int fd, const void *data, std::size_t size) {
        return ::write(fd, data, std::min(size, std::size_t{7}));
    };
    Storage storage{config(path / "normal"), *region, io};
    check(storage.open(), "open");
    Event values[2]{};
    values[0].run_id = 10;
    values[0].producer_seq = 1;
    values[0].mono_ns = monotonicNs();
    values[1] = values[0];
    values[1].producer_seq = 2;
    check(storage.append(values, 2), "short writes complete");
    check(region->producers[0].position.written.load() == 2, "written");
    check(region->producers[0].position.synced.load() == 0, "write != sync");
    check(storage.finish(true), "double sync");
    check(region->producers[0].position.synced.load() == 2, "sync confirmed");
    const auto scan_result{scan(storage.path())};
    check(scan_result.events.size() == 2 && scan_result.complete && !scan_result.damaged, "scan complete");
    std::ifstream input{storage.path(), std::ios::binary};
    std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    for (std::size_t size{}; size < bytes.size(); ++size) {
        const auto copy{path / "cut.dtd"};
        std::ofstream output{copy, std::ios::binary};
        output.write(bytes.data(), size);
        output.close();
        const auto cut{scan(copy)};
        check(cut.valid_bytes <= size && cut.events.size() <= 2, "all truncation boundaries");
    }
    auto corrupt{bytes};
    corrupt[sizeof(RecordHeader)] ^= 1;
    {
        std::ofstream output{path / "corrupt.dtd", std::ios::binary};
        output.write(corrupt.data(), corrupt.size());
    }
    check(scan(path / "corrupt.dtd").damaged, "CRC damage accepted");
    corrupt = bytes;
    corrupt[4] = 99;
    {
        std::ofstream output{path / "version.dtd", std::ios::binary};
        output.write(corrupt.data(), corrupt.size());
    }
    check(scan(path / "version.dtd").unknown_version, "unknown version accepted");
    {
        std::ofstream output{path / "no-commit.dtd", std::ios::binary};
        output.write(bytes.data(), sizeof(RecordHeader) + 2 * sizeof(Event));
    }
    check(scan(path / "no-commit.dtd").commits.empty(), "file size synthesized commit");
    for (unsigned failure{0}; failure < 5; ++failure) {
        auto failed_region{std::make_unique<SharedRegion>()};
        failed_region->run_id = failure + 100;
        StorageIo injected{};
        unsigned sync_calls{};
        if (failure == 0) {
            injected.write = [](int, const void *, std::size_t) -> std::ptrdiff_t {
                errno = EIO;
                return -1;
            };
        }
        if (failure == 1 || failure == 2) {
            injected.sync = [&sync_calls, failure](int fd) {
                if (++sync_calls == failure) {
                    errno = EIO;
                    return -1;
                }
                return ::fdatasync(fd);
            };
        }
        if (failure == 3) {
            injected.directory_sync = [](int) {
                errno = EIO;
                return -1;
            };
        }
        if (failure == 4) {
            injected.write = [](int fd, const void *data, std::size_t size) {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
                return ::write(fd, data, size);
            };
        }
        Storage failed{config(path / ("fault" + std::to_string(failure))), *failed_region, injected};
        if (failure == 3) {
            check(!failed.open(), "directory failure");
            continue;
        }
        check(failed.open(), "fault open");
        const bool appended{failed.append(values, 2)};
        if (failure == 0) {
            check(!appended && failed_region->producers[0].position.written.load() == 0, "write failure position");
        } else if (failure < 3) {
            check(appended && !failed.synchronize() && failed_region->producers[0].position.synced.load() == 0,
                  "sync failure preserves confirmed");
        } else {
            check(appended, "bounded injected block");
        }
    }
    auto blocked_region{std::make_unique<SharedRegion>()};
    blocked_region->run_id = 200;
    std::mutex mutex{};
    std::condition_variable changed{};
    bool entered{}, release{};
    StorageIo blocked_io{};
    blocked_io.sync = [&](int fd) {
        std::unique_lock<std::mutex> lock{mutex};
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release; });
        return ::fdatasync(fd);
    };
    Storage blocked{config(path / "blocked"), *blocked_region, blocked_io};
    check(blocked.open(), "blocked open");
    auto synchronizing{std::async(std::launch::async, [&] { return blocked.synchronize(); })};
    {
        std::unique_lock<std::mutex> lock{mutex};
        check(changed.wait_for(lock, std::chrono::seconds{1}, [&] { return entered; }), "block entered");
    }
    Recorder recorder{*blocked_region, -1, Mode::Basic};
    const auto submission_start{monotonicNs()};
    for (unsigned index{}; index < 5000; ++index) {
        recorder.submit(Producer::Node, event(Type::Capture));
    }
    check(monotonicNs() - submission_start < 100000000, "业务提交等待了阻塞存储");
    check(blocked_region->producers[0].position.dropped.load() > 0, "阻塞期间未报告溢出");
    {
        std::lock_guard<std::mutex> lock{mutex};
        release = true;
    }
    changed.notify_all();
    check(synchronizing.get(), "blocked sync recover");
    auto protected_region{std::make_unique<SharedRegion>()};
    protected_region->run_id = 50;
    auto protected_config{config(path / "space")};
    protected_config.total_bytes = 1;
    Storage protected_storage{protected_config, *protected_region};
    check(!protected_storage.open() && protected_storage.error() == ENOSPC, "space protection");
}
std::size_t descriptorCount(pid_t pid) {
    const auto directory{std::filesystem::path{"/proc"} / std::to_string(pid) / "fd"};
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator{directory},
                                                std::filesystem::directory_iterator{}));
}
void rejectedConnections(const std::string &socket, pid_t worker, Client &healthy) {
    const auto baseline{descriptorCount(worker)};
    for (unsigned repeat{}; repeat < 2; ++repeat) {
        for (unsigned scenario{}; scenario < 12; ++scenario) {
            const int connection{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0)};
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::strncpy(address.sun_path, socket.c_str(), sizeof(address.sun_path) - 1);
            check(::connect(connection, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
                  "malformed connect");
            timeval timeout{1, 0};
            ::setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            const int memory{::memfd_create("diagnostic-malformed", MFD_CLOEXEC | MFD_ALLOW_SEALING)};
            const auto bytes{sizeof(SharedRegion) + (scenario == 4 ? 4096 : 0)};
            check(::ftruncate(memory, static_cast<off_t>(bytes)) == 0, "malformed size");
            void *mapped{::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0)};
            check(mapped != MAP_FAILED, "malformed mmap");
            auto *region{static_cast<SharedRegion *>(mapped)};
            std::allocator<SharedRegion> allocator{};
            std::allocator_traits<std::allocator<SharedRegion>>::construct(allocator, region);
            region->run_id = 702;
            if (scenario == 10) { region->version++; }
            if (scenario != 5) { ::fcntl(memory, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW); }
            const int notification{scenario == 6 ? ::open("/dev/null", O_RDONLY | O_CLOEXEC)
                : ::eventfd(0, EFD_CLOEXEC | (scenario == 7 ? 0 : EFD_NONBLOCK))};
            Control control{};
            control.pid = static_cast<std::uint32_t>(::getpid());
            control.run_id = 702;
            if (scenario == 0) { control.version++; }
            if (scenario == 1) { control.length--; }
            if (scenario == 2) { control.region_size = 9ULL * 1024 * 1024; }
            std::vector<char> payload(sizeof(control) + (scenario == 11 ? 1 : 0));
            std::memcpy(payload.data(), &control, sizeof(control));
            iovec data{payload.data(), scenario == 3 ? sizeof(control) - 1 : payload.size()};
            std::array<int, 16> descriptors{};
            descriptors.fill(notification);
            descriptors[0] = memory;
            const std::size_t count{scenario == 8 ? 3U : (scenario == 9 ? 16U : 2U)};
            std::vector<char> ancillary(CMSG_SPACE(count * sizeof(int)));
            msghdr message{};
            message.msg_iov = &data; message.msg_iovlen = 1;
            message.msg_control = ancillary.data(); message.msg_controllen = ancillary.size();
            auto *header{CMSG_FIRSTHDR(&message)};
            header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = CMSG_LEN(count * sizeof(int));
            std::memcpy(CMSG_DATA(header), descriptors.data(), count * sizeof(int));
            check(::sendmsg(connection, &message, MSG_NOSIGNAL) == static_cast<ssize_t>(data.iov_len),
                  "malformed send");
            Control reply{};
            check(::recv(connection, &reply, sizeof(reply), 0) == sizeof(reply) && reply.result == EPROTO,
                  "malformed client accepted or rejection reason absent");
            check(region->consumer_pid.load() == 0, "rejected region claimed");
            ::close(connection); ::close(memory); ::close(notification); ::munmap(mapped, bytes);
            const auto deadline{monotonicNs() + 1000000000ULL};
            while (descriptorCount(worker) != baseline && monotonicNs() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            check(descriptorCount(worker) == baseline, "rejected descriptor leak");
            const auto before{healthy.region()->producers[0].normal.write.load()};
            diagnostics::emit(event(Type::Capture));
            while (healthy.region()->producers[0].normal.read.load() <= before && monotonicNs() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            check(healthy.region()->producers[0].normal.read.load() > before,
                  "healthy session stopped after invalid client");
        }
    }
    std::cout << "真实socket：12种拒绝各2轮、fd回收、健康会话继续通过\n";
}
void processTest(const std::filesystem::path &path, const char *worker, const char *inspector) {
    const std::string socket{(path / "worker.sock").string()}, directory{(path / "worker").string()};
    const pid_t pid{::fork()};
    if (pid == 0) {
        ::execl(worker, worker, "--socket", socket.c_str(), "--directory", directory.c_str(), "--minimum-free-bytes",
                "0", nullptr);
        ::_exit(127);
    }
    for (unsigned attempt{}; attempt < 100 && !std::filesystem::exists(socket); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    Client client{};
    check(client.prepare(Mode::Basic, socket, 700), "cross-process confirmation");
    rejectedConnections(socket, pid, client);
    Event value{event(Type::Capture)};
    value.values[0] = 42;
    diagnostics::emit(value);
    // Notification omitted: the worker's 10 ms poll still consumes the ring.
    auto &ring{client.region()->producers[0].normal};
    const auto write{ring.write.load()};
    value.producer_seq = client.region()->producers[0].position.generated.load() + 1;
    value.run_id = 700;
    value.mono_ns = monotonicNs();
    ring.events[write % NORMAL_CAPACITY] = value;
    ring.write.store(write + 1, std::memory_order_release);
    client.region()->producers[0].position.generated.store(value.producer_seq);
    check(client.finish(), "finite drain");
    ::kill(pid, SIGTERM);
    int status{};
    ::waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "worker stop");
    const std::string output{(path / "export").string()};
    const pid_t inspect{::fork()};
    if (inspect == 0) {
        ::execl(inspector, inspector, directory.c_str(), "--output", output.c_str(), nullptr);
        ::_exit(127);
    }
    ::waitpid(inspect, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "inspect export");
    check(std::filesystem::is_regular_file(path / "export/events.jsonl"), "jsonl");
    auto evidence_region{std::make_unique<SharedRegion>()};
    evidence_region->run_id = 901;
    Storage evidence{config(path / "evidence"), *evidence_region};
    check(evidence.open(), "evidence open");
    std::array<Event, 6> events{};
    events[0] = event(Type::DetectStart);
    events[0].run_id = 901;
    events[0].request_id = 17;
    events[0].producer_seq = 1;
    events[0].mono_ns = monotonicNs();
    events[1] = events[0];
    events[2] = event(Type::Gap, Reason::BufferFull);
    events[2].run_id = 901;
    events[2].values[0] = 2;
    events[2].values[1] = 2;
    events[2].values[2] = 3;
    events[3] = event(Type::InitializeEnd, Reason::InitFailed);
    events[3].values[0] = 2; events[3].values[1] = 1; events[3].values[2] = 6;
    events[4] = events[3]; events[4].flags |= ErrorCodeKnown;
    events[5] = events[4]; events[5].values[2] = 7;
    check(evidence.append(events.data(), events.size()) && evidence.finish(false), "evidence write");
    std::ifstream before_input{evidence.path(), std::ios::binary};
    const std::string before_bytes{std::istreambuf_iterator<char>{before_input}, {}};
    const std::uint32_t before_hash{crc32(before_bytes.data(), before_bytes.size())};
    auto inspect_copy{[&](const std::string &output_path) {
        const pid_t child{::fork()};
        if (child == 0) {
            ::execl(inspector, inspector, evidence.path().c_str(), "--output", output_path.c_str(), nullptr);
            ::_exit(127);
        }
        int exit_status{};
        ::waitpid(child, &exit_status, 0);
        return WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : 1;
    }};
    const auto evidence_output{(path / "evidence-export").string()};
    check(inspect_copy(evidence_output) == 0 && inspect_copy(evidence_output) != 0, "export overwrite not rejected");
    check(inspect_copy((evidence.path().parent_path() / "export").string()) != 0, "raw directory export accepted");
    std::ifstream timeline_input{path / "evidence-export/timeline.txt"};
    const std::string timeline{std::istreambuf_iterator<char>{timeline_input}, {}};
    check(timeline.find("重复事件 1") != std::string::npos && timeline.find("结果未知") != std::string::npos &&
              timeline.find("已知缺口") != std::string::npos,
          "insufficient evidence not visible");
    std::ifstream json_input{path / "evidence-export/events.jsonl"};
    const std::string json_text{std::istreambuf_iterator<char>{json_input}, {}};
    check(json_text.find("\"error_code_known\":false,\"error_domain\":null,\"error_code\":null") != std::string::npos &&
          json_text.find("\"error_code_known\":true,\"error_domain\":1,\"error_code\":6") != std::string::npos &&
          json_text.find("\"error_code_known\":true,\"error_domain\":1,\"error_code\":7") != std::string::npos,
          "旧日志未知有效性或两种错误JSONL导出不可区分");
    std::ifstream after_input{evidence.path(), std::ios::binary};
    const std::string after_bytes{std::istreambuf_iterator<char>{after_input}, {}};
    check(before_bytes == after_bytes && before_hash == crc32(after_bytes.data(), after_bytes.size()),
          "inspector modified original/hash");
    auto launch_worker{[&] {
        const pid_t child{::fork()};
        if (child == 0) {
            ::execl(worker, worker, "--socket", socket.c_str(), "--directory", directory.c_str(),
                    "--minimum-free-bytes", "0", nullptr);
            ::_exit(127);
        }
        return child;
    }};
    pid_t first_worker{launch_worker()};
    for (unsigned attempt{}; attempt < 100 && !std::filesystem::exists(socket); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    Client reconnecting{};
    check(reconnecting.prepare(Mode::Basic, socket, 701), "reconnect prepare");
    SharedRegion *original_region{reconnecting.region()};
    diagnostics::emit(event(Type::Capture));
    ::kill(first_worker, SIGKILL);
    ::waitpid(first_worker, &status, 0);
    pid_t replacement{launch_worker()};
    const auto reconnect_deadline{monotonicNs() + 7000000000ULL};
    bool connected{};
    while (!connected && monotonicNs() < reconnect_deadline) {
        connected = reconnecting.reconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    check(connected && reconnecting.region() == original_region, "unique same-region reconnect");
    const pid_t duplicate{launch_worker()};
    ::waitpid(duplicate, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) != 0, "second consumer accepted");
    check(original_region->consumer_pid.load() == static_cast<std::uint32_t>(replacement), "consumer identity changed");
    diagnostics::emit(event(Type::Capture));
    check(reconnecting.finish(), "reconnected drain");
    ::kill(replacement, SIGTERM);
    ::waitpid(replacement, &status, 0);
    Client unavailable{};
    const auto start{monotonicNs()};
    check(!unavailable.prepare(Mode::Basic, (path / "absent.sock").string()), "unavailable degrade");
    check(monotonicNs() - start < 500000000, "preparation budget");
    unavailable.finish(std::chrono::milliseconds{0});
}
} // namespace
int main(int argc, char **argv) {
    ::alarm(115);
    try {
        check(argc == 3, "program args");
        auto path{temporary()};
        ringTest();
        visibilityTest();
        storageTest(path);
        processTest(path, argv[1], argv[2]);
        std::filesystem::remove_all(path);
        std::cout << "诊断协议、跨进程、存储故障和恢复检查通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
