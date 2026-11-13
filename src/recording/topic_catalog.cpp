#include "topic_catalog.h"

namespace recording {
std::vector<TopicDescriptor> topicCatalog(bool bench) {
    std::vector<TopicDescriptor> result{};
#define APPEND_TOPIC(id, package, type, file, name, time, source, hz, protect, qos, depth) \
    result.push_back({#id, name, #package "/msg/" #type, TimestampRule::time, SourceKind::source, \
        hz, protect, Delivery::qos, depth});
    DRONE_RECORDING_TOPICS(APPEND_TOPIC)
#undef APPEND_TOPIC
    if (!bench) { result.pop_back(); }
    for (auto &entry : result) {
        if (entry.id == "tracker_image") { entry.max_messages = 4; entry.max_bytes = 32U * 1024U * 1024U; }
    }
    return result;
}
} // namespace recording
