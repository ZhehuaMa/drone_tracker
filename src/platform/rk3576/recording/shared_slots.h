#ifndef RK3576_RECORDING_SHARED_SLOTS_H
#define RK3576_RECORDING_SHARED_SLOTS_H

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <new>
#include <stdexcept>
#include "nv12_copy.h"

namespace rk3576 {
/** @brief 每轮独立的四槽共享区；生产者不等待消费者，不覆盖已发布或读取中的槽。 */
class RecordingSlots final : public CameraNv12Sink {
public:
    enum class State : std::uint32_t { Empty, Writing, Ready, Reading };
    struct Identity {
        CameraFormat format{};
        std::int64_t timestamp_us{};
        std::int64_t driver_seconds{};
        std::int64_t driver_microseconds{};
        std::uint32_t flags{};
        std::uint32_t sequence{};
        std::uint64_t round{};
    };
    struct Slot {
        std::atomic<State> state{State::Empty};
        Identity identity{};
    };
    struct Event {
        Identity identity{};
        // 1=槽满，2=无效画面，3=暂存后撤销，4=录像已停止接收，5=编码输出背压。
        std::uint32_t kind{};
        std::uint32_t first_sequence{}, last_sequence{};
        std::uint64_t count{};
    };
    struct Header {
        std::array<Slot, 4> slots{};
        std::atomic<bool> stop_accepting{false};
        std::atomic<bool> backpressure{false};
        std::atomic<bool> stopping{false};
        std::atomic<bool> ready{false};
        std::atomic<bool> finished{false};
        std::atomic<unsigned> failure{0}; // 0=健康，1=正在写原因，2=原因已发布。
        std::array<char, 512> reason{};
        std::array<char, 4096> session_path{};
        std::atomic<std::uint64_t> attempts{0}, accepted{0}, dropped{0}, cancelled{0};
        std::atomic<std::uint64_t> encoded{0}, written{0}, synced{0};
        std::atomic<std::uint64_t> total_bytes{0}, free_bytes{0};
        std::atomic<std::int64_t> first_input_mono_us{0}, latest_frame_us{0};
        std::atomic<std::int64_t> last_idr_mono_us{0}, last_synced_frame_us{0};
        std::atomic<std::int64_t> last_sync_mono_us{0}, sync_started_us{0}, sync_duration_us{0};
        std::array<Event, 512> events{};
        std::atomic<std::uint64_t> event_write{0}, event_read{0};
        // 相机停止后一次性发布有界预热/终止统计，子进程 acquire 后才能读取。
        std::array<char, 65536> camera_summary{};
        std::atomic<std::size_t> summary_size{0};
    };
    struct DropRange {
        std::uint32_t first{};
        std::uint32_t last{};
        std::uint64_t count{};
    };
    struct Statistics {
        std::uint64_t published{};
        std::uint64_t full{};
        std::uint64_t invalid{};
        std::uint64_t cancelled{};
        std::array<DropRange, 128> ranges{};
        std::size_t range_count{};
        std::uint64_t omitted_ranges{};
    };

