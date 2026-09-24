#include "osd/timing.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) { throw std::runtime_error(message); }
}
}
int main() {
    try {
        osd::TimingHistogram h{};
        require(!h.percentile(95), "empty percentile");
        for (std::uint64_t n : {0, 1, 2, 3, 4, 5, 8}) { h.add(n); }
        require(h.count == 7 && h.total_us == 23 && h.max_us == 8, "exact aggregates");
        require(h.buckets[0] == 2 && h.buckets[1] == 1 && h.buckets[2] == 2 && h.buckets[3] == 2,
                "power of two boundaries");
        require(h.percentile(95) == 8 && h.percentile(99) == 8, "percentile rank");
        osd::TimingHistogram overflow{};
        overflow.add(67108864);
        require(overflow.percentile(99) == 67108864, "last finite bucket");
        overflow.add(67108865);
        require(!overflow.percentile(95) && overflow.max_us == 67108865, "overflow lower bound");
        osd::TimingSample sample{};
        static_assert(osd::TimingClock::is_steady, "timing must resist wall-clock corrections");
        osd::TimingClock::time_point now{};
        std::chrono::system_clock::time_point wall{};
        const std::function<osd::TimingClock::time_point()> clock{[&] { return now; }};
        {
            osd::TimingSpan span{&sample, osd::TimingStage::SourceCopy, clock};
            wall += std::chrono::hours{24}; // 模拟系统校时，不改变独立的单调时钟。
            now += std::chrono::microseconds{20000};
            span.next(osd::TimingStage::ScaleConvert);
            now += std::chrono::microseconds{3000};
        }
        require(sample.stages[0] == 20000 && sample.stages[1] == 3000 && !sample.stages[2],
                "injected monotonic time/missing stage");
        require(wall.time_since_epoch() == std::chrono::hours{24}, "wall jump fixture");
        int calls{};
        const std::function<osd::TimingClock::time_point()> disabled_clock{[&] { ++calls; return now; }};
        { osd::TimingSpan span{nullptr, osd::TimingStage::Layout, disabled_clock}; span.next(osd::TimingStage::Compose); }
        require(calls == 0, "disabled timing reads clock");
        try {
            osd::TimingSpan span{&sample, osd::TimingStage::Layout, clock};
            now += std::chrono::microseconds{17};
            throw std::runtime_error("injected");
        } catch (const std::runtime_error &) {}
        require(sample.stages[2] == 17 && !sample.stages[4], "exception stage retained");
        osd::TimingWindow window{};
        window.add(sample, true, osd::TimingResult::Failure, true);
        require(window.groups[2].count == 1 && window.groups[0].count == 0, "failure grouping");
        const auto log{osd::formatTiming(window)};
        require(log.find("p95_approx=32768") != std::string::npos, "quantile precision label");
        require(log.find("samples=none") != std::string::npos, "empty groups");
        std::cout << "OSD bounded timing statistics passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
