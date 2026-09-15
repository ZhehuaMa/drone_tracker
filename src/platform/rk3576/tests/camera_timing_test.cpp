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
        std::cout << "camera timing tests passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
