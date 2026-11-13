#include "bag_session.h"

#include <iostream>
#include <atomic>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <rosbag2_cpp/reader.hpp>

namespace {
using namespace std::chrono_literals;
void require(bool value, const char *message) { if (!value) { throw std::runtime_error(message); } }
template<typename Predicate> void until(Predicate predicate, const char *message) {
    const auto end{std::chrono::steady_clock::now() + 5s};
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) { throw std::runtime_error(message); }
        std::this_thread::sleep_for(2ms);
    }
}
struct Gate {
    std::mutex mutex{}; std::condition_variable changed{}; bool entered{}; std::atomic_bool released{};
    void block() {
        std::unique_lock<std::mutex> lock{mutex}; entered = true; changed.notify_all();
        changed.wait(lock, [&] { return released.load(); });
    }
    void wait() {
        std::unique_lock<std::mutex> lock{mutex};
        if (!changed.wait_for(lock, 5s, [&] { return entered; })) { throw std::runtime_error("writer gate timeout"); }
    }
    void release() { std::lock_guard<std::mutex> lock{mutex}; released = true; changed.notify_all(); }
};
void inFlight(const std::filesystem::path &root) {
    Gate gate{}; BagSession::Status result{};
    BagSession::Options options{}; options.catalog = recording::topicCatalog();
    BagSession session{(root / "drone_rosbag_inflight").string(), [&](const char *stage) {
        if (std::string{stage} == "write" && !gate.released) { gate.block(); }
    }, [&](const auto &status) { result = status; }, options};
    auto image{std::make_shared<sensor_msgs::msg::Image>()}; image->data.resize(20U * 1024U * 1024U);
    const bool first{session.submit(image, "/fpv/tracker_image", rclcpp::Time{1})};
    if (!first) { gate.release(); session.stop(); throw std::runtime_error("first large image rejected"); }
    gate.wait(); const auto retained{session.snapshot()};
    const auto begin{std::chrono::steady_clock::now()};
    const bool second{session.submit(image, "/fpv/tracker_image", rclcpp::Time{2})};
    const auto elapsed{std::chrono::steady_clock::now() - begin};
    auto off{std::make_shared<std_msgs::msg::Header>()}; off->frame_id = "OFF";
    const bool control{session.submit(off, "/fpv/tracker_trigger", rclcpp::Time{3})};
    const bool pair{session.submitPair(std::make_shared<vision_msgs::msg::Detection2D>(),
        std::make_shared<geometry_msgs::msg::TwistStamped>(), rclcpp::Time{3}, 0, 0,
        "/fpv/target_bbox", "/fpv/target_los", true)};
    gate.release(); session.stop();
    require(retained.queued == 1 && retained.queued_bytes >= image->data.capacity(), "in-flight image budget released early");
    require(!second && elapsed < 100ms && control && pair, "image saturation blocked control/admission");
    require(result.phase == "incomplete" && result.topics.at("/fpv/tracker_image").written == 1,
        "image gap accounting lost");
}
void split(const std::filesystem::path &root) {
    BagSession::Status result{}; BagSession::Options options{}; options.catalog = recording::topicCatalog();
    options.max_file_bytes = 100000;
    const auto path{root / "drone_rosbag_split"};
    BagSession session{path.string(), {}, [&](const auto &status) { result = status; }, options};
    auto image{std::make_shared<sensor_msgs::msg::Image>()}; image->data.resize(200000, 71);
    for (std::uint64_t index{}; index < 8; ++index) {
        require(session.submit(image, "/fpv/tracker_image", rclcpp::Time{static_cast<std::int64_t>(index + 1)}), "split image rejected");
        until([&] { return session.snapshot().topics.at("/fpv/tracker_image").written == index + 1;
        }, "split writer stalled");
    }
    session.stop(); require(result.phase == "complete", "split session incomplete");
    std::size_t files{};
    for (const auto &entry : std::filesystem::directory_iterator{path}) { files += entry.path().extension() == ".db3"; }
    require(files > 1, "file-size target did not split");
    rosbag2_cpp::Reader reader{}; reader.open(path.string()); std::uint64_t count{};
    while (reader.has_next()) {
        const auto item{reader.read_next()};
        require(item->time_stamp == static_cast<std::int64_t>(++count), "split changed timestamp");
    }
    require(count == 8, "split readback lost messages");
}
void fairnessAndReserve(const std::filesystem::path &root) {
    Gate gate{}; BagSession *pointer{}; std::vector<std::pair<std::uint64_t, std::uint64_t>> order{};
    BagSession session{(root / "drone_rosbag_fairness").string(), [&](const char *stage) {
        if (std::string{stage} == "open") { gate.block(); }
        if (std::string{stage} == "write") {
            const auto status{pointer->snapshot()};
            order.emplace_back(status.topics.at("/control").written, status.topics.at("/ordinary").written);
        }
    }, {}};
    pointer = &session; gate.wait();
    auto value{std::make_shared<std_msgs::msg::Bool>()};
    bool accepted{session.submit(value, "/ordinary", rclcpp::Time{1})};
    for (int index{}; index < 9; ++index) {
        accepted = session.submit(value, "/control", rclcpp::Time{1}, 0, true) && accepted;
    }
    gate.release(); session.stop();
    require(accepted && order.size() == 10 && order[8] == std::make_pair<std::uint64_t, std::uint64_t>(8, 0)
        && order[9] == std::make_pair<std::uint64_t, std::uint64_t>(8, 1), "ordinary work starved behind control");

    Gate reserve_gate{}; BagSession::Status result{};
    BagSession reserved{(root / "drone_rosbag_reserved").string(), [&](const char *stage) {
        if (std::string{stage} == "open") { reserve_gate.block(); }
    }, [&](const auto &status) { result = status; }};
    reserve_gate.wait(); bool filled{true};
    for (std::size_t index{}; index < 240; ++index) {
        filled = reserved.submit(value, "/ordinary_" + std::to_string(index / 30), rclcpp::Time{1}) && filled;
    }
    const bool rejected{!reserved.submit(value, "/additional", rclcpp::Time{1})};
    auto off{std::make_shared<std_msgs::msg::Header>()}; off->frame_id = "OFF";
    const bool off_admitted{reserved.submit(off, "/off", rclcpp::Time{1}, 0, true)};
    const bool pair_admitted{reserved.submitPair(std::make_shared<vision_msgs::msg::Detection2D>(),
        std::make_shared<geometry_msgs::msg::TwistStamped>(), rclcpp::Time{1}, 0, 0,
        "/bbox", "/los", true)};
    for (int index{}; index < 13; ++index) {
        filled = reserved.submit(value, "/protected", rclcpp::Time{1}, 0, true) && filled;
    }
    const bool full_rejected{!reserved.submit(value, "/protected", rclcpp::Time{1}, 0, true)};
    const auto peak{reserved.snapshot().peak_messages};
    reserve_gate.release(); reserved.stop();
    require(filled && rejected && off_admitted && pair_admitted && full_rejected && peak == 256,
        "reserved capacity increased global bound or excluded OFF pair");
    require(result.phase == "incomplete", "reserved queue gaps not reported");
}
void faults(const std::filesystem::path &root) {
    {
        Gate gate{}; BagSession::Status result{};
        BagSession session{(root / "drone_rosbag_callback_failure").string(), [&](const char *operation) {
            if (std::string{operation} == "open") { gate.block(); }
        }, [&](const auto &status) { result = status; }};
        gate.wait(); session.abort("original_callback_failure"); gate.release(); session.stop();
        require(result.phase == "failed" && result.reason == "original_callback_failure",
            "opening completion erased callback failure");
    }
    for (const std::string stage : {"open", "write", "close", "summary"}) {
        BagSession::Status result{};
        BagSession session{(root / ("drone_rosbag_fault_" + stage)).string(), [&](const char *operation) {
            if (stage == operation) { throw std::runtime_error("injected_" + stage); }
        }, [&](const auto &status) { result = status; }};
        session.submit(std::make_shared<std_msgs::msg::Bool>(), "/fpv/arm", rclcpp::Time{1}); session.stop();
        require(result.phase == "failed" && result.reason == "injected_" + stage, "writer failure reason replaced");
        for (const auto &entry : result.topics) { require(entry.second.accepted >= entry.second.written, "negative unwritten count"); }
    }
    for (bool low_free : {false, true}) {
        BagSession::Options options{}; options.storage_observation = [low_free] {
            return low_free ? std::pair<std::uint64_t, std::uint64_t>{0, 1}
                : std::pair<std::uint64_t, std::uint64_t>{5368709120ULL, 9000000000ULL};
        };
        BagSession::Status result{};
        BagSession session{(root / (low_free ? "drone_rosbag_free" : "drone_rosbag_quota")).string(), {},
            [&](const auto &status) { result = status; }, options};
        session.stop();
        require(result.phase == "storage_limited" && result.reason == "storage_limit", "storage guard classified as writer failure");
    }
    const auto existing{root / "drone_rosbag_existing"}; std::filesystem::create_directory(existing);
    { std::ofstream marker{existing / "evidence"}; marker << "retained"; }
    BagSession::Status result{};
    BagSession collision{existing.string(), {}, [&](const auto &status) { result = status; }}; collision.stop();
    require(result.phase == "failed" && std::filesystem::exists(existing / "evidence")
        && !std::filesystem::exists(existing / "session.json"), "collision overwrote existing evidence");
}

