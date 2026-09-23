#include "osd/presenter.h"

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
    explicit FakeBackend(BackendState &state) : state_{state} {}
    void initialize() override {}
    void prepareVideo(const Frame &frame) override {
        auto view{frame.mapRead()};
        prepared_ = view->mat().at<unsigned char>(0, 0);
        std::unique_lock<std::mutex> lock{state_.mutex};
        ++state_.preparations;
        state_.preparing = true;
        state_.changed.notify_all();
        state_.changed.wait(lock, [&] { return !state_.block_prepare || state_.interrupted; });
    }
    bool present(const osd::PresentationState &state, const std::function<bool()> &authorize) override {
        if (!authorize()) {
            return false;
        }
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
        state_.pixels.push_back(current_);
        state_.outputs.push_back(state);
        state_.changed.notify_all();
        return true;
    }
    void discardPrepared() noexcept override { prepared_ = 0; }
    void interrupt() noexcept override {
        std::lock_guard<std::mutex> lock{state_.mutex};
        state_.interrupted = true;
        state_.changed.notify_all();
    }
    void shutdown() noexcept override {}
private:
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
void pairingAndOwnership() {
    Resources resources{};
    BackendState state{};
    state.block_prepare = true;
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state)};
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
}
void stopBeforeHandoff() {
    Resources resources{};
    BackendState state{};
    state.block_prepare = true;
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state)};
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
    require(resources.frames == 0 && resources.views == 0, "stop resource leak");
}
void stopInFlightAndRestart() {
    Resources resources{};
    BackendState state{};
    state.block_present = true;
    std::atomic<int> clock_ms{0};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state), 50ms,
        [&] { return osd::SteadyClock::time_point{std::chrono::milliseconds{clock_ms.load()}}; }};
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
    require(resources.frames == 0, "restart leaked frames");
}
void failureAndExit() {
    Resources resources{};
    BackendState state{};
    osd::OsdPresenter presenter{std::make_unique<FakeBackend>(state)};
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
    require(presenter.failure() == "injected display failure", "failure not recorded");
    require(!presenter.submit(packet(next, 2, resources)), "failed backend accepts packets");
    presenter.shutdown();
    require(resources.frames == 0 && resources.views == 0, "exit leak");
}
} // namespace
int main() {
    try {
        pairingAndOwnership();
        stopBeforeHandoff();
        stopInFlightAndRestart();
        failureAndExit();
        std::cout << "OSD pairing, lifetime, stop and restart tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
