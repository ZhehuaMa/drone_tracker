#include "realtime_source.h"

#include <cerrno>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "diagnostics/recorder.h"
#include "platform/frame_source_ops.h"
#include <rclcpp/time.hpp>
#include <yaml-cpp/yaml.h>

namespace {
using namespace std::chrono_literals;
using tracking_regression::RealtimeSource;
using tracking_regression::SourceOptions;
void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error{message}; }
}
struct TemporaryDirectory {
    std::filesystem::path path{std::filesystem::temp_directory_path()
        / ("tracking-regression-source-" + std::to_string(::getpid()))};
    TemporaryDirectory() { require(std::filesystem::create_directory(path), "test directory already exists"); }
    ~TemporaryDirectory() { std::error_code error{}; std::filesystem::remove_all(path, error); }
};
void generateFixture(const std::filesystem::path &fixture) {
    std::filesystem::create_directory(fixture);
    std::ofstream frames{fixture / "frames.jsonl"};
    for (std::uint64_t id{}; id < 1200; ++id) {
        const std::uint64_t relative{15721 + (19992663 - 15721) * id / 1199};
        frames << "{\"fixture_frame_id\":" << id << ",\"relative_us\":" << relative << ",\"pixel_sha256\":\"0d2039545940e7775d22c8d82079282c91be29af0203faa77e2bff78a67bed0f\"}\n";
    }
    require(static_cast<bool>(frames), "fixture frame manifest write failed");
    std::ofstream attitude{fixture / "attitude.jsonl"};
    for (const std::int64_t relative : {-2000, 100000, 300000, 19999999}) {
        attitude << "{\"relative_us\":" << relative
            << ",\"frame_id\":\"test-gimbal\",\"orientation\":[0,0,0,1],"
            "\"orientation_covariance\":[0,0,0,0,0,0,0,0,0],\"angular_velocity\":[0,0,0],"
            "\"angular_velocity_covariance\":[0,0,0,0,0,0,0,0,0],\"linear_acceleration\":[0,0,0],"
            "\"linear_acceleration_covariance\":[0,0,0,0,0,0,0,0,0]}\n";
    }
    require(static_cast<bool>(attitude), "fixture attitude write failed");
    const std::string clip{(fixture / "clip.mkv").string()};
    const int pid{::fork()};
    require(pid >= 0, "fixture encoder fork failed");
    if (pid == 0) {
        ::execl(TRACKING_REGRESSION_FFMPEG, "ffmpeg", "-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=1920x1200:r=60", "-frames:v", "1200", "-an", "-threads", "1",
            "-c:v", "ffv1", "-level", "3", "-pix_fmt", "gray", clip.c_str(), nullptr);
        ::_exit(127);
    }
    int result{};
    while (::waitpid(pid, &result, 0) < 0 && errno == EINTR) {}
    require(WIFEXITED(result) && WEXITSTATUS(result) == 0, "FFmpeg fixture generation failed");
}
SourceOptions options(const std::filesystem::path &root, const std::string &name) {
    SourceOptions value{};
    value.fixture = root / "fixture";
    value.output = root / name;
    std::filesystem::create_directory(value.output);
    value.max_supply_lateness_us = 1000000;
    return value;
}
template <typename Predicate>
void until(Predicate predicate, std::chrono::milliseconds timeout, const std::string &message) {
    const auto end{std::chrono::steady_clock::now() + timeout};
    while (!predicate() && std::chrono::steady_clock::now() < end) { std::this_thread::sleep_for(2ms); }
    require(predicate(), message);
}
std::vector<YAML::Node> input(const std::filesystem::path &output) {
    std::ifstream file{output / "input.jsonl"};
    require(static_cast<bool>(file), "source input export missing");
    std::vector<YAML::Node> rows{};
    std::string line{};
    while (std::getline(file, line)) { rows.push_back(YAML::Load(line)); }
    require(rows.size() == 1200, "source did not preserve all fixture identities");
    for (std::size_t id{}; id < rows.size(); ++id) {
        require(rows[id]["fixture_frame_id"].as<std::size_t>() == id, "fixture identity reordered");
    }
    return rows;
}
void noChild() {
    int result{};
    const int pid{::waitpid(-1, &result, WNOHANG)};
    require(pid == -1 && errno == ECHILD, "source left a live decoder or unreaped child");
}
struct Recording {
    std::unique_ptr<diagnostics::SharedRegion> region{std::make_unique<diagnostics::SharedRegion>()};
    diagnostics::Recorder recorder{*region, -1, diagnostics::Mode::Basic};
    Recording() {
        region->run_id = 123;
        diagnostics::current_producer = diagnostics::Producer::Node;
        diagnostics::current_update = diagnostics::current_request = diagnostics::current_tracker_frame = 0;
        diagnostics::active.store(&recorder);
    }
    ~Recording() { diagnostics::active.store(nullptr); }
};
void slowConsumerAndEof(const std::filesystem::path &root) {
    const auto config{options(root, "slow-consumer")};
    Recording recording{};
    std::vector<std::pair<std::uint64_t, std::int64_t>> attitudes{};
    bool attitude_payload_ok{true};
    RealtimeSource source{config};
    source.setAttitudeCallback([&](sensor_msgs::msg::Imu::SharedPtr message) {
        attitude_payload_ok = attitude_payload_ok && message->header.frame_id == "test-gimbal"
            && message->orientation.w == 1;
        attitudes.emplace_back(diagnostics::monotonicNs(), rclcpp::Time{message->header.stamp}.nanoseconds() / 1000);
    });
    require(source.prepare(), "full clip preparation failed");
    const auto preparation{YAML::LoadFile((config.output / "preparation.json").string())};
    require(preparation["ready"].as<bool>() && preparation["verified_frame_count"].as<unsigned>() == 1200
        && preparation["cached_pixel_bytes"].as<std::uint64_t>() == 2764800000ULL,
        "preparation omitted complete verified cache or retained the old memory ceiling");
    require(!std::filesystem::exists(config.output / "input-clock.json") && source.status().read == 0,
        "preparation started measured playback");
    // Once ready, playback must depend entirely on the retained pixels rather than decoding the file again.
    const auto hidden{config.fixture / "clip-hidden.mkv"};
    std::filesystem::rename(config.fixture / "clip.mkv", hidden);
    require(source.start(), "source startup failed");
    std::filesystem::rename(hidden, config.fixture / "clip.mkv");
    const auto clock{YAML::LoadFile((config.output / "input-clock.json").string())};
    const std::uint64_t origin{clock["m0_ns"].as<std::uint64_t>()};
    const std::uint64_t epoch{clock["e0_us"].as<std::uint64_t>()};
    require(origin > preparation["end_mono_ns"].as<std::uint64_t>(), "clock started before preparation completed");
    until([&] { return source.status().read != 0; }, 2s, "source never supplied first frame");
    const std::uint64_t before{source.status().read};
    // The consumer is blocked for the observed long-call duration, while the source must keep advancing.
    std::this_thread::sleep_for(388ms);
    require(source.status().read >= before + 15, "blocked consumer stalled independent supplier");
    FrameRef selected{};
    require(waitForLatestFrame(source, selected, 1s), "production latest-frame consumer failed");
    require(selected && selected->width() == 1920 && selected->height() == 1200
        && selected->pixelFormat() == FramePixelFormat::Gray8, "decoded frame contract changed");
    const auto &ring{recording.region->producers[0].normal};
    require(ring.write.load() >= 15 && ring.write.load() <= config.queue_capacity,
        "production selection did not skip accumulated frames");
    std::set<std::uint64_t> consumed{};
    for (std::uint64_t index{}; index < ring.write.load(); ++index) {
        const auto &event{ring.events[index]};
        require((event.flags & diagnostics::FrameTimeKnown) != 0, "consumer event lost exact frame time");
        require(consumed.insert(event.frame_time_us).second, "consumer duplicated frame disposition");
        require(event.type == (index + 1 == ring.write.load()
            ? diagnostics::Type::FrameSelect : diagnostics::Type::FrameSkip), "latest selection order changed");
        if (index + 1 == ring.write.load()) {
            require(event.frame_time_us == selected->timestampUs(), "selected event belongs to another frame");
        } else {
            require(event.frame_time_us < selected->timestampUs(), "selected frame was older than skipped frame");
        }
    }
    until([&] { return source.status().state != FrameSourceState::Running; }, 22s, "source EOF missing");
    const auto status{source.status()};
    require(status.state == FrameSourceState::Eof && status.read == 1200, "normal EOF lost supplied frames");
    require(diagnostics::monotonicNs() >= origin + 20000000000ULL, "EOF truncated fixed input window");
    source.stop();
    require(attitude_payload_ok && attitudes.size() == 4
        && attitudes.front().second == static_cast<std::int64_t>(epoch) - 2000,
        "initial attitude timestamp did not use frame epoch");
    for (std::size_t index{1}; index < attitudes.size(); ++index) {
        const std::uint64_t relative{index == 1 ? 100000ULL : (index == 2 ? 300000ULL : 19999999ULL)};
        require(attitudes[index].first >= origin + relative * 1000
            && attitudes[index].second == static_cast<std::int64_t>(epoch + relative),
            "future attitude was delivered early or remapped incorrectly");
    }
    const auto rows{input(config.output)};
    std::size_t rejected{}, residual{};
    for (const auto &row : rows) {
        const std::uint64_t relative{row["relative_us"].as<std::uint64_t>()};
        require(row["supplied"].as<bool>(), "normal EOF omitted a frame");
        require(row["planned_mono_ns"].as<std::uint64_t>() == origin + relative * 1000,
            "supplier changed original relative frame deadline");
        require(row["frame_time_us"].as<std::uint64_t>() == epoch + relative,
            "prediction frame timestamp lost epoch mapping");
        require(row["supplied_mono_ns"].as<std::uint64_t>() >= origin + relative * 1000,
            "supplier delivered a future frame before its deadline");
        if (!row["enqueued"].as<bool>()) { ++rejected; }
        if (row["residual"].as<bool>()) {
            ++residual;
            require(consumed.count(row["frame_time_us"].as<std::uint64_t>()) == 0,
                "consumed frame also classified as residual");
        }
    }
    require(rows.front()["planned_mono_ns"].as<std::uint64_t>() == origin + 15721000,
        "first frame offset was incorrectly normalized to zero");
    require(residual == config.queue_capacity && rejected == status.dropped
        && consumed.size() + residual + rejected == 1200, "frame disposition conservation failed");
    const auto completion{YAML::LoadFile((config.output / "source-completion.json").string())};
    require(completion["normal_eof"].as<bool>(), "normal EOF export missing");
    FrameRef leftover{};
    require(!source.tryFrame(leftover), "stop did not release residual queue");
    noChild();
}
void rejectAndExternalStop(const std::filesystem::path &root) {
    auto config{options(root, "external-stop")};
    config.queue_capacity = 1;
    RealtimeSource source{config};
    require(source.start(), "bounded queue source startup failed");
    until([&] { return source.status().read >= 4; }, 2s, "source stalled at full queue");
    require(source.status().enqueued == 1 && source.status().dropped >= 3,
        "capacity-one queue did not reject new arrivals");
    const auto begin{std::chrono::steady_clock::now()};
    source.stop();
    require(std::chrono::steady_clock::now() - begin < 2s, "external stop exceeded shutdown bound");
    require(source.status().state == FrameSourceState::Stopped, "early external stop marked EOF");
    const auto rows{input(config.output)};
    require(rows.front()["residual"].as<bool>() && !rows.back()["supplied"].as<bool>(),
        "external stop did not preserve residual and unsupplied identities");
    const auto completion{YAML::LoadFile((config.output / "source-completion.json").string())};
    require(!completion["normal_eof"].as<bool>(), "external stop incorrectly qualified as complete");
    noChild();
}
void latenessInvalidates(const std::filesystem::path &root) {
    auto config{options(root, "late")};
    config.max_supply_lateness_us = 1;
    RealtimeSource source{config};
    // Initial callback is deliberately slower than the first deadline; no scheduler luck is required.
    source.setAttitudeCallback([](sensor_msgs::msg::Imu::SharedPtr) { std::this_thread::sleep_for(40ms); });
    require(source.start(), "late source startup failed");
    until([&] { return source.status().read != 0; }, 2s, "late source supplied nothing");
    source.stop();
    require(source.status().reason == "supply_lateness_exceeded", "lateness budget did not invalidate round");
    const auto rows{input(config.output)};
    require(rows.front()["supplied_mono_ns"].as<std::uint64_t>()
        > rows.front()["planned_mono_ns"].as<std::uint64_t>() + 1000, "lateness injection did not execute");
    noChild();
}
void decodeFailure(const std::filesystem::path &root) {
    const auto damaged{root / "damaged"};
    std::filesystem::create_directory(damaged);
    std::filesystem::copy_file(root / "fixture/frames.jsonl", damaged / "frames.jsonl");
    std::filesystem::copy_file(root / "fixture/attitude.jsonl", damaged / "attitude.jsonl");
    std::ofstream invalid{damaged / "clip.mkv"}; invalid << "not an FFV1 file"; invalid.close();
    auto config{options(root, "decode-failure")}; config.fixture = damaged;
    RealtimeSource source{config};
    require(!source.start(), "malformed input decoder incorrectly started");
    source.stop();
    require(source.status().state == FrameSourceState::Failed, "decode failure not retained");
    require(!source.status().reason.empty(), "decode failure reason missing");
    const auto rows{input(config.output)};
    require(!rows.front()["supplied"].as<bool>(), "decode failure fabricated input");
    noChild();
}
void optionValidation(const std::filesystem::path &root) {
    auto config{options(root, "missing-budget")};
    config.max_supply_lateness_us = 0;
    bool rejected{};
    try { RealtimeSource source{config}; }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "missing lateness budget accepted");
    config = options(root, "full-queue");
    config.queue_capacity = 1200;
    RealtimeSource source{config};
    require(source.status().state == FrameSourceState::Ready, "old 256 MiB queue limit remains");
}
void allocationFailure(const std::filesystem::path &root) {
    auto config{options(root, "allocation-failure")};
    const int pid{::fork()};
    require(pid >= 0, "allocation test fork failed");
    if (pid == 0) {
        try {
            RealtimeSource source{config};
            std::ifstream memory{"/proc/self/statm"};
            std::uint64_t pages{}; memory >> pages;
            const auto bytes{pages * static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE))};
            const struct rlimit limit{bytes + 128ULL * 1024 * 1024, bytes + 128ULL * 1024 * 1024};
            require(::setrlimit(RLIMIT_AS, &limit) == 0, "allocation failure injection failed");
            require(!source.prepare(), "resource-limited preparation incorrectly succeeded");
            source.stop();
            require(source.status().state == FrameSourceState::Failed, "allocation failure was not retained");
            require(!std::filesystem::exists(config.output / "input-clock.json"), "allocation failure started playback");
            const auto record{YAML::LoadFile((config.output / "preparation.json").string())};
            require(!record["ready"].as<bool>() && !record["reason"].as<std::string>().empty(),
                "allocation failure evidence missing");
            ::_exit(0);
        } catch (const std::exception &error) { std::cerr << error.what() << '\n'; ::_exit(1); }
    }
    int result{};
    while (::waitpid(pid, &result, 0) < 0 && errno == EINTR) {}
    require(WIFEXITED(result) && WEXITSTATUS(result) == 0, "allocation failure did not cleanly reject preparation");
}
void pixelFailure(const std::filesystem::path &root) {
    const auto damaged{root / "pixel-damaged"};
    std::filesystem::create_directory(damaged);
    std::filesystem::copy_file(root / "fixture/frames.jsonl", damaged / "frames.jsonl");
    std::filesystem::copy_file(root / "fixture/attitude.jsonl", damaged / "attitude.jsonl");
    std::filesystem::create_hard_link(root / "fixture/clip.mkv", damaged / "clip.mkv");
    auto text{std::string{}};
    { std::ifstream stream{damaged / "frames.jsonl"}; text.assign(std::istreambuf_iterator<char>{stream}, {}); }
    const auto offset{text.find("pixel_sha256\":\"") + 15};
    text[offset] = text[offset] == '0' ? '1' : '0';
    { std::ofstream stream{damaged / "frames.jsonl"}; stream << text; }
    auto config{options(root, "pixel-failure")}; config.fixture = damaged;
    RealtimeSource source{config};
    require(!source.prepare(), "altered cached pixel hash accepted");
    source.stop();
    require(source.status().reason.find("cached pixel checksum mismatch at frame 0") != std::string::npos,
        "actual cached pixels were not verified");
    require(!std::filesystem::exists(config.output / "input-clock.json"), "pixel failure fabricated a playback clock");
    const auto preparation{YAML::LoadFile((config.output / "preparation.json").string())};
    require(!preparation["ready"].as<bool>() && preparation["end_mono_ns"].as<std::uint64_t>() > 0,
        "preparation failure evidence missing");
}
} // namespace
int main() {
    try {
        TemporaryDirectory temporary{};
        generateFixture(temporary.path / "fixture");
        optionValidation(temporary.path);
        decodeFailure(temporary.path);
        pixelFailure(temporary.path);
        allocationFailure(temporary.path);
        slowConsumerAndEof(temporary.path);
        rejectAndExternalStop(temporary.path);
        latenessInvalidates(temporary.path);
        std::cout << "Realtime source, production latest selection, EOF, rejection and stop checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
