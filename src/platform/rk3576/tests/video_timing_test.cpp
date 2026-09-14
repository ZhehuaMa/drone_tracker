#include "../video_timing.h"
#include "video_test_support.h"

#include <future>
#include <iostream>
#include <limits>
#include <thread>

int main() {
    using namespace rk3576;
    using namespace video_test;
    using namespace std::chrono_literals;
    try {
        const auto maximum{std::numeric_limits<std::int64_t>::max()};
        require(historyBase({{"aRtIsT", "1700000000000000"}}) == 1700000000000000, "Artist case");
        require(historyBase({{"artist", std::to_string(maximum)}}) == maximum, "Artist int64 edge");
        for (const auto &value : {"", "0", "-1", "+1", " 1", "1 ", "1x", "9223372036854775808"}) {
            rejects([&] { historyBase({{"Artist", value}}); }, "Invalid Artist accepted");
        }
        rejects([] { historyBase({}); }, "Missing Artist accepted");
        rejects([] { historyBase({{"Artist", "1"}, {"ARTIST", "2"}}); }, "Ambiguous Artist accepted");
        require(videoPtsUs(1, true, {1001, 30000}, -1) == 33367, "Noninteger time base");
        require(videoPtsUs(1, true, {1, 2000000}, -1) == 1, "Half-microsecond rounding");
        require(videoPtsUs(maximum, true, {1, 1000000}, -1) == maximum, "PTS int64 edge");
        rejects([&] { videoPtsUs(maximum, true, {1, 1}, -1); }, "PTS overflow");
        rejects([] { videoPtsUs(1, false, {1, 1}, -1); }, "Missing PTS");
        rejects([] { videoPtsUs(-1, true, {1, 1}, -1); }, "Negative PTS");
        rejects([] { videoPtsUs(1, true, {0, 1}, -1); }, "Invalid base");
        rejects([] { videoPtsUs(1, true, {1, 3000000}, 0); }, "Rounded repeat");
        rejects([] { videoPtsUs(1, true, {1, 1}, 1000000); }, "Repeated PTS");
        require(videoTimestamp(1700000000000000, 100000) == 1700000000100000, "Offset dropped");
        require(videoTimestamp(1, maximum - 1) == maximum, "Timestamp boundary");
        rejects([&] { videoTimestamp(1, maximum); }, "Timestamp overflow");
        rejects([] { videoTimestamp(0, 0); }, "Zero frame time");
        const VideoClock::TimePoint origin{10s};
        require(videoDeadline(origin, 100000) == origin + 100ms, "Deadline offset");
        rejects([&] { videoDeadline(origin, maximum); }, "Deadline overflow");
        rejects([] { videoDeadline(VideoClock::TimePoint::max(), 1); }, "Origin overflow");
        VideoClock clock{};
        std::atomic<bool> stop{false};
        const auto before{clock.steadyNow()};
        require(clock.waitUntil(before - 10s, stop), "Late frame stopped");
        require(clock.steadyNow() - before < 100ms, "Late frame waited");
        auto waiting{std::async(std::launch::async, [&] { return clock.waitUntil(clock.steadyNow() + 10s, stop); })};
        std::this_thread::sleep_for(20ms);
        stop.store(true);
        clock.wake();
        require(waiting.wait_for(1s) == std::future_status::ready && !waiting.get(), "Long wait not interrupted");
        std::cout << "Video timing boundary tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
