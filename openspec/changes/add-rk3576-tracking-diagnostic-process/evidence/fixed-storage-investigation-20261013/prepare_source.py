from pathlib import Path
p=Path(__file__).parent/'source'; w=p/'src/diagnostics/worker.cpp';s=w.read_text()
s=s.replace('#include <memory>','#include <memory>\n#include <fstream>')
s=s.replace('std::uint64_t last_health_ns{};', 'std::uint64_t last_health_ns{};\n    std::string identity{};')
s=s.replace('control.result = 0;', 'session->identity = std::string(control.identity.data(), strnlen(control.identity.data(), control.identity.size()));\n                    control.result = 0;')
s=s.replace('StorageConfig config{};', 'StorageConfig config{};\n    std::filesystem::path switch_control{}, tmpfs_directory{};\n    int switched{-1};\n    struct SyncSample { int block; std::uint64_t start, end; bool okay; };\n    std::vector<SyncSample> sync_samples{};\n    sync_samples.reserve(5000);')
s=s.replace('if (option == "--socket") {', 'if (option == "--test-switch-control") {\n                switch_control = value;\n            } else if (option == "--test-tmpfs-directory") {\n                tmpfs_directory = value;\n            } else if (option == "--socket") {')
s=s.replace('::signal(SIGINT, stop);','if (switch_control.empty() != tmpfs_directory.empty()) { throw std::runtime_error("Incomplete test switch arguments"); }\n        ::signal(SIGINT, stop);')
anchor='                std::uint64_t notification{};'
insert='''                // Test-only handoff: the producer is stopped; keep its region, recorder, consumer and positions.
                if (!switch_control.empty()) {
                    std::ifstream request{switch_control / "request"};
                    int sequence{-1}, destination{-1};
                    if (request >> sequence >> destination && sequence != switched) {
                        bool drained{empty(*session->region) && session->region->error.load() == 0 &&
                            session->region->oldest_unsynced_ns.load() == 0 &&
                            session->region->sync_finished_ns.load() >= session->region->sync_started_ns.load()};
                        for (const auto &producer : session->region->producers) {
                            drained = drained && producer.position.generated.load() == producer.position.written.load() &&
                                producer.position.generated.load() == producer.position.synced.load() &&
                                producer.position.dropped.load() == 0;
                        }
                        if (sequence != switched + 1 || destination < 0 || destination > 1 || !drained) {
                            throw std::runtime_error("Unsafe test storage switch");
                        }
                        if (!session->storage->finish(true)) { throw std::runtime_error("Old test segment finish failed"); }
                        session->storage.reset();
                        StorageConfig next{config};
                        next.directory = (destination ? tmpfs_directory : config.directory) / ("block-" + std::to_string(sequence));
                        session->storage = std::make_unique<Storage>(next, *session->region);
                        if (!session->storage->open() || !session->storage->identity(session->identity.data(), session->identity.size()) ||
                            !session->storage->synchronize()) { throw std::runtime_error("New test segment open failed"); }
                        session->last_health_ns = monotonicNs();
                        switched = sequence;
                        std::ofstream acknowledgement{switch_control / "ack.tmp"};
                        acknowledgement << sequence << ' ' << destination << '\\n'; acknowledgement.close();
                        std::filesystem::rename(switch_control / "ack.tmp", switch_control / "ack");
                        std::cout << "switch " << sequence << ' ' << destination << ' ' << session->storage->path() << std::endl;
                    }
                }
'''
assert anchor in s;s=s.replace(anchor,insert+anchor)
s=s.replace('session->storage->synchronize();', '''const auto begin{monotonicNs()};
                    const bool okay{session->storage->synchronize()};
                    if (!switch_control.empty()) { sync_samples.push_back({switched, begin, monotonicNs(), okay}); }''')
s=s.replace('::close(listener);', '''if (!switch_control.empty()) {
            std::ofstream sync{switch_control / "sync.csv"};
            sync << "block,start_ns,end_ns,okay\\n";
            for (const auto &entry : sync_samples) { sync << entry.block << ',' << entry.start << ',' << entry.end << ',' << entry.okay << '\\n'; }
        }
        ::close(listener);''')
w.write_text(s)
f=p/'src/fixed_workload_probe.cpp';s=f.read_text();s=s.replace('#include <random>','#include <random>\n#include <filesystem>')
s=s.replace('if (mixed && (observed || audit || cycles % 2))','if (mixed && (observed || cycles % 2))')
s=s.replace('diagnostics::active.store(assignment[block] ? basic_recorder : nullptr, std::memory_order_release);', '''const auto control{std::filesystem::path{std::getenv("DRONE_STORAGE_CONTROL")}};
                { std::ofstream request{control / "request.tmp"}; request << block << ' ' << assignment[block] << '\\n'; }
                std::filesystem::rename(control / "request.tmp", control / "request");
                const auto switch_begin{diagnostics::monotonicNs()};
                bool acknowledged{};
                while (diagnostics::monotonicNs() - switch_begin < 500000000ULL) {
                    std::ifstream ack{control / "ack"}; int sequence{-1}, destination{-1};
                    if (ack >> sequence >> destination && sequence == block && destination == assignment[block]) { acknowledged = true; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
                if (!acknowledged) { throw std::runtime_error("Storage switch acknowledgement timeout"); }
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
                metadata << "switch_" << block << "_ns=" << diagnostics::monotonicNs() - switch_begin << '\\n'; metadata.flush();
                diagnostics::active.store(basic_recorder, std::memory_order_release);''')
s=s.replace('tracker->finishRound();\n        if (mixed)', '''tracker->finishRound();
        if (mixed)''')
s=s.replace('client.finish();', 'if (!client.finish()) { throw std::runtime_error("Final client drain failed"); }')
# Preserve warmup outputs and wall/thread timing rather than retaining only console text.
s=s.replace('for (int block{}; block < 2; ++block) {', '''std::ofstream warmups{std::string{argv[4]} + "/warmups.csv"};
        warmups << "block,position,duration_ns,cpu_ns,state,score,x,y,w,h\\n";
        warmups << std::setprecision(9);
        for (int block{}; block < 2; ++block) {''',1)
s=s.replace('tracker->update(frame);', '''const auto cpu_begin{cpuNs()}, wall_begin{diagnostics::monotonicNs()};
                const auto output{tracker->update(frame)};
                const auto wall{diagnostics::monotonicNs() - wall_begin}, cpu{cpuNs() - cpu_begin};
                const auto box{tracker->lastNanoBox()};
                warmups << block << ',' << (&frame - frames.data() + 1) << ',' << wall << ',' << cpu << ',' << static_cast<int>(output.state) << ',' << tracker->lastNanoScore() << ',' << box.x << ',' << box.y << ',' << box.width << ',' << box.height << '\\n';''',1)
f.write_text(s)
