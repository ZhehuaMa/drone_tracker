#include "../cpu_observer.h"

#include <atomic>
#include <iostream>
#include <thread>

namespace {
/** @brief Export real CPU-observer files from managed threads and the format-2 dispatcher. */
void exportFixture(const std::filesystem::path &output) {
    tracking_regression::CpuObserver observer{32};
    std::array<std::vector<diagnostics::Event>, diagnostics::PRODUCERS> events{};
    std::atomic<std::size_t> ready{};
    std::atomic<bool> release{};
    std::vector<std::thread> threads{};
    for (const auto producer : {diagnostics::Producer::Node, diagnostics::Producer::Yolo,
                               diagnostics::Producer::Control, diagnostics::Producer::Dispatcher}) {
        threads.emplace_back([&, producer] {
            diagnostics::current_producer = producer;
            diagnostics::positionPoint(200);
            ready.fetch_add(1);
            while (!release.load()) { std::this_thread::yield(); }
            auto &records{events[static_cast<std::size_t>(producer)]};
            const auto observe{[&](diagnostics::Type type) {
                auto event{diagnostics::event(type)};
                event.run_id = 1;
                event.round_id = 1;
                event.update_id = 1;
                event.mono_ns = diagnostics::monotonicNs();
                // Use the receiver installed by CpuObserver, as diagnostics::emit does.
                diagnostics::position_receiver(producer, event, static_cast<std::uint32_t>(type));
                records.push_back(event);
            }};
            if (producer == diagnostics::Producer::Node) {
                observe(diagnostics::Type::UpdateStart);
                observe(diagnostics::Type::UpdateEnd);
            } else if (producer == diagnostics::Producer::Yolo) {
                observe(diagnostics::Type::DetectStart);
                observe(diagnostics::Type::DetectEnd);
            } else if (producer == diagnostics::Producer::Dispatcher) {
                observe(diagnostics::Type::FrameSkip);
            }
        });
    }
    while (ready.load() != threads.size()) { std::this_thread::yield(); }
    const auto origin{diagnostics::monotonicNs()};
    release.store(true);
    for (auto &thread : threads) { thread.join(); }
    observer.exportRecords(output);
    std::ofstream raw{output / "events.raw", std::ios::binary};
    for (const auto &records : events) {
        for (const auto &event : records) {
            raw.write(reinterpret_cast<const char *>(&event), sizeof(event));
        }
    }
    std::ofstream clock{output / "clock.json"};
    clock << "{\"m0_ns\":" << origin << ",\"end_ns\":" << diagnostics::monotonicNs() << "}\n";
    if (!raw || !clock) { throw std::runtime_error{"Cannot export CPU fixture context"}; }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) { throw std::invalid_argument{"Expected an existing output directory"}; }
        exportFixture(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
