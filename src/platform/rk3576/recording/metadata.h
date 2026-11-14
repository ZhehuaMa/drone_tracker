#ifndef RK3576_RECORDING_METADATA_H
#define RK3576_RECORDING_METADATA_H

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include "shared_slots.h"

namespace rk3576::recording {
/** @brief 只生成 JSON 字符串，转义控制字符以保持一行一个完整记录。 */
inline std::string quote(const std::string &value) {
    std::ostringstream out{};
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out << '\\' << c; }
        else if (c < 32) { out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned{c}; }
        else { out << c; }
    }
    out << '"';
    return out.str();
}
inline std::string identityFields(const RecordingSlots::Identity &id) {
    return "\"round\":" + std::to_string(id.round) + ",\"sequence\":" + std::to_string(id.sequence)
        + ",\"capture_id\":" + std::to_string(id.capture_id)
        + ",\"stream_id\":" + std::to_string(id.stream_id)
        + ",\"tracking_round\":" + std::to_string(id.tracking_round)
        + ",\"request_generation\":" + std::to_string(id.request_generation)
        + ",\"driver_seconds\":" + std::to_string(id.driver_seconds)
        + ",\"driver_microseconds\":" + std::to_string(id.driver_microseconds)
        + ",\"flags\":" + std::to_string(id.flags) + ",\"timestamp_us\":" + std::to_string(id.timestamp_us);
}
inline std::string sourceSummary(const AlgorithmSource &source) {
    const auto &r{source.receipt};
    return "{\"stream_id\":" + std::to_string(r.request.stream)
        + ",\"tracking_round\":" + std::to_string(r.request.round)
        + ",\"request_generation\":" + std::to_string(r.request.generation)
        + ",\"capture_id\":" + std::to_string(source.capture_id)
        + ",\"timestamp_us\":" + std::to_string(source.timestamp_us)
        + ",\"sequence\":" + std::to_string(r.driver_sequence)
        + ",\"request_id\":" + std::to_string(source.request_id)
        + ",\"update_id\":" + std::to_string(source.update_id)
        + ",\"algorithm_generation\":" + std::to_string(source.generation)
        + ",\"accepted\":" + (r.disposition == RecordingDisposition::Accepted ? "true" : "false") + "}";
}
inline std::string startupSummary(const RecordingStartupEvidence &e) {
    return "{\"stream_id\":" + std::to_string(e.request.stream)
        + ",\"tracking_round\":" + std::to_string(e.request.round)
        + ",\"request_generation\":" + std::to_string(e.request.generation)
        + ",\"outcome\":" + quote(e.fallback ? "unavailable" : e.cancelled && !e.yolo_known ? "cancelled" : "accepted")
        + ",\"initialization_outcome\":" + quote(e.initialization_known ? e.fallback ? "unavailable" : "accepted" : "not-applicable")
        + ",\"cancelled\":" + (e.cancelled ? "true" : "false")
        + ",\"fallback\":" + (e.fallback ? "true" : "false")
        + ",\"first_yolo\":" + (e.yolo_known ? sourceSummary(e.first_yolo) : "null")
        + ",\"first_initialization\":" + (e.initialization_known ? sourceSummary(e.first_initialization) : "null")
        + ",\"preparation_omitted\":" + std::to_string(e.preparation_omitted)
        + ",\"dispatch_withheld\":" + std::to_string(e.dispatch_withheld)
        + ",\"dispatch_rejected\":" + std::to_string(e.dispatch_rejected)
        + ",\"pending_replaced\":" + std::to_string(e.pending_replaced)
        + ",\"on_mono_us\":" + std::to_string(e.on_mono_us)
        + ",\"first_eligible_mono_us\":" + std::to_string(e.first_eligible_mono_us)
        + ",\"initialization_mono_us\":" + std::to_string(e.initialization_mono_us) + "}";
}
inline std::string cameraSummary(const FrameSourceStatus &status) {
    const auto &c{status.camera};
    std::ostringstream out{};
    out << "{\"reason\":" << quote(status.reason) << ",\"failed\":"
        << (status.state == FrameSourceState::Failed ? "true" : "false")
        << ",\"phase\":" << quote(c.phase) << ",\"warmup_reason\":" << quote(c.reason)
        << ",\"dequeued\":" << c.dequeued_total << ",\"warmup_discarded\":" << c.warmup_discarded
        << ",\"terminal_discarded\":" << c.terminal_discarded << ",\"qualified\":" << status.read
        << ",\"tracking_enqueued\":" << status.enqueued << ",\"tracking_dropped\":" << status.dropped
        << ",\"sequence_gaps\":" << status.sequence_gaps << ",\"time_source\":" << quote(status.time_source)
        << ",\"warmup_examined\":" << c.warmup_examined << ",\"warmup_repeats\":" << c.warmup_repeats
        << ",\"warmup_us\":" << c.warmup_us << ",\"warmup_limit_us\":2000000"
        << ",\"stream_started_monotonic_us\":"
        << (c.stream_started_monotonic_us ? std::to_string(c.stream_started_monotonic_us) : "null")
        << ",\"warmup_max_samples\":120,\"warmup_required_streak\":8"
        << ",\"first_sequence\":" << (c.first_frame_known ? std::to_string(c.first_sequence) : "null")
        << ",\"first_timestamp_us\":" << (c.first_frame_known ? std::to_string(c.first_timestamp_us) : "null")
        << ",\"first_latency_us\":" << (c.first_frame_known ? std::to_string(c.first_latency_us) : "null")
        << ",\"observations\":[";
    bool first{true};
    for (const auto &e : c.evidence) {
        if (!first) { out << ','; }
        first = false;
        out << "{\"seconds\":" << e.seconds << ",\"microseconds\":" << e.microseconds
            << ",\"sequence\":" << e.sequence << ",\"flags\":" << e.flags
            << ",\"dequeue_us\":" << e.dequeue_us << ",\"streak\":" << e.streak
            << ",\"decision\":" << quote(e.decision) << '}';
    }
    out << "]}";
    if (out.str().size() >= 65536) {
        throw std::runtime_error("Camera summary exceeds recording metadata bound");
    }
    return out.str();
}
/** @brief Session window snapshots retain the actual running camera state and stream warmup. */
inline std::string cameraSummary(const FrameSourceStatus &end, const FrameSourceStatus &start,
                                std::uint64_t stream, std::uint64_t tracking_round,
                                const std::string &reason) {
    auto text{cameraSummary(end)};
    text.pop_back();
    text += ",\"stream_id\":" + std::to_string(stream)
        + ",\"tracking_round\":" + std::to_string(tracking_round)
        + ",\"end_reason\":" + quote(reason)
        + ",\"camera_running_at_end\":" + (end.state == FrameSourceState::Running ? "true" : "false")
        + ",\"start_dequeued\":" + std::to_string(start.camera.dequeued_total)
        + ",\"end_dequeued\":" + std::to_string(end.camera.dequeued_total)
        + ",\"window_dequeued\":" + std::to_string(end.camera.dequeued_total - start.camera.dequeued_total)
        + ",\"start_qualified\":" + std::to_string(start.read)
        + ",\"end_qualified\":" + std::to_string(end.read)
        + ",\"window_qualified\":" + std::to_string(end.read - start.read)
        + ",\"warmup_from_stream\":" + (stream ? "true" : "false") + "}";
    return text;
}
} // namespace rk3576::recording
#endif // RK3576_RECORDING_METADATA_H