void activeStorageLimits(const std::filesystem::path &root) {
    for (bool split_limit : {false, true}) {
        Gate gate{};
        std::atomic_bool limited{};
        BagSession::Options options{};
        options.catalog = recording::topicCatalog();
        options.max_file_bytes = split_limit ? 100000 : 268435456;
        options.storage_observation = [&] {
            return std::make_pair<std::uint64_t, std::uint64_t>(0, limited ? 1 : 9000000000ULL);
        };
        BagSession::Status result{};
        const auto path{root / (split_limit ? "drone_rosbag_active_split" : "drone_rosbag_active_free")};
        BagSession session{path.string(), [&](const char *stage) {
            if (!split_limit && std::string{stage} == "write" && !gate.released) { gate.block(); }
        }, [&](const auto &status) { result = status; }, options};
        struct Release {
            Gate &gate;
            ~Release() { gate.release(); }
        } release{gate};
        auto image{std::make_shared<sensor_msgs::msg::Image>()};
        image->data.resize(200000, 33);
        require(session.submit(image, "/fpv/tracker_image", rclcpp::Time{1}), "active-limit first item rejected");
        if (split_limit) {
            until([&] { return session.snapshot().topics.at("/fpv/tracker_image").written == 1; },
                "split-limit first write stalled");
        } else {
            gate.wait();
        }
        limited = true;
        require(session.submit(image, "/fpv/tracker_image", rclcpp::Time{2}), "active-limit second item rejected");
        if (!split_limit) {
            // The first write stays in flight while another writer consumes the observed free space.
            std::this_thread::sleep_for(120ms);
            gate.release();
        }
        until([&] { return session.snapshot().reason == "storage_limit"; }, "active storage guard did not seal");
        const bool rejected{!session.submit(image, "/fpv/tracker_image", rclcpp::Time{3})};
        session.stop();
        const auto &counts{result.topics.at("/fpv/tracker_image")};
        require(rejected && result.phase == "storage_limited" && counts.accepted == 2
            && counts.written == 1 && counts.accepted - counts.written == 1,
            "active/split limit lost accepted-but-unwritten work or reopened admission");
        require(std::filesystem::exists(path / "session.json"), "storage-limited summary missing");
    }
}

