#ifndef FRAME_RECORDING_RECEIPT_H
#define FRAME_RECORDING_RECEIPT_H

#include <cstdint>

/** @brief Queue disposition for one exact capture; acceptance does not prove file persistence. */
enum class RecordingDisposition : std::uint8_t { Unavailable, Rejected, Cancelled, Accepted };
/** @brief Portable request identity, registered synchronously before asynchronous preparation. */
struct RecordingRequest {
    std::uint64_t stream{}, round{}, generation{};
    bool matches(const RecordingRequest &other) const noexcept {
        return generation && stream == other.stream && round == other.round && generation == other.generation;
    }
};
/** @brief Immutable-by-publication metadata. Contains values only, with no borrowed driver storage. */
struct RecordingReceipt {
    RecordingDisposition disposition{RecordingDisposition::Unavailable};
    RecordingRequest request{};
    std::uint64_t capture_id{}, timestamp_us{};
    std::uint32_t driver_sequence{};
    bool acceptedFor(const RecordingRequest &token) const noexcept {
        return disposition == RecordingDisposition::Accepted && request.matches(token);
    }
};
/** @brief Executed algorithm input or historical initialization source, independent of diagnostics. */
struct AlgorithmSource {
    RecordingReceipt receipt{};
    std::uint64_t timestamp_us{}, capture_id{}, frame_id{}, update_id{}, request_id{}, generation{};
};
/** @brief At most two source records and counters with distinct observation points. */
struct RecordingStartupEvidence {
    RecordingRequest request{};
    AlgorithmSource first_yolo{}, first_initialization{};
    bool yolo_known{}, initialization_known{}, cancelled{}, fallback{};
    std::uint64_t preparation_omitted{}, dispatch_withheld{}, dispatch_rejected{}, pending_replaced{};
    std::int64_t on_mono_us{}, first_eligible_mono_us{}, initialization_mono_us{};
};
#endif // FRAME_RECORDING_RECEIPT_H