    /** @brief 创建并预触及共享内存；必须在采集开始前调用。 */
    RecordingSlots(std::uint32_t width, std::uint32_t height, std::uint64_t round)
        : width_{width}, height_{height}, round_{round} {
        static_assert(std::atomic<State>::is_always_lock_free,
                      "Shared slots require lock-free 32-bit atomics");
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free
            && std::atomic<std::int64_t>::is_always_lock_free
            && std::atomic<std::size_t>::is_always_lock_free
            && std::atomic<bool>::is_always_lock_free,
            "Shared recording metadata requires lock-free atomics");
        const std::uint64_t pixels{static_cast<std::uint64_t>(width) * height};
        if (!width || !height || width % 2 || height % 2 || pixels > 64U * 1024U * 1024U / 6) {
            throw std::invalid_argument("Recording raw slots exceed 4 frames / 64 MiB or invalid NV12 size");
        }
        frame_bytes_ = pixels * 3 / 2;
        mapping_bytes_ = sizeof(Header) + 4 * frame_bytes_;
        fd_ = ::memfd_create("drone-recording-slots", MFD_CLOEXEC);
        if (fd_ < 0) { throw std::runtime_error("Recording memfd creation failed"); }
        if (::ftruncate(fd_, static_cast<off_t>(mapping_bytes_)) != 0) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Recording shared memory sizing failed");
        }
        mapping_ = ::mmap(nullptr, mapping_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapping_ == MAP_FAILED) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Recording shared memory mapping failed");
        }
        std::memset(mapping_, 0, mapping_bytes_);
        header_ = ::new (mapping_) Header{};
    }
    /** @brief 子进程映射已有槽，不重建原子对象或修改旧轮状态。 */
    RecordingSlots(int descriptor, std::uint32_t width, std::uint32_t height, std::uint64_t round)
        : width_{width}, height_{height}, round_{round} {
        const std::uint64_t pixels{static_cast<std::uint64_t>(width) * height};
        if (!width || !height || width % 2 || height % 2 || pixels > 64U * 1024U * 1024U / 6) {
            throw std::invalid_argument("Invalid attached recording dimensions");
        }
        frame_bytes_ = pixels * 3 / 2;
        mapping_bytes_ = sizeof(Header) + 4 * frame_bytes_;
        struct stat info{};
        if (::fstat(descriptor, &info) || info.st_size != static_cast<off_t>(mapping_bytes_)) {
            throw std::runtime_error("Recording shared mapping size mismatch");
        }
        fd_ = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
        if (fd_ < 0) { throw std::runtime_error("Recording shared descriptor duplication failed"); }
        mapping_ = ::mmap(nullptr, mapping_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapping_ == MAP_FAILED) {
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Recording child mapping failed");
        }
        header_ = static_cast<Header *>(mapping_);
    }
    ~RecordingSlots() override {
        // 持有者必须等工作进程退出再销毁；活跃子进程的资源由会话管理器隔离。
        if (mapping_ != MAP_FAILED) { ::munmap(mapping_, mapping_bytes_); }
        if (fd_ >= 0) { ::close(fd_); }
    }
    RecordingSlots(const RecordingSlots &) = delete;
    RecordingSlots &operator=(const RecordingSlots &) = delete;

    void stage(const CameraNv12View &view) noexcept override {
        cancel();
        pending_identity_ = {view.format, view.timestamp_us, view.driver_seconds, view.driver_microseconds,
                             view.flags, view.sequence, round_};
        pending_full_ = false;
        pending_invalid_ = false;
        pending_stopped_ = header_->stop_accepting.load(std::memory_order_acquire);
        if (pending_stopped_) { return; }
        pending_backpressure_ = header_->backpressure.load(std::memory_order_acquire);
        if (pending_backpressure_) { pending_full_ = true; return; }
        if (view.format.width != width_ || view.format.height != height_) { pending_invalid_ = true; return; }
        for (std::size_t index{}; index < 4; ++index) {
            State expected{State::Empty};
            if (!header_->slots[index].state.compare_exchange_strong(expected, State::Writing, std::memory_order_acquire)) {
                continue;
            }
            if (!copyNv12(view, pixels(index), frame_bytes_)) {
                header_->slots[index].state.store(State::Empty, std::memory_order_release);
                pending_invalid_ = true;
                return;
            }
            header_->slots[index].identity = pending_identity_;
            pending_ = static_cast<int>(index);
            return;
        }
        pending_full_ = true;
    }
    void publish() noexcept override {
        header_->attempts.fetch_add(1, std::memory_order_relaxed);
        header_->latest_frame_us.store(pending_identity_.timestamp_us, std::memory_order_relaxed);
        const auto now{monotonicUs()};
        std::int64_t unset{0};
        header_->first_input_mono_us.compare_exchange_strong(unset, now);
        if (pending_ >= 0 && header_->stop_accepting.load(std::memory_order_acquire)) {
            header_->slots[pending_].state.store(State::Empty, std::memory_order_release);
            pending_ = -1;
            pending_stopped_ = true;
        }
        if (pending_ >= 0) {
            header_->slots[pending_].state.store(State::Ready, std::memory_order_release);
            ++statistics_.published;
            header_->accepted.fetch_add(1, std::memory_order_relaxed);
            pending_ = -1;
        } else if (pending_full_) {
            ++statistics_.full;
            recordDrop(pending_identity_.sequence);
            header_->dropped.fetch_add(1, std::memory_order_relaxed);
            recordEvent(pending_backpressure_ ? 5 : 1);
        } else if (pending_invalid_) {
            ++statistics_.invalid;
            header_->dropped.fetch_add(1, std::memory_order_relaxed);
            recordEvent(2);
            fail("Invalid NV12 recording input");
        } else if (pending_stopped_) {
            header_->dropped.fetch_add(1, std::memory_order_relaxed);
            // 停止之后只更新累计计数；失败轮不伪造完整逐帧证据。
        }
        pending_full_ = false;
        pending_invalid_ = false;
    }
    void cancel() noexcept override {
        if (pending_ >= 0) {
            header_->slots[pending_].state.store(State::Empty, std::memory_order_release);
            pending_ = -1;
            ++statistics_.cancelled;
            header_->cancelled.fetch_add(1, std::memory_order_relaxed);
            recordEvent(3);
        }
        pending_full_ = false;
        pending_invalid_ = false;
    }
    void disposition(const CameraSample &sample, std::uint64_t gaps, bool tracking_dropped) noexcept override {
        if (header_->stop_accepting.load(std::memory_order_acquire)) { return; }
        if (!gaps && !tracking_dropped) { return; }
        Identity identity{};
        identity.round = round_; identity.sequence = sample.sequence; identity.flags = sample.flags;
        identity.driver_seconds = sample.seconds; identity.driver_microseconds = sample.microseconds;
        if (sample.result == CameraSampleResult::FrameReady) { identity = pending_identity_; }
        if (gaps) {
            appendEvent({identity, 6, sample.sequence - static_cast<std::uint32_t>(gaps),
                sample.sequence - 1, gaps});
        }
        if (tracking_dropped) { appendEvent({identity, 7, sample.sequence, sample.sequence, 1}); }
    }
    /** @brief 单消费者取得一个已发布槽；release 之前生产者不会重用该槽。 */
    int acquire() noexcept {
        int selected{-1};
        for (std::size_t index{}; index < 4; ++index) {
            if (header_->slots[index].state.load(std::memory_order_acquire) != State::Ready) { continue; }
            if (selected < 0 || header_->slots[index].identity.timestamp_us
                < header_->slots[selected].identity.timestamp_us) { selected = static_cast<int>(index); }
        }
        if (selected >= 0) { header_->slots[selected].state.store(State::Reading, std::memory_order_relaxed); }
        return selected;
    }

    void release(std::size_t index) noexcept { header_->slots[index].state.store(State::Empty, std::memory_order_release); }
    const Identity &identity(std::size_t index) const noexcept { return header_->slots[index].identity; }
    std::uint8_t *pixels(std::size_t index) noexcept {
        return static_cast<std::uint8_t *>(mapping_) + sizeof(Header) + index * frame_bytes_;
    }
    int fd() const noexcept { return fd_; }
    std::size_t mappingBytes() const noexcept { return mapping_bytes_; }
    std::size_t frameBytes() const noexcept { return frame_bytes_; }
    Header &header() noexcept { return *header_; }
    static std::int64_t monotonicUs() noexcept {
        timespec time{};
        if (::clock_gettime(CLOCK_MONOTONIC, &time)) { return 0; }
        return static_cast<std::int64_t>(time.tv_sec) * 1000000 + time.tv_nsec / 1000;
    }
    /** @brief 第一个故障拥有原因缓冲；失败原因发布后保持不变。 */
    void fail(const char *reason) noexcept {
        header_->stop_accepting.store(true, std::memory_order_release);
        unsigned expected{0};
        if (header_->failure.compare_exchange_strong(expected, 1)) {
            std::strncpy(header_->reason.data(), reason, header_->reason.size() - 1);
            header_->failure.store(2, std::memory_order_release);
        }
    }
    bool nextEvent(Event &event) noexcept {
        const auto read{header_->event_read.load(std::memory_order_relaxed)};
        if (read == header_->event_write.load(std::memory_order_acquire)) { return false; }
        event = header_->events[read % header_->events.size()];
        header_->event_read.store(read + 1, std::memory_order_release);
        return true;
    }
    /** @brief 仅生产者线程或停止采集后访问；会话管理器负责发布状态快照。 */
    const Statistics &statistics() const noexcept { return statistics_; }

