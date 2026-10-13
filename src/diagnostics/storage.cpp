#include "storage.h"
#include "recorder.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/statvfs.h>
#include <unistd.h>

namespace diagnostics {
std::uint32_t crc32(const void *data, std::size_t length) noexcept {
    const auto *bytes{static_cast<const unsigned char *>(data)};
    std::uint32_t crc{0xffffffffU};
    for (std::size_t index{0}; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned bit{0}; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}
Storage::Storage(StorageConfig config, SharedRegion &region, StorageIo io)
    : config_{std::move(config)}, region_{region}, io_{std::move(io)} {
    for (std::size_t index{}; index < PRODUCERS; ++index) {
        written_[index] = region_.producers[index].position.synced.load(std::memory_order_acquire);
    }
    if (!io_.write) {
        io_.write = [](int fd, const void *data, std::size_t size) { return ::write(fd, data, size); };
    }
    if (!io_.sync) {
        io_.sync = [](int fd) { return ::fdatasync(fd); };
    }
    if (!io_.directory_sync) {
        io_.directory_sync = [](int fd) { return ::fsync(fd); };
    }
}
Storage::~Storage() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    if (directory_fd_ >= 0) {
        ::close(directory_fd_);
    }
}
bool Storage::fail(int error) noexcept {
    error_ = error ? error : EIO;
    region_.error.store(static_cast<std::uint32_t>(error_), std::memory_order_release);
    return false;
}
bool Storage::open() {
    if (config_.sync_ms != 100 && config_.sync_ms != 200) {
        return fail(EINVAL);
    }
    std::error_code error{};
    std::filesystem::create_directories(config_.directory, error);
    if (error) {
        return fail(error.value());
    }
    const int parent{::open(config_.directory.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (parent < 0) {
        return fail(errno);
    }
    const int result{io_.directory_sync(parent)};
    ::close(parent);
    if (result != 0) {
        return fail(errno);
    }
    run_directory_ = config_.directory / std::to_string(region_.run_id);
    std::filesystem::create_directory(run_directory_, error);
    if (error) {
        return fail(error.value());
    }
    directory_fd_ = ::open(run_directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int root{::open(config_.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (directory_fd_ < 0 || root < 0) {
        if (root >= 0) {
            ::close(root);
        }
        return fail(errno);
    }
    const int synced{io_.directory_sync(root)};
    ::close(root);
    if (synced != 0) {
        return fail(errno);
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator(config_.directory)) {
        if (entry.is_regular_file()) {
            bytes_ += entry.file_size();
        }
    }
    return newSegment();
}
bool Storage::identity(const char *data, std::size_t size) {
    const auto path{run_directory_ / "identity.json"};
    const int fd{::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
    if (fd < 0) {
        return errno == EEXIST || fail(errno);
    }
    const auto written{::write(fd, data, size)};
    const bool okay{written == static_cast<std::ptrdiff_t>(size) && io_.sync(fd) == 0};
    ::close(fd);
    return okay && io_.directory_sync(directory_fd_) == 0 ? true : fail(errno);
}
bool Storage::protect(std::size_t incoming) {
    // 收集器单循环可能同时保存两会话；按全目录实际大小保护，避免各自超额。
    bytes_ = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(config_.directory)) {
        if (entry.is_regular_file()) {
            bytes_ += entry.file_size();
        }
    }
    struct statvfs info{};
    if (::statvfs(config_.directory.c_str(), &info) != 0) {
        return fail(errno);
    }
    if (bytes_ + incoming > config_.total_bytes ||
        static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize < config_.minimum_free_bytes + incoming) {
        return fail(ENOSPC);
    }
    return true;
}
bool Storage::newSegment() {
    if (!protect(sizeof(RecordHeader))) {
        return false;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    // 每次接管的文件名包含进程和单调时间；O_EXCL 禁止覆盖历史或续写异常尾部。
    path_ = run_directory_ / ("segment-" + std::to_string(::getpid()) + "-" + std::to_string(monotonicNs()) + "-" +
                              std::to_string(segment_++) + ".dtd");
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    offset_ = 0;
    if (fd_ < 0) {
        return fail(errno);
    }
    if (io_.directory_sync(directory_fd_) != 0) {
        return fail(errno);
    }
    return true;
}
bool Storage::writeAll(const void *data, std::size_t length) {
    const auto *bytes{static_cast<const char *>(data)};
    std::size_t done{};
    while (done < length) {
        const std::ptrdiff_t count{io_.write(fd_, bytes + done, length - done)};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return fail(errno);
        }
        done += static_cast<std::size_t>(count);
    }
    offset_ += length;
    bytes_ += length;
    return true;
}
bool Storage::record(RecordKind kind, const void *data, std::size_t length) {
    if (error_ || length > 1024 * 1024 || !protect(sizeof(RecordHeader) + length)) {
        return false;
    }
    RecordHeader header{};
    header.kind = kind;
    header.length = static_cast<std::uint32_t>(length);
    header.crc = crc32(data, length) ^ crc32(&header, sizeof(header));
    return writeAll(&header, sizeof(header)) && writeAll(data, length);
}
bool Storage::append(const Event *events, std::size_t count) {
    if (count == 0) {
        return true;
    }
    const std::size_t size{sizeof(Event) * count};
    if (offset_ + size + sizeof(RecordHeader) + sizeof(Commit) > config_.segment_bytes) {
        if (!synchronize() || !newSegment()) {
            return false;
        }
    }
    if (!record(RecordKind::Batch, events, size)) {
        return false;
    }
    for (std::size_t index{}; index < count; ++index) {
        const Event &event{events[index]};
        if (event.producer_id < PRODUCERS && event.producer_seq != 0) {
            written_[event.producer_id] = std::max(written_[event.producer_id], event.producer_seq);
            region_.producers[event.producer_id].position.written.store(written_[event.producer_id],
                                                                        std::memory_order_release);
        }
        if (event.type == Type::Gap && event.producer_id < PRODUCERS && event.values[5] == 0) {
            dropped_written_[event.producer_id] = event.values[0];
        }
        critical_ = critical_ || (event.flags & Critical) != 0;
    }
    if (region_.oldest_unsynced_ns.load(std::memory_order_acquire) == 0) {
        region_.oldest_unsynced_ns.store(events[0].mono_ns, std::memory_order_release);
    }
    return true;
}
bool Storage::due(std::uint64_t now) const noexcept {
    return critical_ || now - last_sync_ >= static_cast<std::uint64_t>(config_.sync_ms) * 1000000;
}
bool Storage::synchronize() {
    if (error_) {
        return false;
    }
    region_.sync_started_ns.store(monotonicNs(), std::memory_order_release);
    if (io_.sync(fd_) != 0) {
        return fail(errno);
    }
    Commit commit{};
    commit.data_end = offset_;
    commit.mono_ns = monotonicNs();
    commit.written = written_;
    for (std::size_t index{}; index < PRODUCERS; ++index) {
        commit.dropped[index] = dropped_written_[index];
    }
    if (!record(RecordKind::Commit, &commit, sizeof(commit)) || io_.sync(fd_) != 0) {
        return fail(errno);
    }
    for (std::size_t index{}; index < PRODUCERS; ++index) {
        region_.producers[index].position.synced.store(written_[index], std::memory_order_release);
    }
    last_sync_ = monotonicNs();
    region_.sync_finished_ns.store(last_sync_, std::memory_order_release);
    region_.oldest_unsynced_ns.store(0, std::memory_order_release);
    critical_ = false;
    return true;
}
bool Storage::finish(bool normal) {
    const std::uint32_t complete{normal ? 1U : 0U};
    return record(RecordKind::End, &complete, sizeof(complete)) && synchronize();
}
Scan scan(const std::filesystem::path &path) {
    Scan result{};
    std::uint64_t end_boundary{};
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.damaged = true;
        return result;
    }
    while (true) {
        RecordHeader header{};
        input.read(reinterpret_cast<char *>(&header), sizeof(header));
        if (input.gcount() == 0 && input.eof()) {
            break;
        }
        if (input.gcount() != sizeof(header) || header.magic != MAGIC || header.endian_marker != ENDIAN_MARKER ||
            header.length > 1024 * 1024) {
            result.damaged = true;
            break;
        }
        if (header.version != FORMAT_VERSION) {
            result.unknown_version = true;
            break;
        }
        std::vector<char> payload(header.length);
        input.read(payload.data(), header.length);
        const std::uint32_t expected_crc{header.crc};
        header.crc = 0;
        if (input.gcount() != header.length ||
            (crc32(payload.data(), payload.size()) ^ crc32(&header, sizeof(header))) != expected_crc) {
            result.damaged = true;
            break;
        }
        if (header.kind == RecordKind::Batch && header.length != 0 && header.length % sizeof(Event) == 0) {
            const std::size_t start{result.events.size()};
            result.events.resize(start + header.length / sizeof(Event));
            std::memcpy(result.events.data() + start, payload.data(), header.length);
            bool valid_events{true};
            for (std::size_t index{start}; index < result.events.size(); ++index) {
                if (result.events[index].format_version != FORMAT_VERSION ||
                    result.events[index].byte_length != sizeof(Event) ||
                    result.events[index].producer_id >= PRODUCERS) {
                    valid_events = false;
                    break;
                }
            }
            if (!valid_events) {
                result.events.resize(start);
                result.damaged = true;
                break;
            }
        } else if (header.kind == RecordKind::Commit && header.length == sizeof(Commit)) {
            Commit commit{};
            std::memcpy(&commit, payload.data(), sizeof(commit));
            if (commit.data_end != result.valid_bytes) {
                result.damaged = true;
                break;
            }
            result.commits.push_back(commit);
        } else if (header.kind == RecordKind::End && header.length == sizeof(std::uint32_t)) {
            std::uint32_t complete{};
            std::memcpy(&complete, payload.data(), sizeof(complete));
            if (complete > 1) {
                result.damaged = true;
                break;
            }
            result.complete = complete == 1;
            end_boundary = result.valid_bytes + sizeof(header) + header.length;
        } else {
            result.damaged = true;
            break;
        }
        result.valid_bytes += sizeof(header) + header.length;
    }
    if (result.commits.empty() || result.commits.back().data_end < end_boundary) {
        result.complete = false;
    }
    return result;
}
} // namespace diagnostics