void storageAccounting(const std::filesystem::path &root) {
    for (bool summary_exceeds : {false, true}) {
        const auto parent{root / (summary_exceeds ? "account_summary" : "account_symlinks")};
        const auto old{parent / "drone_rosbag_incomplete"};
        const auto external{parent / "unowned"};
        std::filesystem::create_directories(old);
        std::filesystem::create_directories(external);
        { std::ofstream file{old / "partial.db3"}; file << std::string(8192, 'p'); }
        { std::ofstream file{old / "session.json"}; file << std::string(4096, 's'); }
        { std::ofstream file{external / "foreign.bin"}; file << std::string(262144, 'x'); }
        std::filesystem::create_directory_symlink(external, parent / "drone_rosbag_foreign_link");
        std::filesystem::create_directory_symlink(external, old / "nested_link");
        std::filesystem::create_symlink(external / "foreign.bin", old / "file_link");
        BagSession::Options options{};
        options.min_free_bytes = 1;
        options.max_total_bytes = summary_exceeds ? 8193 : 12289;
        BagSession::Status result{};
        BagSession session{(parent / "drone_rosbag_new").string(), {},
            [&](const auto &status) { result = status; }, options};
        session.stop();
        require(result.phase == (summary_exceeds ? "storage_limited" : "complete"),
            "owned incomplete/summary files were omitted or symlinks were followed");
        require(std::filesystem::file_size(old / "partial.db3") == 8192
            && std::filesystem::file_size(old / "session.json") == 4096
            && std::filesystem::file_size(external / "foreign.bin") == 262144,
            "storage accounting changed existing evidence");
    }
}
} // namespace
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    const auto root{std::filesystem::temp_directory_path() / ("recording-writer-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directories(root);
    try {
        inFlight(root);
        fairnessAndReserve(root);
        split(root);
        faults(root);
        activeStorageLimits(root);
        storageAccounting(root);
        std::filesystem::remove_all(root);
    }
    catch (const std::exception &error) { std::cerr << error.what() << " evidence=" << root << '\n';
    rclcpp::shutdown(); return 1; }
    rclcpp::shutdown(); return 0;
}
