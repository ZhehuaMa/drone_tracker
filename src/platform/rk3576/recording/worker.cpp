#include "encoder.h"

#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

#include "storage.h"

#include <yaml-cpp/yaml.h>

namespace {
using rk3576::RecordingSlots;
using namespace rk3576::recording;
constexpr std::size_t OUTPUT_LIMIT{8 * 1024 * 1024};
struct Queue {
    std::mutex mutex{};
    std::condition_variable changed{};
    std::deque<EncodedFrame> frames{};
    std::deque<RecordingSlots::Event> events{};
    std::size_t bytes{}; // 包括写线程在途项及已预留的编码输出。
    bool stopping{};
};
std::string readConfiguration() {
    std::string text{};
    std::array<char, 4096> chunk{};
    while (true) {
        const auto count{::read(4, chunk.data(), chunk.size())};
        if (count < 0 && errno == EINTR) { continue; }
        if (count < 0) { throw std::runtime_error("Cannot read recording worker configuration"); }
        if (!count) { break; }
        if (text.size() + count > 131072) { throw std::runtime_error("Oversized worker configuration"); }
        text.append(chunk.data(), count);
    }
    ::close(4);
    return text;
}
} // namespace
int main() {
    std::unique_ptr<RecordingSlots> slots{};
    try {
        const pid_t parent{::getppid()};
        if (parent == 1 || ::prctl(PR_SET_PDEATHSIG, SIGTERM) || ::getppid() != parent) {
            throw std::runtime_error("Recording parent already exited");
        }
        const auto manifest{readConfiguration()};
        const auto values{YAML::Load(manifest)};
        rk3576::CameraFormat format{};
        format.width = values["width"].as<unsigned>(); format.height = values["height"].as<unsigned>();
        format.stride = values["stride"].as<unsigned>(); format.quantization = values["quantization"].as<unsigned>();
        format.colorspace = values["colorspace"].as<unsigned>(); format.ycbcr_encoding = values["ycbcr"].as<unsigned>();
        format.transfer_function = values["transfer"].as<unsigned>();
        const auto round{values["round"].as<std::uint64_t>()};
        slots = std::make_unique<RecordingSlots>(3, format.width, format.height, round);
        ::close(3);
        CameraRecordingConfig config{};
        config.enabled = true; config.directory = values["directory"].as<std::string>();
        config.identity = values["identity"].as<std::string>(); config.bitrate_bps = values["bitrate_bps"].as<std::int64_t>();
        config.segment_seconds = values["segment_seconds"].as<int>();
        config.sync_interval_ms = values["sync_interval_ms"].as<int>();
        config.max_total_bytes = values["max_total_bytes"].as<std::uint64_t>();
        config.min_free_bytes = values["min_free_bytes"].as<std::uint64_t>();
        auto &h{slots->header()};
        Encoder encoder{format, values["fps"].as<int>(), config.bitrate_bps};
        Storage storage{config, format, round, manifest, h};
        if (storage.directory().size() >= h.session_path.size()) { throw std::runtime_error("Session path too long"); }
        std::copy(storage.directory().begin(), storage.directory().end(), h.session_path.begin());
        Queue queue{};
        std::thread writer{[&] {
            try {
                while (true) {
                    EncodedFrame frame{};
                    std::deque<RecordingSlots::Event> events{};
                    bool stopping{};
                    {
                        std::unique_lock<std::mutex> lock{queue.mutex};
                        queue.changed.wait_for(lock, std::chrono::milliseconds{100}, [&] {
                            return queue.stopping || !queue.frames.empty() || !queue.events.empty();
                        });
                        events.swap(queue.events);
                        if (!queue.frames.empty()) { frame = std::move(queue.frames.front()); queue.frames.pop_front(); }
                        stopping = queue.stopping && queue.frames.empty() && frame.bytes.empty();
                    }
                    if (h.failure.load()) { return; }
                    for (const auto &event : events) { storage.writeEvent(event); }
                    if (!frame.bytes.empty()) {
                        storage.writeFrame(frame);
                        std::lock_guard<std::mutex> lock{queue.mutex};
                        queue.bytes -= frame.bytes.size();
                    }
                    storage.tick();
                    if (stopping) {
                        const auto size{h.summary_size.load(std::memory_order_acquire)};
                        if (!size || size >= h.camera_summary.size()) { throw std::runtime_error("Camera final summary missing"); }
                        storage.finish(std::string{h.camera_summary.data(), size});
                        h.finished.store(true, std::memory_order_release);
                        return;
                    }
                }
            } catch (const std::exception &error) { slots->fail(error.what()); }
        }};
        h.ready.store(true, std::memory_order_release);
        std::int64_t first_timestamp{-1}, next_idr{};
        const std::size_t reservation{static_cast<std::size_t>((format.width + 15) / 16 * 16)
            * ((format.height + 15) / 16 * 16)};
        try {
            if (reservation > OUTPUT_LIMIT) { throw std::runtime_error("MPP output reservation exceeds 8 MiB"); }
            while (!h.failure.load()) {
                pollfd parent_socket{5, POLLIN | POLLHUP, 0};
                if (::poll(&parent_socket, 1, 0) < 0 && errno != EINTR) {
                    throw std::runtime_error("Recording parent channel poll failed");
                }
                if (parent_socket.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    throw std::runtime_error("Recording parent channel closed");
                }
                RecordingSlots::Event event{};
                while (slots->nextEvent(event)) {
                    std::lock_guard<std::mutex> lock{queue.mutex};
                    if (queue.events.size() >= 512) { throw std::runtime_error("Recording storage event queue full"); }
                    queue.events.push_back(event);
                }
                bool room{};
                {
                    std::lock_guard<std::mutex> lock{queue.mutex};
                    room = reservation <= OUTPUT_LIMIT - queue.bytes;
                    if (room) { queue.bytes += reservation; }
                }
                h.backpressure.store(!room, std::memory_order_release);
                if (!room) { std::this_thread::sleep_for(std::chrono::milliseconds{1}); continue; }
                const int index{slots->acquire()};
                if (index < 0) {
                    { std::lock_guard<std::mutex> lock{queue.mutex}; queue.bytes -= reservation; }
                    if (h.stopping.load(std::memory_order_acquire)) { break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    continue;
                }
                const auto identity{slots->identity(index)};
                if (identity.round != round) { throw std::runtime_error("Old recording frame round rejected"); }
                if (first_timestamp < 0) { first_timestamp = identity.timestamp_us; }
                const auto pts{identity.timestamp_us - first_timestamp};
                const bool idr{pts >= next_idr};
                auto encoded{encoder.encode(slots->pixels(index), identity, pts, idr, [&] { slots->release(index); })};
                if (encoded.bytes.size() > reservation) { throw std::runtime_error("MPP packet exceeds reserved output bound"); }
                if (idr) { next_idr = pts + 1000000; }
                if (encoded.idr) { h.last_idr_mono_us.store(RecordingSlots::monotonicUs()); }
                h.encoded.fetch_add(1);
                {
                    std::lock_guard<std::mutex> lock{queue.mutex};
                    queue.bytes -= reservation - encoded.bytes.size();
                    queue.frames.push_back(std::move(encoded));
                }
                queue.changed.notify_one();
            }
        } catch (const std::exception &error) { slots->fail(error.what()); }
        { std::lock_guard<std::mutex> lock{queue.mutex}; queue.stopping = true; }
        queue.changed.notify_one();
        writer.join(); // 父进程负责此进程的有界超时；阻塞 I/O 不会拖住采集进程。
        const auto failure{h.failure.load(std::memory_order_acquire)};
        if (failure) {
            throw std::runtime_error(failure == 2 ? h.reason.data() : "Recording failure publication pending");
        }
        return h.finished.load() ? 0 : 1;
    } catch (const std::exception &error) {
        if (slots) { slots->fail(error.what()); }
        std::cerr << "Recording failed: " << error.what() << '\n';
        return 1;
    }
}
