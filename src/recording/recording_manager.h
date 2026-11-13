#ifndef DRONE_TRACKER_RECORDING_MANAGER_H
#define DRONE_TRACKER_RECORDING_MANAGER_H

#include "bag_session.h"
#include "config.h"

#include <atomic>
#include <optional>

namespace recording {
/** @brief Central owner of typed subscriptions, immutable session handles and bag policy. */
class RecordingManager final {
public:
    struct Session {
        Session(const Config &config, std::shared_ptr<BagSession> writer, std::uint64_t round)
            : bag{std::move(writer)}, round_id{round}, image_tokens{static_cast<double>(config.image_bytes_per_second),
                static_cast<double>(config.image_burst_bytes)} {}
        std::shared_ptr<BagSession> bag{};
        const std::uint64_t round_id{};
        std::mutex mutex{};
        bool sealed{};
        std::map<std::string, Sampler> samplers{};
        TokenBucket image_tokens;
    };
    using Handle = std::shared_ptr<Session>;
    RecordingManager(rclcpp::Node &node, Config config, BagSession::Hook hook, BagSession::Report report);
    ~RecordingManager() noexcept;
    void startExecutor();
    void stopExecutor() noexcept;
    void open(std::uint64_t round, std::int64_t start_ns);
    void seal(const std::string &reason = "session_end") noexcept;
    void close() noexcept;
    void observeTrigger(const std_msgs::msg::Header &message, std::int64_t receipt, bool defer_on = true);
    /** @brief Discard start commands canceled by a failed or terminated control decision. */
    void cancelPendingStart() noexcept;
    void submitObservationPair(std::uint64_t round, std::shared_ptr<vision_msgs::msg::Detection2D> bbox,
        std::shared_ptr<geometry_msgs::msg::TwistStamped> los, bool final_off);
    Handle capture() const;
    const Config &config() const { return config_; }

    /** @brief Submissions remain tied to the handle captured at callback entry. */
    template<typename Message>
    void submit(const Handle &handle, const TopicDescriptor &entry, std::shared_ptr<Message> message,
                std::int64_t receipt, std::int64_t monotonic, bool direct = false, bool snapshot = false) {
        if (!handle) { return; }
        std::lock_guard<std::mutex> lock{handle->mutex};
        if (handle->sealed) { return; }
        const auto stamp{messageStamp(*message, entry.timestamp, receipt)};
        handle->bag->note(entry.name, "offered", stamp.fallback, direct, snapshot);
        if (!handle->samplers[entry.id].select(entry.max_hz, monotonic)) {
            handle->bag->note(entry.name, "policy"); return;
        }
        const auto bytes{retainedPayload(*message)};
        const bool image{entry.id == "tracker_image"};
        if (image && bytes > config_.image_burst_bytes) { handle->bag->note(entry.name, "oversized"); return; }
        if (image && !handle->image_tokens.available(bytes, monotonic)) { handle->bag->note(entry.name, "bytes"); return; }
        if (handle->bag->submit(std::move(message), entry.name, rclcpp::Time{stamp.nanoseconds}, bytes)
            && image) { handle->image_tokens.consume(bytes); }
    }
private:
    template<typename Message> void subscribe(const TopicDescriptor &entry) {
        rclcpp::QoS qos{entry.depth};
        if (entry.delivery == Delivery::BestEffort) { qos.best_effort(); }
        if (entry.delivery == Delivery::Retained) { qos.reliable().transient_local(); }
        rclcpp::SubscriptionOptions options{}; options.callback_group = group_;
        options.event_callbacks.incompatible_qos_callback = [this, entry](rclcpp::QOSRequestedIncompatibleQoSInfo &info) {
            coverage(entry.name, true, info.total_count_change);
            RCLCPP_WARN(node_.get_logger(), "Bag incompatible QoS: %s", entry.name.c_str());
        };
        options.event_callbacks.message_lost_callback = [this, entry](rclcpp::QOSMessageLostInfo &info) {
            coverage(entry.name, false, info.total_count_change);
        };
        subscriptions_.push_back(node_.create_subscription<Message>(entry.name, qos,
            [this, entry](std::shared_ptr<Message> message) {
                const auto handle{capture()};
                const auto receipt{system_clock_.now().nanoseconds()}; const auto monotonic{monotonicNs()};
                if constexpr (std::is_same_v<Message, std_msgs::msg::String>) {
                    if (entry.id == "gimbal_mode") { cacheMode(message, receipt); }
                }
                submit(handle, entry, std::move(message), receipt, monotonic);
            }, options));
    }
    void cacheMode(std::shared_ptr<std_msgs::msg::String> message, std::int64_t receipt);
    void coverage(const std::string &topic, bool incompatible, std::uint64_t count);
    const TopicDescriptor &entry(const std::string &id) const;
    std::string metadata(std::uint64_t round, std::int64_t start_ns, bool has_trigger) const;
    static std::int64_t monotonicNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    struct PendingTrigger { std_msgs::msg::Header message{}; std::int64_t receipt{}; std::uint64_t occurrence{}; };
    rclcpp::Node &node_;
    const Config config_;
    BagSession::Hook hook_{}; BagSession::Report report_{};
    rclcpp::Clock system_clock_{RCL_SYSTEM_TIME};
    rclcpp::CallbackGroup::SharedPtr group_{};
    std::vector<rclcpp::SubscriptionBase::SharedPtr> subscriptions_{};
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_{};
    std::thread executor_thread_{};
    std::atomic_bool executor_running_{};
    mutable std::mutex mutex_{};
    Handle active_{}, closing_{};
    std::deque<PendingTrigger> pending_on_{};
    std::uint64_t occurrence_{}, pending_overflow_{}, cache_oversized_{};
    std::shared_ptr<std_msgs::msg::String> cached_mode_{};
    std::int64_t mode_receipt_{};
    std::string run_id_{};
    std::map<std::string, std::uint64_t> incompatible_{};
};
} // namespace recording
#endif // DRONE_TRACKER_RECORDING_MANAGER_H
