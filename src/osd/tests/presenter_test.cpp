#include "osd/presenter.h"
#include "osd/layout.h"
#include "platform/rk3576/osd/pipeline_state.h"

#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
struct Resources {
    std::atomic<int> frames{0}, views{0};
};
class TestView final : public FrameReadView {
public:
    TestView(const cv::Mat &pixels, Resources &resources) : pixels_{pixels}, resources_{resources} {
        ++resources_.views;
    }
    ~TestView() override { --resources_.views; }
    int width() const noexcept override { return pixels_.cols; }
    int height() const noexcept override { return pixels_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::size_t rowStrideBytes() const noexcept override { return pixels_.step; }
    const cv::Mat &mat() const noexcept override { return pixels_; }
private:
    cv::Mat pixels_;
    Resources &resources_;
};
class TestFrame final : public Frame {
public:
    TestFrame(int value, Resources &resources)
        : pixels_{3, 7, CV_8UC1, cv::Scalar{static_cast<double>(value)}}, resources_{resources} {
        ++resources_.frames;
    }
    ~TestFrame() override { --resources_.frames; }
    int width() const noexcept override { return pixels_.cols; }
    int height() const noexcept override { return pixels_.rows; }
    FramePixelFormat pixelFormat() const noexcept override { return FramePixelFormat::Gray8; }
    std::uint64_t timestampUs() const noexcept override { return 123; }
    std::unique_ptr<FrameReadView> mapRead() const override {
        return std::make_unique<TestView>(pixels_, resources_);
    }
private:
    cv::Mat pixels_;
    Resources &resources_;
};
struct BackendState {
    std::mutex mutex{};
    std::condition_variable changed{};
    bool block_prepare{}, preparing{}, block_present{}, presenting{}, interrupted{}, fail{};
    int preparations{};
    std::vector<osd::PresentationState> outputs{};
    std::vector<int> pixels{};
};
class FakeBackend final : public osd::DisplayBackend {
public:
    explicit FakeBackend(BackendState &state, bool cached) : state_{state}, cached_{cached} {}
    void initialize() override {}
    void prepareVideo(const Frame &frame) override {
        osd::TimingSpan timing{timing_sample_, cached_ ? osd::TimingStage::SourcePrepare
            : osd::TimingStage::SourceCopy, timing_now_};
        auto view{frame.mapRead()};
        prepared_ = view->mat().at<unsigned char>(0, 0);
        std::unique_lock<std::mutex> lock{state_.mutex};
        ++state_.preparations;
        state_.preparing = true;
        state_.changed.notify_all();
        state_.changed.wait(lock, [&] { return !state_.block_prepare || state_.interrupted; });
    }
    bool present(const osd::PresentationState &state, const std::function<bool()> &authorize) override {
        osd::TimingSpan timing{timing_sample_, osd::TimingStage::Layout, timing_now_};
        const auto pair{planes_.prepare(state.new_video)};
        if (cached_) { layout_.draw(state, osd::fitVideo(7, 3, 720, 480), 720, 480, {}); }
        timing.next(cached_ ? osd::TimingStage::OverlayUpload : osd::TimingStage::Compose);
        if (!authorize()) { planes_.discard(); return false; }
        std::unique_lock<std::mutex> lock{state_.mutex};
        if (state.new_video) {
            state_.presenting = true;
            state_.changed.notify_all();
            state_.changed.wait(lock, [&] { return !state_.block_present || state_.interrupted; });
        }
        if (state_.fail) {
            throw std::runtime_error("injected display failure");
        }
        if (state.new_video) {
            current_ = prepared_;
        }
        const auto previous{planes_.displayed()};
        planes_.submitted();
        require(planes_.confirm(pair.generation), "test flip not confirmed");
        if (!state.new_video) { require(planes_.displayed().primary == previous.primary, "frozen primary changed"); }
        state_.pixels.push_back(current_);
        state_.outputs.push_back(state);
        state_.changed.notify_all();
        return true;
    }
    void discardPrepared() noexcept override { prepared_ = 0; planes_.discard(); }
    void interrupt() noexcept override {
        std::lock_guard<std::mutex> lock{state_.mutex};
        state_.interrupted = true;
        state_.changed.notify_all();
    }
    void shutdown() noexcept override {}
private:
    bool cached_{};
    osd::CachedOsdLayout layout_{};
    rk3576::osd_backend::PlaneState planes_{};
    BackendState &state_;
    int prepared_{}, current_{};
};
template <typename Predicate> void waitFor(BackendState &state, Predicate predicate) {
    std::unique_lock<std::mutex> lock{state.mutex};
    require(state.changed.wait_for(lock, 2s, predicate), "backend timeout");
}
osd::PresentationPacket packet(std::uint64_t round, int sequence, Resources &resources) {
    return {std::make_shared<TestFrame>(sequence, resources), round, static_cast<std::uint64_t>(sequence),
            123, 2, {{static_cast<double>(sequence), 0, 1, 1, "target"}}};
}
struct TimingCapture {
    std::mutex mutex{};
    std::vector<osd::TimingWindow> windows{};
    void record(const osd::TimingWindow &window) {
        std::lock_guard<std::mutex> lock{mutex};
        windows.push_back(window);
    }
    std::uint64_t count(std::size_t group, osd::TimingStage stage) {
        std::lock_guard<std::mutex> lock{mutex};
        std::uint64_t result{};
        for (const auto &window : windows) { result += window.groups[group].stages[static_cast<std::size_t>(stage)].count; }
        return result;
    }
};
struct DiagnosticCapture {
    std::mutex mutex{};
    std::condition_variable changed{};
    std::vector<std::string> lines{};
    void record(const std::string &line) {
        std::lock_guard<std::mutex> lock{mutex};
        lines.push_back(line);
        changed.notify_all();
    }
    bool contains(const std::string &fragment) {
        std::lock_guard<std::mutex> lock{mutex};
        for (const auto &line : lines) {
            if (line.find(fragment) != std::string::npos) { return true; }
        }
        return false;
    }
    bool waitFor(const std::string &fragment) {
        std::unique_lock<std::mutex> lock{mutex};
        return changed.wait_for(lock, 2s, [&] {
            for (const auto &line : lines) {
                if (line.find(fragment) != std::string::npos) { return true; }
            }
            return false;
        });
    }
    std::size_t countDisplay() {
        std::lock_guard<std::mutex> lock{mutex};
        std::size_t count{};
        for (const auto &line : lines) {
            if (line.find("OSD display") == 0) { ++count; }
        }
        return count;
    }
};
void pairingAndOwnership(bool cached) {
    Resources resources{};
    BackendState state{};
    state.block_prepare = true;
    TimingCapture timing{};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 1000ms, osd::SteadyClock::now,
        [&](const osd::TimingWindow &window) { timing.record(window); }};
    presenter.start();
    const auto round{presenter.beginRound()};
    require(presenter.beginRound() == round, "duplicate ON changed round");
    auto first{packet(round, 1, resources)};
    const FrameRef detector{first.frame};
    require(presenter.submit(std::move(first)), "A rejected");
    waitFor(state, [&] { return state.preparing; });
    require(resources.views == 1, "view not retained while reading");
    require(presenter.submit(packet(round, 2, resources)), "B rejected");
    require(presenter.submit(packet(round, 3, resources)), "C rejected");
    require(resources.frames == 2, "replaced source frame retained");
    require(presenter.counters().replaced == 1, "replacement counter");
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        state.block_prepare = false;
        state.changed.notify_all();
    }
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().sequence == 3; });
    require(resources.frames == 1 && resources.views == 0, "display retained source/view");
    require(detector->mapRead()->mat().at<unsigned char>(0, 0) == 1, "source mutated");
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        int new_frames{0};
        for (std::size_t i{0}; i < state.outputs.size(); ++i) {
            const auto &output{state.outputs[i]};
            if (output.new_video) {
                ++new_frames;
                require(output.sequence != 2, "B unexpectedly displayed");
                require(output.sequence == static_cast<std::uint64_t>(state.pixels[i]), "pixel pair mismatch");
                require(output.boxes[0].x == state.pixels[i], "box pair mismatch");
            }
        }
        require(new_frames == 2, "new frame count");
    }
    presenter.shutdown();
    presenter.reportTiming();
    require(timing.count(0, osd::TimingStage::PendingWait) == 2, "replaced packet was sampled");
    require(timing.count(0, osd::TimingStage::NewFrameInterval) == 1, "new interval missing");
}
void stopBeforeHandoff(bool cached) {
    Resources resources{};
    BackendState state{};
    state.block_prepare = true;
    TimingCapture timing{};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 1000ms, osd::SteadyClock::now,
        [&](const osd::TimingWindow &window) { timing.record(window); }};
    presenter.start();
    const auto round{presenter.beginRound()};
    presenter.submit(packet(round, 1, resources));
    waitFor(state, [&] { return state.preparing; });
    presenter.endRound(round, osd::DisplayStatus::Stopped);
    require(!presenter.submit(packet(round, 2, resources)), "late packet accepted");
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        state.block_prepare = false;
        state.changed.notify_all();
    }
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().status == osd::DisplayStatus::Stopped; });
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        require(!state.outputs.back().has_video, "uncommitted base used for freeze");
        require(state.outputs.back().boxes.empty(), "late box revived");
    }
    presenter.shutdown();
    presenter.reportTiming();
    require(timing.count(1, osd::TimingStage::RenderTotal) == 1, "rejected render sample missing");
    require(timing.count(0, osd::TimingStage::SubmitToDisplay) == 0, "rejected display sampled as success");
    require(resources.frames == 0 && resources.views == 0, "stop resource leak");
}
void stopInFlightAndRestart(bool cached) {
    Resources resources{};
    BackendState state{};
    state.block_present = true;
    TimingCapture timing{};
    std::atomic<int> clock_ms{0};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 50ms,
        [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{clock_ms.load()}}; },
        [&](const osd::TimingWindow &window) { timing.record(window); }};
    presenter.start();
    const auto round{presenter.beginRound()};
    presenter.submit(packet(round, 1, resources));
    waitFor(state, [&] { return state.presenting; });
    require(resources.frames == 0, "hardware holds source frame");
    presenter.submit(packet(round, 2, resources));
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        require(state.preparations == 1, "buffer reused before hardware completion");
    }
    const auto stopped{osd::SteadyClock::now()};
    presenter.endRound(round, osd::DisplayStatus::Stopped);
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        state.block_present = false;
        state.changed.notify_all();
    }
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().status == osd::DisplayStatus::Stopped; });
    require(osd::SteadyClock::now() - stopped < 500ms, "freeze exceeded deadline");
    const auto next{presenter.beginRound()};
    require(next > round, "round not advanced");
    require(!presenter.submit(packet(round, 9, resources)), "old round accepted");
    presenter.endRound(round, osd::DisplayStatus::Eof);
    waitFor(state, [&] { return state.outputs.back().status == osd::DisplayStatus::WaitingFirst; });
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        require(state.pixels.back() == 1 && state.outputs.back().boxes.empty(), "freeze base or boxes wrong");
        require(state.outputs.back().frame_time_us == 123, "frame time changed");
    }
    presenter.submit(packet(next, 2, resources));
    waitFor(state, [&] { return state.outputs.back().sequence == 2; });
    clock_ms = 100;
    waitFor(state, [&] { return state.outputs.back().status == osd::DisplayStatus::Stale; });
    presenter.endRound(next, osd::DisplayStatus::Eof);
    osd::TelemetrySnapshot telemetry{};
    telemetry.warning = "new warning";
    presenter.updateTelemetry(telemetry);
    waitFor(state, [&] { return state.outputs.back().status == osd::DisplayStatus::Eof
        && state.outputs.back().telemetry.warning == "new warning"; });
    presenter.shutdown();
    presenter.reportTiming();
    require(timing.count(0, osd::TimingStage::NewFrameInterval) == 0, "interval crossed stop/new round");
    require(resources.frames == 0, "restart leaked frames");
}
void timingIntervalsAndStale(bool cached) {
    Resources resources{};
    BackendState state{};
    TimingCapture timing{};
    std::atomic<int> clock_ms{0};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 1000ms,
        [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{clock_ms.load()}}; },
        [&](const osd::TimingWindow &window) { timing.record(window); }};
    presenter.start();
    const auto round{presenter.beginRound()};
    const auto send{[&](int sequence, int time) {
        clock_ms = time;
        require(presenter.submit(packet(round, sequence, resources)), "timed packet rejected");
        const auto deadline{osd::SteadyClock::now() + 2s};
        while (presenter.counters().displayed < static_cast<std::uint64_t>(sequence)
               && osd::SteadyClock::now() < deadline) { std::this_thread::yield(); }
        require(presenter.counters().displayed == static_cast<std::uint64_t>(sequence), "timed display timeout");
    }};
    send(1, 0);
    send(2, 500);
    clock_ms = 2000;
    waitFor(state, [&] { return state.outputs.back().status == osd::DisplayStatus::Stale; });
    send(3, 2100);
    presenter.endRound(round, osd::DisplayStatus::Eof);
    presenter.shutdown();
    presenter.reportTiming();
    require(timing.count(0, osd::TimingStage::NewFrameInterval) == 1, "stale did not break interval");
    long double interval_sum{};
    std::uint64_t submitted{}, displayed{};
    for (const auto &window : timing.windows) {
        interval_sum += window.groups[0].stages[static_cast<std::size_t>(osd::TimingStage::NewFrameInterval)].total_us;
        submitted += window.submitted;
        displayed += window.displayed;
    }
    require(interval_sum == 500000, "interval did not use injected monotonic clock");
    require(submitted == 3 && displayed == 3, "snapshot counter deltas lost or duplicated");
}
void failureAndExit(bool cached) {
    Resources resources{};
    BackendState state{};
    TimingCapture timing{};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 1000ms, osd::SteadyClock::now,
        [&](const osd::TimingWindow &window) { timing.record(window); }};
    presenter.start();
    const auto round{presenter.beginRound()};
    presenter.endRound(round, osd::DisplayStatus::InputFailure);
    waitFor(state, [&] { return !state.outputs.empty()
        && state.outputs.back().status == osd::DisplayStatus::InputFailure; });
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        state.block_present = true;
    }
    const auto next{presenter.beginRound()};
    presenter.submit(packet(next, 1, resources));
    waitFor(state, [&] { return state.presenting; });
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        state.fail = true;
    }
    presenter.shutdown();
    presenter.reportTiming();
    require(timing.count(2, osd::TimingStage::RenderTotal) == 1, "failure sample missing");
    require(presenter.failure() == "injected display failure", "failure not recorded");
    require(!presenter.submit(packet(next, 2, resources)), "failed backend accepts packets");
    presenter.shutdown();
    require(resources.frames == 0 && resources.views == 0, "exit leak");
}
void triggerLifetime(bool cached) {
    BackendState state{};
    std::atomic<int> time_ms{0};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 1000ms,
        [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{time_ms.load()}}; }};
    presenter.start();
    const auto first{presenter.beginRound()};
    presenter.showTrigger(first, true);
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().trigger == "TRIG ON"; });
    time_ms = 1900;
    require(presenter.beginRound() == first, "duplicate ON changed trigger round");
    presenter.showTrigger(first, true);
    time_ms = 2100;
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().trigger == "TRIG ON"; });
    time_ms = 3900;
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().trigger.empty(); });
    presenter.endRound(first, osd::DisplayStatus::Stopped);
    presenter.showTrigger(first, false);
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().trigger == "TRIG OFF"; });
    const auto second{presenter.beginRound()};
    require(second > first, "new trigger round not advanced");
    presenter.showTrigger(first, true);
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().round == second; });
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        require(state.outputs.back().trigger.empty(), "old-round trigger leaked into new round");
    }
    presenter.shutdown();
}
void missionLifetime(bool cached) {
    BackendState state{};
    DiagnosticCapture diagnostics{};
    std::atomic<int> time_ms{0};
    Resources resources{};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state, cached), 1000ms,
        [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{time_ms.load()}}; }, {}, {},
        [&](const std::string &line) { diagnostics.record(line); }};
    presenter.start();
    const auto round{presenter.beginRound()};
    auto first{packet(round, 1, resources)};
    first.boxes.front().label.clear();
    require(presenter.submit(std::move(first)), "mission initial packet rejected");
    waitFor(state, [&] { return !state.outputs.empty() && state.outputs.back().new_video; });
    require(diagnostics.waitFor("hud=\"TRK TRACKING\" target_label=\"TRK TRACKING\""),
        "initial displayed labels missing from diagnostics");
    presenter.showMission();
    waitFor(state, [&] { return state.outputs.back().mission_active; });
    require(diagnostics.contains("event=mission round="), "mission receipt missing from diagnostics");
    require(diagnostics.waitFor("hud=\"ON MISSION\" target_label=\"TRK TRACKING\""),
        "mission display missing from diagnostics");
    const std::size_t display_logs_before_following{diagnostics.countDisplay()};
    time_ms = 3000;
    auto following{packet(round, 2, resources)};
    following.boxes.front().label.clear();
    require(presenter.submit(std::move(following)), "mission following packet rejected");
    waitFor(state, [&] { return state.outputs.back().new_video && state.outputs.back().sequence == 2; });
    for (int attempt{0}; attempt < 200 && presenter.counters().displayed < 2; ++attempt) {
        std::this_thread::sleep_for(1ms);
    }
    require(presenter.counters().displayed >= 2, "following frame not committed");
    require(diagnostics.countDisplay() == display_logs_before_following,
        "unchanged video frame produced diagnostic log");
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        require(state.outputs.back().mission_active, "mission expired without tracker state publication");
        require(state.outputs.back().tracker_state == 2, "mission changed paired tracker state");
    }
    presenter.trackerStatePublished();
    waitFor(state, [&] { return !state.outputs.back().mission_active; });
    require(diagnostics.contains("event=mission_cleared round="), "mission clear missing from diagnostics");
    presenter.showTrigger(round, true);
    waitFor(state, [&] { return state.outputs.back().trigger == "TRIG ON"; });
    {
        std::lock_guard<std::mutex> lock{state.mutex};
        require(!state.outputs.back().mission_active, "ON started mission overlay");
    }
    presenter.endRound(round, osd::DisplayStatus::Stopped);
    waitFor(state, [&] { return state.outputs.back().status == osd::DisplayStatus::Stopped; });
    require(diagnostics.contains("event=end_round round="), "stop event missing from diagnostics");
    require(diagnostics.waitFor("status=Stopped hud=\"TRK TRACKING\" target_label=\"\" boxes=0 has_video=1 new_video=0"),
        "frozen display missing from diagnostics");
    presenter.shutdown();
    require(resources.frames == 0 && resources.views == 0, "mission test leaked frames");
}
} // namespace
int main() {
    try {
        for (bool cached : {false, true}) {
            pairingAndOwnership(cached);
            stopBeforeHandoff(cached);
            stopInFlightAndRestart(cached);
            timingIntervalsAndStale(cached);
            failureAndExit(cached);
            triggerLifetime(cached);
            missionLifetime(cached);
        }
        std::cout << "OSD pairing, lifetime, stop and restart tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
