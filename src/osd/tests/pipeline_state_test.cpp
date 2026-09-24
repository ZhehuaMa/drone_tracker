#include "platform/rk3576/osd/pipeline_state.h"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char *message) { if (!condition) { throw std::runtime_error(message); } }
}
int main() {
    using namespace rk3576::osd_backend;
    try {
        PlaneState state{};
        const auto first{state.prepare(true)};
        require(first.primary == 1 && first.overlay == 1 && state.displayed().primary == 0, "prepare displayed early");
        state.submitted();
        require(!state.confirm(first.generation + 1), "stale generation accepted");
        require(state.confirm(first.generation), "matching flip rejected");
        const auto repeat{state.prepare(false)};
        require(repeat.primary == 1 && repeat.overlay == 0, "telemetry changed primary");
        state.submitted(); require(state.confirm(repeat.generation), "repeat confirm");
        const auto rejected{state.prepare(true)};
        state.discard();
        require(state.displayed().primary == 1 && state.displayed().overlay == 0, "rejection changed displayed");
        require(!state.confirm(rejected.generation), "discarded flip accepted");
        for (bool submitted : {false, true}) {
            PlaneState failed{};
            const auto prepared{failed.prepare(true)};
            if (submitted) { failed.submitted(); }
            failed.fail();
            require(failed.displayed().primary == 0 && !failed.confirm(prepared.generation), "failure advanced state");
            bool blocked{};
            try { failed.videoBack(); } catch (const std::logic_error &) { blocked = true; }
            require(blocked, "failed/in-flight buffer reused");
        }
        for (int channels : {1, 3}) {
            const auto tight{sourceLayout(1280, 720, channels, 1280 * channels)};
            const auto padded{sourceLayout(1280, 720, channels, 1296 * channels)};
            require(!tight.repack && !padded.repack && padded.stride_pixels == 1296, "compatible stride copied");
        }
        const auto odd{sourceLayout(1280, 720, 3, 4096)};
        require(odd.repack && odd.stride_pixels == 1280 && odd.channels == 3, "BGR byte stride truncated");
        const auto gray{sourceLayout(1279, 719, 1, 1280)};
        require(gray.repack && gray.expand_gray && gray.channels == 3, "odd gray geometry lost");
        bool invalid{};
        try { sourceLayout(1280, 720, 3, 3839); } catch (const std::invalid_argument &) { invalid = true; }
        require(invalid, "short row accepted");
        std::cout << "OSD buffer generation and source layout passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