private:
    void recordEvent(unsigned kind) noexcept {
        appendEvent({pending_identity_, kind, pending_identity_.sequence, pending_identity_.sequence, 1});
    }
    void appendEvent(const Event &event) noexcept {
        const auto write{header_->event_write.load(std::memory_order_relaxed)};
        if (write - header_->event_read.load(std::memory_order_acquire) >= header_->events.size()) {
            fail("Recording metadata queue full; evidence incomplete");
            return;
        }
        header_->events[write % header_->events.size()] = event;
        header_->event_write.store(write + 1, std::memory_order_release);
    }
    void recordDrop(std::uint32_t sequence) noexcept {
        if (statistics_.range_count) {
            auto &last{statistics_.ranges[statistics_.range_count - 1]};
            if (sequence == last.last + 1U) { last.last = sequence; ++last.count; return; }
        }
        if (statistics_.range_count < statistics_.ranges.size()) {
            statistics_.ranges[statistics_.range_count++] = {sequence, sequence, 1};
        } else { ++statistics_.omitted_ranges; }
    }
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint64_t round_{};
    int fd_{-1};
    void *mapping_{MAP_FAILED};
    Header *header_{};
    std::size_t frame_bytes_{};
    std::size_t mapping_bytes_{};
    int pending_{-1};
    bool pending_full_{};
    bool pending_invalid_{};
    bool pending_stopped_{};
    bool pending_backpressure_{};
    Identity pending_identity_{};
    Statistics statistics_{};
};
} // namespace rk3576
#endif // RK3576_RECORDING_SHARED_SLOTS_H
