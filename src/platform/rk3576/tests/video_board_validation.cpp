#include "../platform_factory.h"
#include "../video_frame_source.h"
#include "platform/frame_source_ops.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
std::unique_ptr<FrameSource> create(const std::string &path, bool history) {
    FrameSourceConfig config{};
    config.type = FrameSourceType::Video;
    config.path = path;
    config.use_history_timestamp = history;
    auto factory{rk3576::createFactory({})};
    auto result{factory->createFrameSource(config)};
    require(static_cast<bool>(result), result.error.describe());
    return std::move(result.value);
}
rk3576::VideoDiagnostic diagnostic(FrameSource &source) {
    return dynamic_cast<rk3576::Rk3576VideoFrameSource &>(source).diagnostic();
}
double stop(FrameSource &source) {
    const auto before{Clock::now()};
    require(source.stop(), "Stop failed");
    const double elapsed{std::chrono::duration<double, std::milli>(Clock::now() - before).count()};
    require(elapsed < 1000, "Stop exceeded one second");
    return elapsed;
}
void finish(FrameSource &source) {
    const auto limit{Clock::now() + 5s};
    while (source.isRunning() && Clock::now() < limit) {
        std::this_thread::sleep_for(1ms);
    }
    require(!source.isRunning(), "EOF deadline exceeded");
}
FrameRef first(FrameSource &source) {
    require(source.start(), "Start failed");
    FrameRef frame{};
    require(source.waitFrame(frame, 1s), "First frame missing");
    return frame;
}
void resources(const std::string &phase, int round, Clock::time_point origin) {
    std::ifstream status{"/proc/self/status"};
    std::string key{}, value{}, rss{}, threads{}, size{};
    while (std::getline(status, value)) {
        if (value.rfind("VmRSS:", 0) == 0) { rss = value.substr(6); }
        if (value.rfind("VmSize:", 0) == 0) { size = value.substr(7); }
        if (value.rfind("Threads:", 0) == 0) { threads = value.substr(8); }
    }
    std::size_t descriptors{};
    for (const auto &entry : std::filesystem::directory_iterator{"/proc/self/fd"}) {
        (void)entry;
        ++descriptors;
    }
    std::cout << "resource phase=" << phase << " round=" << round << " seconds="
              << std::chrono::duration<double>(Clock::now() - origin).count()
              << " rss=" << rss << " vmsize=" << size << " threads=" << threads
              << " fd=" << descriptors << std::endl;
}
void functional(const std::string &root) {
    for (bool history : {true, false}) {
        auto source{create(root + "/420.mov", history)};
        std::uint64_t previous{};
        for (int round{1}; round <= 20; ++round) {
            auto frame{first(*source)};
            require(!source->start(), "Running start accepted");
            require(frame->timestampUs() > previous || history, "Realtime base did not advance");
            require(!history || frame->timestampUs() == 1700000000000000ULL, "History first PTS changed");
            previous = frame->timestampUs();
            const auto pixel{frame->mapRead()->mat().at<unsigned char>(0, 0)};
            if (round % 2 == 0) { finish(*source); }
            const double elapsed{stop(*source)};
            std::cout << "restart history=" << history << " round=" << round
                      << " first_us=" << previous << " pixel=" << static_cast<int>(pixel)
                      << " stop_ms=" << elapsed << " state=" << static_cast<int>(diagnostic(*source).state)
                      << std::endl;
            // 偶数轮故意保留尾帧，下一轮必须仍交付首帧。
        }
    }
    auto long_source{create(root + "/long.mov", true)};
    first(*long_source);
    std::this_thread::sleep_for(100ms);
    require(long_source->isRunning(), "Long wait ended unexpectedly");
    std::cout << "long_wait stop_ms=" << stop(*long_source) << std::endl;
    auto bad{create(root + "/bad.mov", true)};
    for (int round{1}; round <= 2; ++round) {
        first(*bad);
        finish(*bad);
        stop(*bad);
        const auto stats{diagnostic(*bad)};
        require(stats.state == rk3576::VideoState::Failed, "Bad JPEG confused with EOF");
        std::cout << "bad_jpeg round=" << round << " state=" << static_cast<int>(stats.state)
                  << " reason=" << stats.reason << std::endl;
    }
    bool rejected{};
    try { create(root + "/bad-artist.mov", true); }
    catch (const std::exception &error) {
        rejected = true;
        std::cout << "bad_artist rejected=" << error.what() << std::endl;
    }
    require(rejected, "Invalid Artist accepted");
    auto full{create(root + "/slow.mov", true)};
    require(full->start(), "Queue test start failed");
    finish(*full);
    stop(*full);
    const auto stats{diagnostic(*full)};
    require(stats.read == 90 && stats.dropped > 0 && stats.enqueued + stats.dropped == 90,
            "Queue full accounting failed");
    FrameRef frame{};
    std::uint64_t count{};
    while (full->tryFrame(frame)) {
        require(frame->timestampUs() == 1700000000000000ULL + count * 33333, "Queue evicted old frame");
        ++count;
    }
    require(count == stats.enqueued, "EOF tail missing");
    std::cout << "full_queue read=" << stats.read << " enqueued=" << stats.enqueued
              << " dropped=" << stats.dropped << " tail_consumed=" << count << std::endl;
}
void lifecycle(const std::string &root, int seconds) {
    const auto origin{Clock::now()};
    FrameRef held{};
    std::unique_ptr<FrameReadView> view{};
    int expected{};
    resources("initial", 0, origin);
    for (int round{1}; round <= 100; ++round) {
        {
            auto source{create(root + "/420.mov", true)};
            auto frame{first(*source)};
            if (round == 1) {
                held = frame;
                view = held->mapRead();
                expected = view->mat().at<unsigned char>(0, 0);
            }
            stop(*source);
        }
        require(view->mat().at<unsigned char>(0, 0) == expected, "Held pixels changed");
        resources("create_destroy", round, origin);
    }
    held.reset();
    require(view->mat().at<unsigned char>(0, 0) == expected, "View lost ownership");
    view.reset();
    resources("released", 100, origin);
    const auto load_start{Clock::now()};
    auto source{create(root + "/slow.mov", true)};
    int round{};
    do {
        require(source->start(), "Load restart failed");
        ++round;
        FrameRef frame{};
        std::uint64_t consumed{};
        const auto deadline{Clock::now() + 5s};
        while (true) {
            require(Clock::now() < deadline, "Load round timed out");
            if (!waitForLatestFrame(*source, frame, 20ms)) {
                if (!source->isRunning()) { break; }
                continue;
            }
            if (++consumed == 1) {
                require(frame->timestampUs() == 1700000000000000ULL, "Load restart residue");
            }
            if (consumed == 5) { resources("active", round, origin); }
            std::this_thread::sleep_for(60ms);
            if (round % 2 == 0 && consumed == 10) { break; }
        }
        const double elapsed{stop(*source)};
        const auto stats{diagnostic(*source)};
        require(round % 2 == 0 || stats.state == rk3576::VideoState::Eof, "Load failed before EOF");
        std::cout << "load round=" << round << " consumed=" << consumed << " read=" << stats.read
                  << " dropped=" << stats.dropped << " stop_ms=" << elapsed << std::endl;
        resources("stopped", round, origin);
    } while (Clock::now() - load_start < std::chrono::seconds{seconds});
    source.reset();
    resources("final", round, origin);
    std::cout << "load_complete rounds=" << round << " duration_s="
              << std::chrono::duration<double>(Clock::now() - load_start).count() << std::endl;
}
} // namespace

/** @brief Linux 实机验收：固定素材目录、资源负载秒数；不加载模型。 */
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Usage: video_board_validation FIXTURE_DIRECTORY LOAD_SECONDS");
        const int seconds{std::stoi(argv[2])};
        require(seconds >= 0, "Negative duration");
        functional(argv[1]);
        lifecycle(argv[1], seconds);
        std::cout << "BOARD_VALIDATION_PASS" << std::endl;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
