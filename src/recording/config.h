#ifndef DRONE_TRACKER_RECORDING_CONFIG_H
#define DRONE_TRACKER_RECORDING_CONFIG_H

#include "topic_catalog.h"

#include <cstdint>
#include <map>
#include <string>

#include <rclcpp/rclcpp.hpp>

namespace recording {
/** @brief Startup settings copied once; runtime parameters cannot alter a session. */
struct Config {
    std::vector<TopicDescriptor> topics{};
    double observations_max_hz{};
    std::uint64_t image_bytes_per_second{16U * 1024U * 1024U}, image_burst_bytes{16U * 1024U * 1024U};
    std::uint64_t max_file_bytes{268435456}, max_total_bytes{5368709120ULL}, min_free_bytes{5368709120ULL};
    std::string directory{}, fingerprint{}, source_revision{}, program_sha256{};
};

/** @brief Declare the complete startup namespace without creating ROS subscriptions. */
inline void declareParameters(rclcpp::Node &node) {
    node.declare_parameter<bool>("bag.bench_profile", false);
    node.declare_parameter<double>("bag.observations.max_hz", 0.0);
    node.declare_parameter<std::int64_t>("bag.image.bytes_per_second", 16U * 1024U * 1024U);
    node.declare_parameter<std::int64_t>("bag.image.burst_bytes", 16U * 1024U * 1024U);
    node.declare_parameter<std::int64_t>("bag.max_file_bytes", 268435456);
    node.declare_parameter<std::int64_t>("bag.max_total_bytes", 5368709120LL);
    node.declare_parameter<std::int64_t>("bag.min_free_bytes", 5368709120LL);
    for (const auto &entry : topicCatalog(true)) {
        const auto prefix{"bag.topics." + entry.id + "."};
        node.declare_parameter<std::string>(prefix + "name", entry.name);
        if (entry.source != SourceKind::Pair) { node.declare_parameter<double>(prefix + "max_hz", entry.max_hz); }
        node.declare_parameter<std::int64_t>(prefix + "max_messages", entry.max_messages);
        node.declare_parameter<std::int64_t>(prefix + "max_bytes", entry.max_bytes);
    }
}

/** @brief Resolve names through this node's namespace/remappings and reject conflicting policy. */
Config readConfig(rclcpp::Node &node);
} // namespace recording
#endif // DRONE_TRACKER_RECORDING_CONFIG_H
