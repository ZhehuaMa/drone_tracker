#ifndef DRONE_DIAGNOSTICS_STORAGE_H
#define DRONE_DIAGNOSTICS_STORAGE_H

#include "protocol.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace diagnostics {
enum class RecordKind : std::uint16_t { Batch = 1, Commit, End };
struct RecordHeader {
    std::uint32_t magic{MAGIC};
    std::uint16_t version{FORMAT_VERSION};
    RecordKind kind{};
    std::uint32_t length{};
    std::uint32_t crc{};
    std::uint32_t endian_marker{ENDIAN_MARKER};
};
struct Commit {
    std::uint64_t data_end{};
    std::uint64_t mono_ns{};
    std::array<std::uint64_t, PRODUCERS> written{};
    std::array<std::uint64_t, PRODUCERS> dropped{};
};
struct StorageConfig {
    std::filesystem::path directory{"/var/lib/drone_tracker/diagnostics"};
    std::uint64_t segment_bytes{64ULL * 1024 * 1024};
    std::uint64_t total_bytes{1024ULL * 1024 * 1024};
    std::uint64_t minimum_free_bytes{5ULL * 1024 * 1024 * 1024};
    unsigned sync_ms{100};
};
/** @brief 可注入系统调用，仅用于受控故障检查；默认使用 Linux 文件接口。 */
struct StorageIo {
    std::function<std::ptrdiff_t(int, const void *, std::size_t)> write{};
    std::function<int(int)> sync{};
    std::function<int(int)> directory_sync{};
};
std::uint32_t crc32(const void *data, std::size_t length) noexcept;
/** @brief 收集进程独占文件；完整批次写入与双阶段同步分别推进位置。 */
class Storage final {
public:
    Storage(StorageConfig config, SharedRegion &region, StorageIo io = {});
    ~Storage() noexcept;
    bool open();
    bool append(const Event *events, std::size_t count);
    bool identity(const char *data, std::size_t size);
    bool synchronize();
    bool finish(bool normal);
    bool due(std::uint64_t now) const noexcept;
    int error() const noexcept { return error_; }
    std::uint64_t offset() const noexcept { return offset_; }
    const std::filesystem::path &path() const noexcept { return path_; }

private:
    bool newSegment();
    bool record(RecordKind kind, const void *data, std::size_t length);
    bool writeAll(const void *data, std::size_t length);
    bool protect(std::size_t incoming);
    bool fail(int error) noexcept;
    StorageConfig config_{};
    SharedRegion &region_;
    StorageIo io_{};
    std::filesystem::path path_{}, run_directory_{};
    int fd_{-1}, directory_fd_{-1}, error_{};
    std::uint64_t offset_{}, last_sync_{}, segment_{}, bytes_{};
    bool critical_{};
    std::array<std::uint64_t, PRODUCERS> written_{}, dropped_written_{};
};
struct Scan {
    std::vector<Event> events{};
    std::vector<Commit> commits{};
    std::uint64_t valid_bytes{};
    bool complete{}, damaged{}, unknown_version{};
};
/** @brief 只读恢复完整可校验批次前缀；文件大小不代表成功同步位置。 */
Scan scan(const std::filesystem::path &path);
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_STORAGE_H
