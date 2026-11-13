#ifndef DRONE_TRACKER_BAG_SESSION_H
#define DRONE_TRACKER_BAG_SESSION_H

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "platform/cpu_affinity.h"
#include "recording/policy.h"
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_cpp/writers/sequential_writer.hpp>
#include <rmw/rmw.h>

/** @brief Session admission never calls storage; one worker owns the writer. */
class BagSession final {
public:
    struct TopicCounts {
        std::uint64_t attempted{}, accepted{}, written{}, dropped{};
        std::uint64_t policy_skipped{}, byte_limited{}, oversized{}, timestamp_fallback{};
        std::array<std::uint64_t, 4> fallback_reasons{};
        std::uint64_t direct{}, snapshots{}, middleware_lost{}, incompatible_qos{};
        std::size_t retained{}, retained_bytes{};
    };
    struct Status {
        std::string phase{"opening"}, reason{}, end_reason{};
        std::map<std::string, TopicCounts> topics{};
        std::size_t queued{}, queued_bytes{}, peak_bytes{}, peak_messages{};
        std::int64_t start_ns{}, end_ns{};
    };
    struct Options {
        std::vector<recording::TopicDescriptor> catalog{};
        std::uint64_t max_file_bytes{268435456}, max_total_bytes{5368709120ULL}, min_free_bytes{5368709120ULL};
        std::string metadata_json{"{}"};
        std::function<std::pair<std::uint64_t, std::uint64_t>()> storage_observation{};
    };
    using Hook = std::function<void(const char *)>;
    using Report = std::function<void(const Status &)>;
    BagSession(std::string path, Hook hook, Report report)
        : BagSession(std::move(path), std::move(hook), std::move(report), Options{}) {}
    BagSession(std::string path, Hook hook, Report report, Options options)
        : path_{std::move(path)}, hook_{std::move(hook)}, report_{std::move(report)}, options_{std::move(options)} {
        status_.start_ns = systemNs();
        for (const auto &entry : options_.catalog) {
            descriptors_.emplace(entry.name, entry); status_.topics.emplace(entry.name, TopicCounts{});
        }
        thread_ = cpu_affinity::start(cpu_affinity::topicRecordingRole(cpu_affinity::Role::Recording),
            [this] { work(); });
    }
    BagSession(const BagSession &) = delete;
    BagSession &operator=(const BagSession &) = delete;
    ~BagSession() noexcept { stop(); }
    template<typename Message>
    bool submit(std::shared_ptr<Message> message, const std::string &topic, const rclcpp::Time &stamp,
                std::size_t bytes = 0, bool control = false) {
        if (!bytes) { bytes = recording::retainedPayload(*message); }
        Work work{}; work.control = control || descriptor(topic).protected_event;
        work.items.push_back({topic, rosidl_generator_traits::name<Message>(), bytes,
            [message = std::move(message), topic, stamp](rosbag2_cpp::Writer &writer) {
                writer.write(*message, topic, stamp);
            }});
        return admit(std::move(work));
    }
    /** @brief A pair consumes two slots, and cannot be partially admitted. */
    bool submitPair(std::shared_ptr<vision_msgs::msg::Detection2D> bbox,
        std::shared_ptr<geometry_msgs::msg::TwistStamped> los, const rclcpp::Time &stamp,
        std::size_t bbox_bytes = 0, std::size_t los_bytes = 0,
        const std::string &bbox_topic = "/fpv/target_bbox", const std::string &los_topic = "/fpv/target_los",
        bool final_off = false) {
        if (!bbox_bytes) { bbox_bytes = recording::retainedPayload(*bbox); }
        if (!los_bytes) { los_bytes = recording::retainedPayload(*los); }
        Work work{}; work.control = final_off;
        work.items.push_back({bbox_topic, "vision_msgs/msg/Detection2D", bbox_bytes,
            [bbox = std::move(bbox), bbox_topic, stamp](rosbag2_cpp::Writer &writer) {
                writer.write(*bbox, bbox_topic, stamp);
            }});
        work.items.push_back({los_topic, "geometry_msgs/msg/TwistStamped", los_bytes,
            [los = std::move(los), los_topic, stamp](rosbag2_cpp::Writer &writer) {
                writer.write(*los, los_topic, stamp);
            }});
        return admit(std::move(work));
    }
    void note(const std::string &topic, const std::string &outcome, recording::StampFallback fallback = {},
        bool direct = false, bool snapshot = false, std::uint64_t count = 1) {
        std::lock_guard<std::mutex> lock{mutex_}; if (!accepting_) { return; }
        auto &c{status_.topics[topic]};
        if (outcome == "policy") { ++c.attempted; ++c.policy_skipped; }
        if (outcome == "bytes") { ++c.attempted; ++c.byte_limited; }
        if (outcome == "oversized") { ++c.attempted; ++c.oversized; incomplete_ = true; }
        if (outcome == "pending_drop" && count) { c.attempted += count; c.dropped += count; incomplete_ = true; }
        if (fallback != recording::StampFallback::None) {
            ++c.timestamp_fallback; ++c.fallback_reasons[static_cast<std::size_t>(fallback)];
        }
        c.direct += direct ? count : 0; c.snapshots += snapshot ? count : 0;
    }
    void coverage(const std::string &topic, bool incompatible, std::uint64_t count) {
        std::lock_guard<std::mutex> lock{mutex_}; if (!accepting_) { return; }
        auto &c{status_.topics[topic]};
        if (incompatible) { c.incompatible_qos += count; } else { c.middleware_lost += count; }
        incomplete_ = true;
    }
    Status snapshot() const {
        std::lock_guard<std::mutex> lock{mutex_}; auto result{status_};
        result.queued = retained_; result.queued_bytes = retained_bytes_;
        if (incomplete_ && (result.phase == "opening" || result.phase == "recording")) { result.phase = "incomplete"; }
        return result;
    }
    void seal(const std::string &reason = "session_end") noexcept {
        { std::lock_guard<std::mutex> lock{mutex_};
          if (accepting_) { status_.end_reason = reason; status_.end_ns = systemNs(); }
          accepting_ = false; stopping_ = true; }
        changed_.notify_one();
    }
    void stop() noexcept { seal(); if (thread_.joinable()) { thread_.join(); } }
    /** @brief A callback/executor failure must not be reported as a policy-complete close. */
    void abort(const std::string &reason) { fail(reason); changed_.notify_one(); }
    static constexpr std::size_t max_messages{256}, max_bytes{64U * 1024U * 1024U};
    static constexpr std::size_t reserved_messages{16}, reserved_bytes{1024U * 1024U};
    static std::string jsonString(const std::string &value) {
        static constexpr char hex[]{"0123456789abcdef"}; std::string result{"\""};
        for (const unsigned char byte : value) {
            if (byte == '"' || byte == '\\') { result += '\\'; result += static_cast<char>(byte); }
            else if (byte < 32) { result += "\\u00"; result += hex[byte >> 4]; result += hex[byte & 15]; }
            else { result += static_cast<char>(byte); }
        }
        return result + '"';
    }
private:
    class StorageLimit final : public std::runtime_error {
    public:
        StorageLimit() : std::runtime_error{"storage_limit"} {}
    };
    class GuardedWriter final : public rosbag2_cpp::writers::SequentialWriter {
    public:
        explicit GuardedWriter(std::function<bool()> check) : check_{std::move(check)} {}
    protected:
        void split_bagfile() override {
            if (!check_()) { throw StorageLimit{}; }
            rosbag2_cpp::writers::SequentialWriter::split_bagfile();
        }
    private:
        std::function<bool()> check_{};
    };
    struct Entry {
        std::string topic{}, type{}; std::size_t bytes{};
        std::function<void(rosbag2_cpp::Writer &)> write{};
    };
    struct Work { std::vector<Entry> items{}; bool control{}; };
    static std::int64_t systemNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    recording::TopicDescriptor descriptor(const std::string &topic) const {
        const auto found{descriptors_.find(topic)};
        return found == descriptors_.end() ? recording::TopicDescriptor{} : found->second;
    }
    bool admit(Work work) {
        std::lock_guard<std::mutex> lock{mutex_}; if (!accepting_) { return false; }
        std::size_t bytes{}; bool fits{true}, oversized{};
        for (const auto &item : work.items) {
            auto &c{status_.topics[item.topic]}; ++c.attempted; const auto entry{descriptor(item.topic)};
            if (item.bytes > max_bytes || item.bytes > entry.max_bytes) { fits = false; oversized = true; }
            if (c.retained >= entry.max_messages
                || item.bytes > entry.max_bytes - std::min(entry.max_bytes, c.retained_bytes)) { fits = false; }
            if (item.bytes > max_bytes - std::min(max_bytes, bytes)) { fits = false; }
            else { bytes += item.bytes; }
        }
        const auto slots_limit{max_messages - (work.control ? 0 : reserved_messages)};
        const auto bytes_limit{max_bytes - (work.control ? 0 : reserved_bytes)};
        fits = fits && retained_ + work.items.size() <= slots_limit
            && bytes <= bytes_limit - std::min(bytes_limit, retained_bytes_);
        if (!fits) {
            for (const auto &item : work.items) {
                auto &counts{status_.topics[item.topic]};
                if (oversized) { ++counts.oversized; } else { ++counts.dropped; }
            }
            incomplete_ = true; return false;
        }
        for (const auto &item : work.items) {
            auto &c{status_.topics[item.topic]}; ++c.accepted; ++c.retained; c.retained_bytes += item.bytes;
        }
        retained_ += work.items.size(); retained_bytes_ += bytes;
        status_.peak_bytes = std::max(status_.peak_bytes, retained_bytes_);
        status_.peak_messages = std::max(status_.peak_messages, retained_);
        (work.control ? control_queue_ : ordinary_queue_).push_back(std::move(work));
        changed_.notify_one(); return true;
    }
    void release(Work &work) {
        for (auto &item : work.items) {
            // Drop the worker's message reference before making its budget reusable.
            item.write = {};
            --retained_; retained_bytes_ -= item.bytes;
            auto &c{status_.topics[item.topic]}; --c.retained; c.retained_bytes -= item.bytes;
        }
    }
    void clearPending() {
        for (auto &work : control_queue_) { release(work); }
        for (auto &work : ordinary_queue_) { release(work); }
        control_queue_.clear(); ordinary_queue_.clear();
    }
    void fail(const std::string &reason) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (status_.phase != "failed") { status_.phase = "failed"; status_.reason = reason; }
        accepting_ = false; stopping_ = true; clearPending();
    }
    bool canTakeControl() const {
        if (control_queue_.empty()) { return false; }
        for (const auto &work : ordinary_queue_) {
            for (const auto &ordinary : work.items) {
                for (const auto &control : control_queue_.front().items) {
                    if (ordinary.topic == control.topic) { return false; }
                }
            }
        }
        return true;
    }
    std::pair<std::uint64_t, std::uint64_t> storageObservation() const {
        if (options_.storage_observation) { return options_.storage_observation(); }
        auto parent{std::filesystem::path{path_}.parent_path()};
        if (parent.empty()) { parent = "."; }
        std::uint64_t owned{};
        for (const auto &entry : std::filesystem::directory_iterator{parent}) {
            if (entry.path().filename().string().rfind("drone_rosbag_", 0) != 0
                || entry.is_symlink() || !entry.is_directory()) { continue; }
            for (const auto &file : std::filesystem::recursive_directory_iterator{entry.path()}) {
                if (!file.is_symlink() && file.is_regular_file()) { owned += file.file_size(); }
            }
        }
        return {owned, std::filesystem::space(parent).available};
    }
    bool checkStorage() {
        if (hook_) { hook_("storage"); }
        const auto observed{storageObservation()};
        if (observed.first < options_.max_total_bytes && observed.second >= options_.min_free_bytes) { return true; }
        std::lock_guard<std::mutex> lock{mutex_}; accepting_ = false; stopping_ = true; storage_limited_ = true;
        if (status_.phase != "failed") {
            status_.reason = "storage_limit"; status_.end_reason = "storage_limit";
        }
        status_.end_ns = systemNs();
        clearPending(); return false;
    }
    void writeSummary(const Status &status) {
        if (!owns_directory_) { return; }
        if (hook_) { hook_("summary"); }
        std::ofstream out{std::filesystem::path{path_} / "session.json"};
        out << "{\"version\":1,\"identity\":" << jsonString(std::filesystem::path{path_}.filename().string())
            << ",\"configuration\":" << options_.metadata_json << ",\"start_ns\":" << status.start_ns
            << ",\"end_ns\":" << status.end_ns << ",\"phase\":" << jsonString(status.phase)
            << ",\"reason\":" << jsonString(status.reason) << ",\"end_reason\":" << jsonString(status.end_reason)
            << ",\"peak_bytes\":" << status.peak_bytes << ",\"peak_messages\":" << status.peak_messages << ",\"topics\":{";
        bool first{true};
        for (const auto &entry : status.topics) {
            if (!first) { out << ','; } first = false; const auto &c{entry.second};
            out << jsonString(entry.first) << ":{\"offered\":" << c.attempted << ",\"policy_skipped\":" << c.policy_skipped
                << ",\"byte_limited\":" << c.byte_limited << ",\"oversized\":" << c.oversized
                << ",\"queue_dropped\":" << c.dropped << ",\"accepted\":" << c.accepted << ",\"written\":" << c.written
                << ",\"accepted_but_unwritten\":" << c.accepted - c.written << ",\"timestamp_fallback\":" << c.timestamp_fallback
                << ",\"fallback_zero\":" << c.fallback_reasons[1] << ",\"fallback_negative\":" << c.fallback_reasons[2]
                << ",\"fallback_nanoseconds\":" << c.fallback_reasons[3] << ",\"direct_offered\":" << c.direct
                << ",\"snapshots\":" << c.snapshots << ",\"middleware_lost\":" << c.middleware_lost
                << ",\"incompatible_qos\":" << c.incompatible_qos << ",\"observed\":" << (c.attempted ? "true" : "false") << '}';
        }
        out << "}}\n"; out.flush(); if (!out) { throw std::runtime_error("session summary write failed"); }
    }
    void work() noexcept {
        std::unique_ptr<rosbag2_cpp::Writer> writer{}; std::set<std::string> created{};
        try {
            const auto path{std::filesystem::path{path_}};
            if (!path.parent_path().empty()) { std::filesystem::create_directories(path.parent_path()); }
            if (!std::filesystem::create_directory(path)) { throw std::runtime_error("Recording directory already exists: " + path_); }
            owns_directory_ = true;
            if (hook_) { hook_("open"); }
            if (checkStorage()) {
                writer = std::make_unique<rosbag2_cpp::Writer>(
                    std::make_unique<GuardedWriter>([this] { return checkStorage(); }));
                rosbag2_storage::StorageOptions storage{};
                // The outer directory is exclusively ours. rosbag2 requires its own new URI.
                storage.uri = (path / "data").string(); storage.storage_id = "sqlite3";
                storage.max_bagfile_size = options_.max_file_bytes;
                writer->open(storage);
                std::lock_guard<std::mutex> lock{mutex_};
                if (status_.phase != "failed") { status_.phase = "recording"; }
            }
            std::size_t controls{}; auto checked{std::chrono::steady_clock::now()};
            while (writer) {
                Work item{};
                {
                    std::unique_lock<std::mutex> lock{mutex_};
                    changed_.wait_for(lock, std::chrono::milliseconds{100}, [this] {
                        return stopping_ || !control_queue_.empty() || !ordinary_queue_.empty();
                    });
                    if (control_queue_.empty() && ordinary_queue_.empty() && stopping_) { break; }
                    if (!control_queue_.empty() || !ordinary_queue_.empty()) {
                        const bool control{canTakeControl() && (ordinary_queue_.empty()
                            || controls + control_queue_.front().items.size() <= 8)};
                        auto &queue{control ? control_queue_ : ordinary_queue_};
                        item = std::move(queue.front()); queue.pop_front();
                        controls = control ? controls + item.items.size() : 0;
                    }
                }
                try {
                    const auto current{std::chrono::steady_clock::now()};
                    if (current - checked >= std::chrono::milliseconds{100}) {
                        checked = current;
                        if (!checkStorage()) { std::lock_guard<std::mutex> lock{mutex_}; release(item); break; }
                    }
                    for (const auto &entry : item.items) {
                        if (created.insert(entry.topic).second) {
                            writer->create_topic({entry.topic, entry.type, rmw_get_serialization_format(), ""});
                        }
                        if (hook_) { hook_("write"); }
                        entry.write(*writer);
                        std::lock_guard<std::mutex> lock{mutex_}; ++status_.topics[entry.topic].written;
                    }
                } catch (...) { std::lock_guard<std::mutex> lock{mutex_}; release(item); throw; }
                std::lock_guard<std::mutex> lock{mutex_}; release(item);
            }
        } catch (const StorageLimit &) {
            // The guard already sealed admission and retained the storage-limit reason.
        } catch (const std::exception &error) { fail(error.what()); }
        if (writer) {
            try { if (hook_) { hook_("close"); } writer->close(); }
            catch (const std::exception &error) { fail(error.what()); }
        }
        writer.reset();
        if (owns_directory_) {
            try {
                const auto data{std::filesystem::path{path_} / "data"};
                if (std::filesystem::is_directory(data)) {
                    for (const auto &file : std::filesystem::directory_iterator{data}) {
                        std::filesystem::rename(file.path(), std::filesystem::path{path_} / file.path().filename());
                    }
                    std::filesystem::remove(data);
                }
            } catch (const std::exception &error) { fail(error.what()); }
        }
        Status result{};
        {
            std::lock_guard<std::mutex> lock{mutex_}; accepting_ = false;
            if (!status_.end_ns) { status_.end_ns = systemNs(); }
            if (status_.phase != "failed") {
                status_.phase = storage_limited_ ? "storage_limited" : incomplete_ ? "incomplete" : "complete";
                if (incomplete_ && status_.reason.empty()) { status_.reason = "unexpected recorder gap"; }
            }
            result = status_;
        }
        try { writeSummary(result); }
        catch (const std::exception &error) { fail(error.what()); result = snapshot(); }
        try { if (report_) { report_(result); } } catch (...) { /* Reporting cannot kill a writer thread. */ }
    }
    std::string path_{}; Hook hook_{}; Report report_{}; Options options_{};
    std::map<std::string, recording::TopicDescriptor> descriptors_{};
    mutable std::mutex mutex_{}; std::condition_variable changed_{};
    std::deque<Work> control_queue_{}, ordinary_queue_{};
    std::size_t retained_{}, retained_bytes_{};
    bool accepting_{true}, stopping_{}, incomplete_{}, storage_limited_{}, owns_directory_{};
    Status status_{}; std::thread thread_{};
};
#endif // DRONE_TRACKER_BAG_SESSION_H
