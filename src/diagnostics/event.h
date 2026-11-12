#ifndef DRONE_DIAGNOSTICS_EVENT_H
#define DRONE_DIAGNOSTICS_EVENT_H

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace diagnostics {
inline constexpr std::uint16_t FORMAT_VERSION{4};
enum class Mode : std::uint8_t { Off, Basic, Detail };
enum class Producer : std::uint32_t { Node, Camera, Yolo, Control, Dispatcher, Count };
enum class Type : std::uint16_t {
    RunStart,
    RunEnd,
    RoundStart,
    RoundEnd,
    Capture,
    FrameSkip,
    FrameSelect,
    UpdateStart,
    UpdateEnd,
    HistoryAdd,
    HistoryEvict,
    HistoryLookup,
    RequestSkipped,
    RequestDecision,
    DetectStart,
    DetectStage,
    DetectEnd,
    ResultRead,
    ResultOverwrite,
    RequestCancel,
    Decision,
    InitializeStart,
    InitializeEnd,
    CatchupStep,
    CatchupEnd,
    Output,
    PublishStart,
    PublishEnd,
    PublishSkipped,
    Timing,
    Gap,
    AbnormalEnd,
    ClockRelation,
    Health,
    JumpComparison,
    SourceContext,
    ObservationBbox,
    ObservationLos,
    LosPublishStart,
    LosPublishEnd,
    ObservationSuppressed,
    ControlBboxStart,
    ControlBboxEnd,
    ControlLosStart,
    ControlLosEnd,
    ValidityState,
    EvidenceClock,
    CandidateMatch,
    TrustLoss,
    TrustLossDisposed,
    ValidAdmission,
    PlanOutcome,
    StageDisposition,
    DetectorControl,
    SchedulerTiming,
    CleanupPolicy,
    RegionHint,
    PlanCancelled
};
enum class Reason : std::uint16_t {
    None,
    Unknown,
    InvalidInput,
    RepeatedTime,
    Stopped,
    Cleaning,
    PendingOccupied,
    UnreadResult,
    BusyPending,
    Interval,
    Warmup,
    Termination,
    DriverGap,
    QueueFull,
    LatestSkip,
    NoTarget,
    LowIou,
    YoloJump,
    NanoJump,
    NoResultTimeout,
    HistoryMiss,
    InitFailed,
    ImagePrepareFailed,
    InferenceException,
    PublishException,
    NotConsumed,
    BufferFull,
    StorageError,
    SyncError,
    SpaceProtection,
    Incomplete,
    NanoUpdateFailed,
    Superseded,
    ContextUnavailable,
    RecoveryUnavailable,
    PreviousRound,
    Disposed,
    InvalidClock,
    ActiveReplacement,
    PartialCoverage,
    CandidateExpired,
    StaleEvidence
};
enum Flag : std::uint32_t {
    CaptureKnown = 1U,
    DriverKnown = 2U,
    FrameTimeKnown = 4U,
    PredictionKnown = 8U,
    BoxValid = 16U,
    OtherBoxValid = 32U,
    ScoreKnown = 64U,
    Complete = 128U,
    Consumed = 256U,
    HasTarget = 512U,
    Critical = 1024U,
    Observed = 2048U,
    ErrorCodeKnown = 4096U,
    DecisionDetails = 8192U,
    NanoCounter = 16384U,
    ControlObservation = 32768U,
    SeedUsed = 65536U,
    StaleSeedUsed = 131072U,
    TrustLossObservation = 262144U
};
/** @brief 数值身份随原帧传递；零标志表示未知，不持有像素或帧引用。 */
struct FrameIdentity {
    std::uint64_t capture_id{};
    std::uint64_t round_id{};
    std::uint32_t driver_sequence{};
    std::uint32_t flags{};
};
/** @brief 固定 256 字节事件；负载含义由 Type 决定，禁止加入指针和动态字段。 */
struct alignas(64) Event {
    std::uint16_t format_version{FORMAT_VERSION};
    std::uint16_t byte_length{256};
    Type type{};
    Reason reason{};
    std::uint32_t flags{};
    std::uint32_t producer_id{};
    std::uint64_t run_id{};
    std::uint64_t round_id{};
    std::uint64_t producer_seq{};
    std::uint64_t mono_ns{};
    FrameIdentity frame{};
    std::uint64_t update_id{};
    std::uint64_t tracker_frame_id{};
    std::uint64_t request_id{};
    std::uint64_t source_update_id{};
    std::uint64_t source_tracker_frame_id{};
    std::uint64_t frame_time_us{};
    std::uint64_t prediction_us{};
    std::uint64_t duration_ns{};
    std::array<float, 4> box{}; // x, y, width, height；有效性由 flags 表示。
    std::array<float, 4> other_box{};
    float score{};
    float iou{};
    float threshold{};
    float highest_score{};
    std::array<std::uint64_t, 7> values{};
    std::array<std::uint32_t, 4> counts{};
};
/** @brief Version 2 payload: values[0]=observation id, [1]=stamp ns, [2..5]=four doubles. */
inline void setObservationValues(Event &event, const std::array<double, 4> &values) noexcept {
    std::memcpy(event.values.data() + 2, values.data(), sizeof(values));
}
inline std::array<double, 4> observationValues(const Event &event) noexcept {
    std::array<double, 4> values{};
    std::memcpy(values.data(), event.values.data() + 2, sizeof(values));
    return values;
}
inline bool supportedVersion(std::uint16_t version) noexcept { return version >= 1 && version <= FORMAT_VERSION; }
static_assert(sizeof(Event) == 256 && alignof(Event) == 64);
static_assert(std::is_trivially_copyable<Event>::value && std::is_standard_layout<Event>::value);
} // namespace diagnostics
#endif // DRONE_DIAGNOSTICS_EVENT_H
