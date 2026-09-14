#include "../video_frame_source.h"
#include "../host_frame.h"
#include "video_test_support.h"
#include "platform/frame_source_ops.h"

#include <future>
#include <iostream>

namespace {
using namespace rk3576;
using namespace video_test;
using namespace std::chrono_literals;
class FakeClock final : public VideoClock {
public:
    std::int64_t system{1000000};
    TimePoint now{100s};
    std::vector<TimePoint> deadlines{};
    int waits{};
    std::int64_t systemUs() override { return system; }
    TimePoint steadyNow() override { return now; }
    bool waitUntil(TimePoint deadline, const std::atomic<bool> &stop) override {
        deadlines.push_back(deadline);
        if (deadline > now) {
            ++waits;
            now = deadline;
        }
        system += 900000000; // 模拟系统校时；单调播放时钟独立。
        return !stop.load();
    }
};
class Reader final : public VideoReader {
public:
    int count{100};
    int index{};
    int failure{-1};
    bool repeated{};
    cv::Mat buffer{3, 5, CV_8UC1, cv::Scalar{0}};
    std::vector<std::pair<std::string, std::string>> tags{{"Artist", "1700000000000000"}};
    VideoReadResult next() override {
        if (index == failure) {
            return {VideoReadStatus::ReadError, {}, "injected I/O error"};
        }
        if (index >= count) {
            return {VideoReadStatus::Eof, {}, "EOF"};
        }
        buffer.setTo(index);
        const std::int64_t pts{repeated && index == 1 ? 0 : index};
        ++index;
        return {VideoReadStatus::Frame, {buffer, pts, true}, {}};
    }
    std::pair<int, int> timeBase() const override { return {1, 1000}; }
    const std::vector<std::pair<std::string, std::string>> &metadata() const override { return tags; }
};
void finish(Rk3576VideoFrameSource &source) {
    const auto limit{std::chrono::steady_clock::now() + 2s};
    while (source.isRunning() && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(1ms);
    }
    require(!source.isRunning(), "Worker did not finish within test bound");
    source.stop();
}
VideoDependencies dependencies(const std::shared_ptr<FakeClock> &clock, int count = 100) {
    VideoDependencies injected{};
    injected.clock = clock;
    injected.open = [count](const std::string &, const std::atomic<bool> &) {
        auto reader{std::make_unique<Reader>()};
        reader->count = count;
        return reader;
    };
    injected.diagnose = [](const VideoDiagnostic &) { throw std::runtime_error("diagnostic failure"); };
    return injected;
}
void queueAndOwnership() {
    FrameSourceConfig config{};
    config.path = "controlled";
    config.use_history_timestamp = true;
    FrameRef held{};
    std::unique_ptr<FrameReadView> view{};
    std::weak_ptr<const Frame> lifetime{};
    {
        auto clock{std::make_shared<FakeClock>()};
        auto injected{dependencies(clock)};
        Rk3576VideoFrameSource source{config, injected};
        require(!source.isRunning(), "Construction started worker");
        int width{}, height{};
        require(source.getImageSize(width, height) && width == 5 && height == 3, "Pre-start dimensions");
        require(source.start(), "Start failed");
        finish(source);
        const auto stats{source.diagnostic()};
        require(stats.state == VideoState::Eof && stats.read == 100 && stats.dropped > 0
            && stats.enqueued + stats.dropped == 100, "Queue accounting or EOF");
        require(source.tryFrame(held) && held->timestampUs() == 1700000000000000, "Queue evicted oldest frame");
        view = held->mapRead();
        lifetime = held;
        require(view->mat().at<unsigned char>(0, 0) == 0, "Decode buffer reused without copy");
        FrameRef frame{};
        std::uint64_t next{1};
        while (source.tryFrame(frame)) {
            require(frame->timestampUs() == 1700000000000000 + next * 1000, "Old queued frames overwritten");
            ++next;
        }
        require(next == stats.enqueued && !frame, "Tail count/output clearing");
        frame = held;
        require(!source.waitFrame(frame, 1ms) && !frame, "Timeout retained stale output");
        for (int round{0}; round < 3; ++round) {
            require(source.start(), "EOF restart failed");
            finish(source);
            require(source.tryFrame(frame) && frame->timestampUs() == held->timestampUs(), "History restart residue");
            // 故意保留其余尾帧，下次 start 必须清除。
        }
    }
    require(view->mat().at<unsigned char>(0, 0) == 0, "View lost pixels across source destruction");
    held.reset();
    require(view->mat().at<unsigned char>(0, 0) == 0, "View lost pixels after frame release");
    require(view->mat().u->refcount == 1, "Image has unexpected owner after source/frame release");
    view.reset();
    require(lifetime.expired(), "Final owner not released");
}
void clocksAndFailures() {
    FrameSourceConfig config{};
    auto clock{std::make_shared<FakeClock>()};
    auto injected{dependencies(clock, 3)};
    Rk3576VideoFrameSource realtime{config, injected};
    require(realtime.start(), "Realtime start");
    finish(realtime);
    FrameRef frame{};
    require(realtime.tryFrame(frame) && frame->timestampUs() == 1000000, "Realtime origin");
    require(realtime.tryFrame(frame) && frame->timestampUs() == 1001000, "System jump changed timestamp");
    const auto new_base{clock->system};
    require(realtime.start(), "Realtime restart");
    finish(realtime);
    require(realtime.tryFrame(frame) && frame->timestampUs() == static_cast<std::uint64_t>(new_base), "New origin");
    require(clock->deadlines[1] - clock->deadlines[0] == 1ms, "System jump changed pacing");
    auto late{std::make_shared<FakeClock>()};
    auto late_deps{dependencies(late, 3)};
    late_deps.launch = [late](std::function<void()> work) {
        late->now += 10s;
        return std::thread{std::move(work)};
    };
    Rk3576VideoFrameSource behind{config, late_deps};
    require(behind.start(), "Late start");
    finish(behind);
    require(late->waits == 0, "Behind schedule added wait");
    int attempts{};
    injected.launch = [&attempts](std::function<void()> work) {
        if (++attempts == 1) {
            throw std::runtime_error("injected thread creation failure");
        }
        return std::thread{std::move(work)};
    };
    Rk3576VideoFrameSource retry{config, injected};
    require(!retry.start() && retry.diagnostic().state == VideoState::Failed, "Thread failure swallowed");
    require(retry.start(), "Retry after launch failure");
    finish(retry);
    int opens{};
    injected.launch = {};
    injected.open = [&opens](const std::string &, const std::atomic<bool> &) {
        auto reader{std::make_unique<Reader>()};
        reader->count = 3;
        reader->failure = ++opens == 1 ? 1 : -1;
        return reader;
    };
    Rk3576VideoFrameSource io{config, injected};
    require(io.start(), "I/O start");
    finish(io);
    require(io.diagnostic().state == VideoState::Failed
        && io.diagnostic().reason.find("I/O") != std::string::npos, "I/O confused with EOF");
    require(io.start(), "I/O retry");
    finish(io);
    require(io.diagnostic().state == VideoState::Eof, "Retry failed");
    injected.open = [](const std::string &, const std::atomic<bool> &) {
        auto reader{std::make_unique<Reader>()};
        reader->repeated = true;
        return reader;
    };
    Rk3576VideoFrameSource duplicate{config, injected};
    require(duplicate.start(), "Duplicate test start");
    finish(duplicate);
    require(duplicate.diagnostic().state == VideoState::Failed && duplicate.diagnostic().enqueued == 1
        && duplicate.diagnostic().frame_index == 2, "Invalid PTS delivered");

    int reopen_count{};
    injected.open = [&reopen_count](const std::string &, const std::atomic<bool> &) {
        auto reader{std::make_unique<Reader>()};
        reader->count = 1;
        if (++reopen_count == 2) {
            reader->buffer = cv::Mat{4, 6, CV_8UC1, cv::Scalar{0}};
        }
        return reader;
    };
    Rk3576VideoFrameSource changed{config, injected};
    require(changed.start(), "Before changed reopen");
    finish(changed);
    require(!changed.start() && changed.diagnostic().state == VideoState::Failed,
        "Reopened dimensions accepted");
    require(!changed.tryFrame(frame) && !frame, "Failed restart retained old queue");
    require(changed.start(), "Retry after preparation failure");
    finish(changed);
}
void concurrentStop(const std::string &root) {
    FrameSourceConfig config{};
    config.path = root + "/long.mov";
    config.use_history_timestamp = true;
    Rk3576VideoFrameSource source{config};
    require(source.start(), "Long video start");
    require(!source.start(), "Running start accepted");
    FrameRef first{};
    require(source.waitFrame(first, 1s), "First frame missing");
    auto consumer{std::async(std::launch::async, [&] {
        FrameRef frame{first};
        return !source.waitFrame(frame, 100ms) && !frame;
    })};
    const auto before{std::chrono::steady_clock::now()};
    source.stop();
    require(std::chrono::steady_clock::now() - before < 1s, "Stop blocked for PTS gap");
    require(consumer.get(), "Stop/consumer overlap failed");
    require(source.stop(), "Repeated stop failed");
    require(source.start() && source.waitFrame(first, 1s), "Midstream restart first frame");
    require(first->timestampUs() == 1700000000000000, "History first frame changed");
    source.stop();
    config.path = root + "/bad-artist.mov";
    rejects([&] { Rk3576VideoFrameSource invalid{config}; }, "Invalid history accepted");
    config.use_history_timestamp = false;
    { Rk3576VideoFrameSource allowed{config}; }
    config.path = root + "/420.mov";
    {
        auto fast{std::make_shared<FakeClock>()};
        VideoDependencies injected{};
        injected.clock = fast;
        Rk3576VideoFrameSource natural{config, injected};
        require(natural.start(), "Natural EOF start");
        const auto limit{std::chrono::steady_clock::now() + 2s};
        while (natural.isRunning() && std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(1ms);
        }
        require(!natural.isRunning(), "Natural EOF timeout");
        // 不显式 stop，析构必须回收仍可 join 的已结束线程。
    }
    config.path = root + "/offset.mov";
    config.use_history_timestamp = true;
    VideoDependencies offset_deps{};
    offset_deps.clock = std::make_shared<FakeClock>();
    Rk3576VideoFrameSource offset{config, offset_deps};
    require(offset.start(), "Nonzero PTS start");
    finish(offset);
    require(offset.tryFrame(first) && first->timestampUs() == 1700000000100000, "Real file PTS offset lost");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Expected fixture directory");
        queueAndOwnership();
        clocksAndFailures();
        concurrentStop(argv[1]);
        std::cout << "Video queue, restart, clock, failure and ownership tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
