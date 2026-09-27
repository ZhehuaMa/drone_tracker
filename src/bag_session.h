#ifndef DRONE_TRACKER_BAG_SESSION_H
#define DRONE_TRACKER_BAG_SESSION_H

#include <rosbag2_cpp/writer.hpp>
#include <rmw/rmw.h>
#include <rclcpp/time.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>

/** @brief 一轮 ROS bag；生产方只入队，工作线程独占 Writer。 */
class BagSession final {
public:
    struct TopicCounts {
        std::uint64_t attempted{}, accepted{}, written{}, dropped{};
    };
    struct Status {
        std::string phase{"opening"};
        std::string reason{};
        std::map<std::string, TopicCounts> topics{};
        std::size_t queued{}, queued_bytes{}, peak_bytes{};
    };
    using Hook = std::function<void(const char *)>;
    using Report = std::function<void(const Status &)>;

    BagSession(std::string path, bool images, Hook hook, Report report)
        : path_{std::move(path)}, images_{images}, hook_{std::move(hook)}, report_{std::move(report)} {
        thread_ = std::thread{[this] { work(); }};
    }
    BagSession(const BagSession &) = delete;
    BagSession &operator=(const BagSession &) = delete;
    ~BagSession() noexcept { stop(); }

    template<typename Message>
    bool submit(std::shared_ptr<Message> message, const std::string &topic,
                const rclcpp::Time &stamp, std::size_t bytes = sizeof(Message)) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!accepting_) { return false; }
        auto &counts{status_.topics[topic]};
        ++counts.attempted;
        if (queue_.size() >= max_messages || bytes > max_bytes - queued_bytes_) {
            ++counts.dropped;
            incomplete_ = true;
            return false;
        }
        queue_.push_back({topic, bytes, [message = std::move(message), topic, stamp](rosbag2_cpp::Writer &writer) {
            writer.write(*message, topic, stamp);
        }});
        queued_bytes_ += bytes;
        ++counts.accepted;
        status_.peak_bytes = std::max(status_.peak_bytes, queued_bytes_);
        changed_.notify_one();
        return true;
    }

    Status snapshot() const {
        std::lock_guard<std::mutex> lock{mutex_};
        auto result{status_};
        result.queued = queue_.size();
        result.queued_bytes = queued_bytes_;
        if (incomplete_ && (result.phase == "opening" || result.phase == "recording")) {
            result.phase = "incomplete";
        }
        return result;
    }

    void seal() noexcept {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            accepting_ = false;
            stopping_ = true;
        }
        changed_.notify_one();
    }

    void stop() noexcept {
        seal();
        if (thread_.joinable()) { thread_.join(); }
    }

    static constexpr std::size_t max_messages{256};
    static constexpr std::size_t max_bytes{64U * 1024U * 1024U};

private:
    struct Entry {
        std::string topic;
        std::size_t bytes;
        std::function<void(rosbag2_cpp::Writer &)> write;
    };

    void fail(const std::string &reason) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (status_.phase != "failed") {
            status_.phase = "failed";
            status_.reason = reason;
        }
        accepting_ = false;
        queue_.clear();
        queued_bytes_ = 0;
    }

    void work() noexcept {
        std::unique_ptr<rosbag2_cpp::Writer> writer{};
        try {
            if (std::filesystem::exists(path_)) { throw std::runtime_error("Recording directory already exists: " + path_); }
            if (hook_) { hook_("open"); }
            writer = std::make_unique<rosbag2_cpp::Writer>();
            writer->open(path_);
            for (const auto &topic : std::map<std::string, std::string>{
                     {"/fpv/gimbal", "sensor_msgs/msg/Imu"}, {"/fpv/odom", "nav_msgs/msg/Odometry"},
                     {"/fpv/target_bbox", "vision_msgs/msg/Detection2D"}}) {
                writer->create_topic({topic.first, topic.second, rmw_get_serialization_format(), ""});
            }
            if (images_) {
                writer->create_topic({"/fpv/tracker_image", "sensor_msgs/msg/Image", rmw_get_serialization_format(), ""});
            }
            {
                std::lock_guard<std::mutex> lock{mutex_};
                status_.phase = "recording";
            }
            for (;;) {
                Entry entry{};
                {
                    std::unique_lock<std::mutex> lock{mutex_};
                    changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                    if (queue_.empty()) { break; }
                    entry = std::move(queue_.front());
                    queue_.pop_front();
                    queued_bytes_ -= entry.bytes;
                }
                if (hook_) { hook_("write"); }
                entry.write(*writer);
                {
                    std::lock_guard<std::mutex> lock{mutex_};
                    ++status_.topics[entry.topic].written;
                }
            }
        } catch (const std::exception &error) {
            fail(error.what());
        }
        if (writer) {
            try {
                if (hook_) { hook_("close"); }
                writer->close();
            } catch (const std::exception &error) {
                fail(error.what());
            }
        }
        writer.reset();
        Status result{};
        {
            std::lock_guard<std::mutex> lock{mutex_};
            accepting_ = false;
            if (status_.phase != "failed") {
                status_.phase = incomplete_ ? "incomplete" : "complete";
                for (const auto &topic : status_.topics) {
                    if (topic.second.accepted != topic.second.written) { status_.phase = "incomplete"; }
                }
                if (status_.phase == "incomplete") { status_.reason = "queue overflow or accepted messages not written"; }
            }
            result = status_;
        }
        try {
            if (report_) { report_(result); }
        } catch (...) {
            // 状态回调不可让文件线程异常终止；Writer 已关闭，节点仍可安全退出。
        }
    }

    std::string path_;
    bool images_{};
    Hook hook_{};
    Report report_{};
    mutable std::mutex mutex_{};
    std::condition_variable changed_{};
    std::deque<Entry> queue_{};
    std::size_t queued_bytes_{};
    bool accepting_{true}, stopping_{}, incomplete_{};
    Status status_{};
    std::thread thread_{};
};

#endif
