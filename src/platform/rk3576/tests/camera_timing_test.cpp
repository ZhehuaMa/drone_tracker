#include "camera_test_support.h"

#include <limits>
#include <iostream>

int main() {
    using namespace camera_test;
    using namespace rk3576;
    try {
        const auto pair{cameraClockPair(100, 1700000000000110, 120)};
        require(pair.offset_us == 1700000000000000 && pair.span_us == 20, "Midpoint offset");
        CameraTiming driver{pair};
        require(driver.map(10, 0, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 500) == 1700000010000000,
                "Driver timestamp mapping");
        require(driver.map(10, 1, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 0) == 1700000010000001,
                "Fixed offset unexpectedly resampled");
        rejects([&] { driver.map(10, 1, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 600); }, "repeated");
        rejects([&] { driver.map(9, 1, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 600); }, "backwards");
        rejects([&] { driver.map(10, 2, 0, 600); }, "strategy changed");
        CameraTiming fallback{pair};
        require(fallback.map(999, 0, 0, 500) == 1700000000000500
                && fallback.strategy() == "dequeue_fallback", "Unknown clock guessed");
        require(fallback.map(0, 0, V4L2_BUF_FLAG_TIMESTAMP_COPY, 501) == 1700000000000501,
                "COPY fallback timestamp");
        rejects([&] { fallback.map(0, 0, 0, 501); }, "repeated");
        rejects([&] { fallback.map(0, 0, 0, 499); }, "backwards");
        rejects([&] { fallback.map(0, 0, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 600); }, "strategy changed");
        rejects([&] { cameraTimevalUs(-1, 0); }, "timeval");
        rejects([&] { cameraTimevalUs(1, -1); }, "timeval");
        rejects([&] { cameraTimevalUs(1, 1000000); }, "timeval");
        rejects([&] { cameraTimevalUs(std::numeric_limits<std::int64_t>::max(), 0); }, "overflow");
        rejects([&] { cameraClockPair(200, 100, 100); }, "clock pair");
        CameraTiming overflow{{std::numeric_limits<std::int64_t>::max(), 0}};
        rejects([&] { overflow.map(1, 0, 0, 1); }, "overflow");
        CameraTiming negative{{-1000, 0}};
        rejects([&] { negative.map(0, 0, 0, 1); }, "nonpositive");
        const auto flags{V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC};
        CameraWarmup warmup{pair, 0};
        const std::int64_t times[]{100, 100, 117, 117, 134, 151, 168, 185, 202, 219, 236};
        for (unsigned index{}; index < 11; ++index) {
            require(warmup.observe(0, times[index], flags, index, index) == (index == 10),
                    "Measured startup repeat pattern");
        }
        require(warmup.examined() == 11 && warmup.repeats() == 2 && warmup.streak() == 8,
                "Warmup counters");
        CameraTiming output{pair};
        require(output.map(0, 236, flags, 100) == pair.offset_us + 236, "Warmup altered timestamp");
        rejects([&] { output.map(0, 236, flags, 101); }, "repeated");
        CameraWarmup repeated{pair, 0};
        for (unsigned index{}; index < 120; ++index) {
            require(!repeated.observe(0, 100, flags, index, index), "Repeated warmup succeeded");
        }
        require(repeated.exhausted() && repeated.streak() == 1 && repeated.repeats() == 119,
                "Repeated sample limit");
        rejects([&] { repeated.observe(0, 101, flags, 121, 121); }, "finished");
        CameraWarmup last{pair, 0};
        for (unsigned index{}; index < 120; ++index) {
            const auto time{index < 113 ? 100U : 100U + index - 112};
            require(last.observe(0, time, flags, index, index) == (index == 119), "Last sample boundary");
        }
        CameraWarmup expired{pair, 0};
        for (unsigned index{}; index < 7; ++index) {
            expired.observe(0, 100 + index, flags, index * 200000, index * 200000);
        }
        rejects([&] { expired.observe(0, 107, flags, 2000000, 2000000); }, "time limit");
        CameraWarmup idle{pair, 10};
        require(idle.remainingUs(2000009) == 1, "Absolute warmup budget");
        rejects([&] { idle.checkDeadline(2000010); }, "no successful dequeue");
        CameraWarmup changed{pair, 0};
        changed.observe(0, 100, flags, 1, 1);
        rejects([&] { changed.observe(0, 101, 0, 2, 2); }, "strategy changed");
        CameraWarmup backwards{pair, 0};
        backwards.observe(0, 100, flags, 1, 1);
        rejects([&] { backwards.observe(0, 99, flags, 2, 2); }, "backwards");
        CameraWarmup unknown{pair, 0};
        for (unsigned index{}; index < 8; ++index) {
            require(unknown.observe(0, 900 - index, 0, index, index) == (index == 7),
                    "Fallback compared meaningless raw time");
        }
        CameraWarmup invalid{pair, 0};
        rejects([&] { invalid.observe(-1, 0, flags, 1, 1); }, "timeval");
        CameraWarmup overflow_warmup{{std::numeric_limits<std::int64_t>::max(), 0}, 0};
        rejects([&] { overflow_warmup.observe(1, 0, flags, 1, 1); }, "overflow");
        CameraWarmup nonpositive{{-1000, 0}, 0};
        rejects([&] { nonpositive.observe(0, 1, flags, 1, 1); }, "nonpositive");
        std::cout << "camera timing tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
