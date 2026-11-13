#include "config.h"
#include "platform/file_identity.h"

#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

#include <openssl/sha.h>

namespace recording {
namespace {
std::uint64_t positive(rclcpp::Node &node, const std::string &name) {
    const auto value{node.get_parameter(name).as_int()};
    if (value <= 0) { throw std::invalid_argument(name + " must be positive"); }
    return static_cast<std::uint64_t>(value);
}
void validRate(double value, const std::string &name) {
    if (!std::isfinite(value) || value < 0 || value > 1e9) {
        throw std::invalid_argument(name + " must be finite and between 0 and 1e9 Hz");
    }
}
} // namespace
Config readConfig(rclcpp::Node &node) {
    Config result{};
    result.program_sha256 = recordingFileIdentity("/proc/self/exe");
    result.directory = node.get_parameter("save_path").as_string();
    if (result.directory.empty()) { throw std::invalid_argument("save_path is empty"); }
    result.topics = topicCatalog(node.get_parameter("bag.bench_profile").as_bool());
    result.observations_max_hz = node.get_parameter("bag.observations.max_hz").as_double();
    validRate(result.observations_max_hz, "bag.observations.max_hz");
    result.image_bytes_per_second = positive(node, "bag.image.bytes_per_second");
    result.image_burst_bytes = positive(node, "bag.image.burst_bytes");
    result.max_file_bytes = positive(node, "bag.max_file_bytes");
    result.max_total_bytes = positive(node, "bag.max_total_bytes");
    result.min_free_bytes = positive(node, "bag.min_free_bytes");
    if (result.max_file_bytes < 86016) { throw std::invalid_argument("bag.max_file_bytes is below SQLite minimum"); }
    std::map<std::string, std::string> names{};
    for (auto &entry : result.topics) {
        const auto prefix{"bag.topics." + entry.id + "."};
        const auto default_name{entry.name};
        entry.name = node.get_node_topics_interface()->resolve_topic_name(node.get_parameter(prefix + "name").as_string());
        if (entry.source != SourceKind::Subscription && entry.name
            != node.get_node_topics_interface()->resolve_topic_name(default_name)) {
            throw std::invalid_argument(prefix + "name must match the actual direct business endpoint");
        }
        if (!names.emplace(entry.name, entry.id).second) {
            throw std::invalid_argument("Conflicting bag names: " + names.at(entry.name) + " and " + entry.id);
        }
        if (entry.source == SourceKind::Pair) { entry.max_hz = result.observations_max_hz; }
        else { entry.max_hz = node.get_parameter(prefix + "max_hz").as_double(); }
        validRate(entry.max_hz, prefix + "max_hz");
        if (entry.protected_event && entry.max_hz != 0) { throw std::invalid_argument(prefix + "max_hz protects events; use 0"); }
        const auto messages{positive(node, prefix + "max_messages")};
        const auto bytes{positive(node, prefix + "max_bytes")};
        if (messages > entry.max_messages || bytes > entry.max_bytes) {
            throw std::invalid_argument(prefix + "retention limits may only lower compiled defaults");
        }
        entry.max_messages = messages; entry.max_bytes = bytes;
    }
    // Unknown pair-specific frequency overrides must not silently bypass the joint policy.
    for (const auto &parameter : node.get_node_parameters_interface()->get_parameter_overrides()) {
        if (parameter.first == "bag.topics.target_bbox.max_hz" || parameter.first == "bag.topics.target_los.max_hz") {
            throw std::invalid_argument("Use bag.observations.max_hz for both observation members");
        }
    }
    std::ostringstream frozen{};
    frozen << std::setprecision(17) << result.directory << '\n' << result.observations_max_hz << '\n'
        << result.image_bytes_per_second << '\n' << result.image_burst_bytes << '\n' << result.max_file_bytes
        << '\n' << result.max_total_bytes << '\n' << result.min_free_bytes << '\n';
    for (const auto &entry : result.topics) {
        frozen << entry.id << '\n' << entry.name << '\n' << entry.type << '\n' << entry.max_hz << '\n'
            << entry.max_messages << '\n' << entry.max_bytes << '\n' << static_cast<int>(entry.delivery)
            << '\n' << entry.depth << '\n' << static_cast<int>(entry.timestamp) << '\n';
    }
    const auto text{frozen.str()};
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    SHA256(reinterpret_cast<const unsigned char *>(text.data()), text.size(), digest);
    std::ostringstream hex{};
    for (unsigned char byte : digest) { hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte); }
    result.fingerprint = hex.str();
#ifdef DRONE_RECORDING_SOURCE_REVISION
    result.source_revision = DRONE_RECORDING_SOURCE_REVISION;
#else
    result.source_revision = "unknown";
#endif
    return result;
}
} // namespace recording
