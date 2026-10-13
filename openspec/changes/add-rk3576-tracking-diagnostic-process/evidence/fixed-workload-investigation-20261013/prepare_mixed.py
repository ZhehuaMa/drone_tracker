from pathlib import Path
p=Path(__file__).parent/'source-mixed/src/fixed_workload_probe.cpp';s=p.read_text()
s=s.replace('#include <iomanip>','#include <iomanip>\n#include <random>\n#include <algorithm>')
s=s.replace('AuditNano(std::unique_ptr<model::NanoTrackBackend> backend, std::ofstream &out) : backend_{std::move(backend)}, out_{out} {}','AuditNano(std::unique_ptr<model::NanoTrackBackend> backend, std::ofstream &out, bool enabled = true) : backend_{std::move(backend)}, out_{out}, enabled_{enabled} {}\n    std::uint64_t templates{}, searches{};')
s=s.replace('auto value{backend_->setTemplate(input)};', '++templates; auto value{backend_->setTemplate(input)};').replace('auto value{backend_->search(input)};', '++searches; auto value{backend_->search(input)};')
s=s.replace('const auto prepared{std::dynamic_pointer_cast<const rk3576::FloatInput>(input)};', 'if (!enabled_) { return; }\n        const auto prepared{std::dynamic_pointer_cast<const rk3576::FloatInput>(input)};')
s=s.replace('std::unique_ptr<model::NanoTrackBackend> backend_; std::ofstream &out_;', 'std::unique_ptr<model::NanoTrackBackend> backend_; std::ofstream &out_; bool enabled_{};')
s=s.replace('const int cycles{std::atoi(argv[5])};', 'const bool mixed{std::string{argv[1]} == "mixed"};\n        const unsigned seed{std::getenv("DRONE_MIXED_SEED") ? static_cast<unsigned>(std::strtoul(std::getenv("DRONE_MIXED_SEED"), nullptr, 10)) : 20261013U};\n        const int cycles{std::atoi(argv[5])};\n        if (mixed && (observed || audit || cycles % 2)) { throw std::runtime_error("Mixed mode requires unobserved even complete blocks"); }')
s=s.replace('if (audit) { components.value.nano = std::make_unique<AuditNano>(std::move(components.value.nano), audit_file); }', 'AuditNano *counted{};\n        if (audit || mixed) { auto counter{std::make_unique<AuditNano>(std::move(components.value.nano), audit_file, audit)}; counted = counter.get(); components.value.nano = std::move(counter); }')
s=s.replace('std::string{argv[1]} == "basic" ? diagnostics::Mode::Basic', '(std::string{argv[1]} == "basic" || mixed) ? diagnostics::Mode::Basic')
s=s.replace('if (!client.prepare(mode, argv[2]))', 'if (!client.prepare(mode, argv[2]))')
s=s.replace('diagnostics::measurement = observed ? measured.get() : nullptr;', '''auto *const basic_recorder{diagnostics::active.load()};
        std::array<int, MAX_CALLS / 40> assignment{};
        std::mt19937 generator{seed};
        if (mixed) {
            for (int index{}; index < cycles; index += 2) { assignment[index] = generator() & 1U; assignment[index + 1] = 1 - assignment[index]; }
        } else { assignment.fill(mode == diagnostics::Mode::Basic ? 1 : 0); }
        std::ofstream blocks{std::string{argv[4]} + "/blocks.csv"};
        blocks << "block,pair,mode,templates,searches,drain_wait_ns,drain_complete,behavior_same\\n";
        metadata << "mixed=" << mixed << "\\nseed=" << seed << "\\nboundary_minimum_wait_ns=100000000\\nboundary_maximum_wait_ns=250000000\\n";
        diagnostics::measurement = observed ? measured.get() : nullptr;''')
s=s.replace('tracker->finishRound(); tracker->reset(); diagnostics::current_round = block + 1;', '''tracker->finishRound();
            std::uint64_t boundary_elapsed{};
            if (mixed) {
                diagnostics::active.store(nullptr, std::memory_order_release);
                const auto boundary_start{diagnostics::monotonicNs()};
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
                bool complete{};
                while (diagnostics::monotonicNs() - boundary_start < 250000000ULL) {
                    complete = client.region()->oldest_unsynced_ns.load() == 0 && client.region()->sync_finished_ns.load() >= client.region()->sync_started_ns.load();
                    for (const auto &producer : client.region()->producers) { complete = complete && producer.position.written.load() == producer.position.generated.load() && producer.position.synced.load() == producer.position.generated.load() && producer.normal.write.load() == producer.normal.read.load() && producer.critical.write.load() == producer.critical.read.load(); }
                    if (complete) { break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
                boundary_elapsed = diagnostics::monotonicNs() - boundary_start;
                if (!complete || client.region()->error.load()) { throw std::runtime_error("Mixed boundary not fully drained and synced"); }
                diagnostics::active.store(assignment[block] ? basic_recorder : nullptr, std::memory_order_release);
            }
            tracker->reset(); diagnostics::current_round = block + 1;''')
s=s.replace('for (int i{}; i < 40; ++i) {\n                sample', 'const auto templates_before{counted ? counted->templates : 0}, searches_before{counted ? counted->searches : 0};\n            for (int i{}; i < 40; ++i) {\n                sample')
s=s.replace('sample->score = tracker->lastNanoScore(); sample->box = tracker->lastNanoBox(); sample = nullptr;\n            }\n        }', '''sample->score = tracker->lastNanoScore(); sample->box = tracker->lastNanoBox(); sample = nullptr;
            }
            if (mixed) {
                const auto same_box = [](const BBoxRect &a, const BBoxRect &b) { return a.cx == b.cx && a.cy == b.cy && a.width == b.width && a.height == b.height && a.is_valid == b.is_valid; };
                bool same{true};
                for (int index{}; index < 40; ++index) {
                    const auto &a{(*samples)[index]}, &b{(*samples)[block * 40 + index]};
                    same = same && a.score == b.score && a.box == b.box && a.output.state == b.output.state && a.output.consumed == b.output.consumed && a.output.tracking == b.output.tracking && same_box(a.output.display_box, b.output.display_box) && same_box(a.output.realtime_box, b.output.realtime_box);
                }
                blocks << block << ',' << block / 2 << ',' << assignment[block] << ',' << counted->templates - templates_before << ',' << counted->searches - searches_before << ',' << boundary_elapsed << ",1," << same << '\\n'; blocks.flush();
                if (!same || counted->templates - templates_before != 1 || counted->searches - searches_before != 39) { throw std::runtime_error("Mixed actual model workload or output changed"); }
            }
        }''')
s=s.replace('tracker->finishRound(); client.finish();', 'tracker->finishRound(); if (mixed) { diagnostics::active.store(basic_recorder, std::memory_order_release); } client.finish();')
s=s.replace('rvalid,preparation_ns,preparation_count";', 'rvalid,preparation_ns,preparation_count,mode";').replace("<< v.preparation_count;", "<< v.preparation_count << ',' << assignment[n / 40];")
p.write_text(s)
