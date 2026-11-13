#include "recording_manager.h"

#include <filesystem>
#include <random>
#include <sstream>

namespace recording {
RecordingManager::RecordingManager(rclcpp::Node &node, Config config, BagSession::Hook hook, BagSession::Report report)
    : node_{node}, config_{std::move(config)}, hook_{std::move(hook)}, report_{std::move(report)} {
    std::random_device random{};
    run_id_ = std::to_string(system_clock_.now().nanoseconds()) + "_" + std::to_string(random());
    cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [&] {
        group_ = node_.create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive, false);
        for (const auto &descriptor : config_.topics) {
            if (descriptor.source != SourceKind::Subscription) { continue; }
#define SUBSCRIBE_TOPIC(stable_id, package, type, file, name, time, source, hz, protect, qos, depth) \
            if (descriptor.id == #stable_id) { subscribe<package::msg::type>(descriptor); continue; }
            DRONE_RECORDING_TOPICS(SUBSCRIBE_TOPIC)
#undef SUBSCRIBE_TOPIC
        }
    });
}
RecordingManager::~RecordingManager() noexcept { seal("service_exit"); stopExecutor(); close(); }
void RecordingManager::startExecutor() {
    if (executor_thread_.joinable()) { return; }
    cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [this] {
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_callback_group(group_, node_.get_node_base_interface());
    });
    executor_running_ = true;
    try {
        executor_thread_ = cpu_affinity::start(cpu_affinity::topicRecordingRole(cpu_affinity::Role::RosTransport), [this] {
            try { while (executor_running_) { executor_->spin_once(std::chrono::milliseconds{50}); } }
            catch (const std::exception &error) {
                RCLCPP_ERROR(node_.get_logger(), "Recording executor failed: %s", error.what());
                if (auto session = capture()) { session->bag->abort(error.what()); }
                seal("executor_failure");
            }
        });
    } catch (...) {
        const auto failure{std::current_exception()};
        executor_running_ = false;
        try {
            cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [this] {
                executor_->remove_callback_group(group_);
                executor_.reset();
            });
        } catch (const std::exception &error) {
            RCLCPP_ERROR(node_.get_logger(), "Recorder executor cleanup failed: %s", error.what());
        }
        std::rethrow_exception(failure);
    }
}
void RecordingManager::stopExecutor() noexcept {
    executor_running_ = false;
    if (executor_) { executor_->cancel(); }
    if (executor_thread_.joinable()) { executor_thread_.join(); }
    try {
        cpu_affinity::withRole(cpu_affinity::Role::RosTransport, [this] {
            if (executor_) { executor_->remove_callback_group(group_); }
            subscriptions_.clear(); executor_.reset(); group_.reset();
        });
    } catch (const std::exception &error) { RCLCPP_ERROR(node_.get_logger(), "Recorder transport cleanup: %s", error.what()); }
}
RecordingManager::Handle RecordingManager::capture() const {
    std::lock_guard<std::mutex> lock{mutex_}; return active_;
}
const TopicDescriptor &RecordingManager::entry(const std::string &id) const {
    for (const auto &value : config_.topics) { if (value.id == id) { return value; } }
    throw std::logic_error("Missing compiled bag entry: " + id);
}
void RecordingManager::coverage(const std::string &topic, bool incompatible, std::uint64_t count) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (incompatible) { incompatible_[topic] += count; }
    if (active_) { active_->bag->coverage(topic, incompatible, count); }
}
void RecordingManager::cacheMode(std::shared_ptr<std_msgs::msg::String> message, std::int64_t receipt) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (retainedPayload(*message) > 65536) {
        ++cache_oversized_; cached_mode_.reset();
        RCLCPP_WARN(node_.get_logger(), "Bag retained mode exceeds 64 KiB cache"); return;
    }
    cached_mode_ = std::move(message); mode_receipt_ = receipt;
}
std::string RecordingManager::metadata(std::uint64_t round, std::int64_t start_ns, bool has_trigger) const {
    std::ostringstream out{};
    out << "{\"source_revision\":" << BagSession::jsonString(config_.source_revision)
        << ",\"program_sha256\":" << BagSession::jsonString(config_.program_sha256)
        << ",\"fingerprint\":" << BagSession::jsonString(config_.fingerprint)
        << ",\"run_id\":" << BagSession::jsonString(run_id_) << ",\"round_id\":" << round
        << ",\"start_ros_ns\":" << start_ns << ",\"programmatic_start\":" << (has_trigger ? "false" : "true")
        << ",\"mode_snapshot\":" << (cached_mode_ ? "true" : "false") << ",\"mode_receipt_ns\":" << mode_receipt_
        << ",\"cache_oversized\":" << cache_oversized_ << ",\"pending_trigger_overflow\":" << pending_overflow_
        << ",\"max_messages\":256,\"max_payload_bytes\":67108864"
        << ",\"reserved_messages\":16,\"reserved_payload_bytes\":1048576,\"cache_limit_bytes\":65536"
        << ",\"max_file_bytes\":" << config_.max_file_bytes << ",\"max_total_bytes\":" << config_.max_total_bytes
        << ",\"min_free_bytes\":" << config_.min_free_bytes << ",\"image_bytes_per_second\":" << config_.image_bytes_per_second
        << ",\"image_burst_bytes\":" << config_.image_burst_bytes << ",\"catalog\":[";
    bool first{true};
    for (const auto &value : config_.topics) {
        if (!first) { out << ','; } first = false;
        out << "{\"id\":" << BagSession::jsonString(value.id) << ",\"name\":" << BagSession::jsonString(value.name)
            << ",\"type\":" << BagSession::jsonString(value.type) << ",\"timestamp_rule\":"
            << BagSession::jsonString(value.timestamp == TimestampRule::Header ? "H" : value.timestamp == TimestampRule::Stamp ? "S" : "R")
            << ",\"source_kind\":" << BagSession::jsonString(value.source == SourceKind::Subscription ? "subscription"
                : value.source == SourceKind::Pair ? "direct_pair" : "direct_trigger")
            << ",\"delivery\":" << BagSession::jsonString(value.delivery == Delivery::BestEffort ? "best_effort"
                : value.delivery == Delivery::Retained ? "reliable_transient_local"
                : value.delivery == Delivery::Direct ? "direct" : "reliable_volatile")
            << ",\"depth\":" << value.depth << ",\"max_hz\":" << value.max_hz
            << ",\"max_messages\":" << value.max_messages << ",\"max_bytes\":" << value.max_bytes << '}';
    }
    out << "]}"; return out.str();
}
void RecordingManager::open(std::uint64_t round, std::int64_t start_ns) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (active_ || closing_) { throw std::logic_error("Previous recorder session is still active/closing"); }
    BagSession::Options options{}; options.catalog = config_.topics;
    options.max_file_bytes = config_.max_file_bytes; options.max_total_bytes = config_.max_total_bytes;
    options.min_free_bytes = config_.min_free_bytes;
    options.metadata_json = metadata(round, start_ns, !pending_on_.empty());
    const auto path{std::filesystem::path{config_.directory} / ("drone_rosbag_" + run_id_ + "_" + std::to_string(round))};
    auto bag{std::make_shared<BagSession>(path.string(), hook_, report_, std::move(options))};
    auto session{std::make_shared<Session>(config_, std::move(bag), round)};
    for (const auto &issue : incompatible_) { session->bag->coverage(issue.first, true, issue.second); }
    for (const auto &trigger : pending_on_) {
        submit(session, entry("trigger"), std::make_shared<std_msgs::msg::Header>(trigger.message),
            trigger.receipt, monotonicNs(), true);
    }
    session->bag->note(entry("trigger").name, "pending_drop", StampFallback::None, true, false, pending_overflow_);
    pending_overflow_ = 0;
    pending_on_.clear();
    if (cached_mode_) { submit(session, entry("gimbal_mode"), cached_mode_, start_ns, monotonicNs(), false, true); }
    active_ = std::move(session);
}
void RecordingManager::observeTrigger(const std_msgs::msg::Header &message, std::int64_t receipt, bool defer_on) {
    Handle handle{};
    {
        std::lock_guard<std::mutex> lock{mutex_}; ++occurrence_; handle = active_;
        if (!handle) {
            if (message.frame_id == "ON" && defer_on) {
                if (pending_on_.size() < 16) { pending_on_.push_back({message, receipt, occurrence_}); }
                else { ++pending_overflow_; }
            } else if (message.frame_id == "OFF") { pending_on_.clear(); pending_overflow_ = 0; }
            return;
        }
    }
    submit(handle, entry("trigger"), std::make_shared<std_msgs::msg::Header>(message), receipt, monotonicNs(), true);
}
void RecordingManager::cancelPendingStart() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    pending_on_.clear();
    pending_overflow_ = 0;
}
void RecordingManager::submitObservationPair(std::uint64_t round,
    std::shared_ptr<vision_msgs::msg::Detection2D> bbox,
    std::shared_ptr<geometry_msgs::msg::TwistStamped> los, bool final_off) {
    const auto handle{capture()}; if (!handle || handle->round_id != round) { return; }
    std::lock_guard<std::mutex> lock{handle->mutex}; if (handle->sealed) { return; }
    const auto &first{entry("target_bbox")}; const auto &second{entry("target_los")};
    handle->bag->note(first.name, "offered", StampFallback::None, true);
    handle->bag->note(second.name, "offered", StampFallback::None, true);
    if (!handle->samplers["observations"].select(config_.observations_max_hz, monotonicNs(), final_off)) {
        handle->bag->note(first.name, "policy"); handle->bag->note(second.name, "policy"); return;
    }
    const auto receipt{system_clock_.now().nanoseconds()}; const auto stamp{selectStamp(bbox->header.stamp, receipt)};
    handle->bag->note(first.name, "stamp", stamp.fallback); handle->bag->note(second.name, "stamp", stamp.fallback);
    handle->bag->submitPair(bbox, los, rclcpp::Time{stamp.nanoseconds}, retainedPayload(*bbox), retainedPayload(*los),
        first.name, second.name, final_off);
}
void RecordingManager::seal(const std::string &reason) noexcept {
    std::lock_guard<std::mutex> lock{mutex_}; if (!active_) { return; }
    std::lock_guard<std::mutex> session_lock{active_->mutex}; active_->sealed = true;
    active_->bag->seal(reason); closing_ = std::move(active_);
}
void RecordingManager::close() noexcept {
    seal(); Handle closing{};
    { std::lock_guard<std::mutex> lock{mutex_}; closing = std::move(closing_); }
    if (closing) { closing->bag->stop(); }
}
} // namespace recording
