#include "hybrid_tracker.h"
#include "tracking_geometry.h"
#include "tracking_validators.h"
#include "diagnostics/recorder.h"
#include "diagnostics/timing.h"
#include "nano_tracker.h"
#include <iostream>
#include <deque>
#include <vector>
#include <opencv2/opencv.hpp>

#include "yolo_detector.h"
#include "nano_tracker.h"
#include "dcf_tracker.h"
#include "rotation_kf.h"
#ifdef DRONE_TRACKER_DIAGNOSTIC_TESTING
#include "tests/diagnostic_test_access.h"
#endif

namespace {
std::array<float, 4> diagnosticBox(const cv::Rect2f &box) { return {box.x, box.y, box.width, box.height}; }
struct UpdateDiagnostic {
    const FrameContext &context;
    TrackerOutput &output;
    diagnostics::Reason reason{diagnostics::Reason::None};
    std::uint64_t start{diagnostics::monotonicNs()};
    ~UpdateDiagnostic() noexcept {
        auto value{diagnostics::event(diagnostics::Type::UpdateEnd, reason)};
        value.duration_ns = diagnostics::monotonicNs() - start;
        value.prediction_us = context.now_us; value.flags |= diagnostics::PredictionKnown;
        if (context.frame) { value.frame = context.frame->diagnosticIdentity(); value.frame_time_us = context.frame->timestampUs(); value.flags |= diagnostics::FrameTimeKnown; }
        value.values[0] = static_cast<std::uint64_t>(output.state);
        value.values[1] = output.tracking ? 1 : 0;
        if (output.consumed) { value.flags |= diagnostics::Consumed; }
        const auto &box{output.display_box};
        value.box = {static_cast<float>(box.cx - box.width / 2.0), static_cast<float>(box.cy - box.height / 2.0),
            static_cast<float>(box.width), static_cast<float>(box.height)};
        const auto &other{output.realtime_box};
        value.other_box = {static_cast<float>(other.cx - other.width / 2.0), static_cast<float>(other.cy - other.height / 2.0),
            static_cast<float>(other.width), static_cast<float>(other.height)};
        if (box.is_valid) { value.flags |= diagnostics::BoxValid; }
        if (other.is_valid) { value.flags |= diagnostics::OtherBoxValid; }
        diagnostics::emit(value); value.type = diagnostics::Type::Output; diagnostics::emit(value);
        const auto &permission{output.permission};
        auto validity{diagnostics::event(diagnostics::Type::ValidityState)};
        validity.values = {permission.attempt, permission.revision, permission.seed_frame,
            permission.seed_request, static_cast<std::uint64_t>(permission.attempt_deadline_ns),
            static_cast<std::uint64_t>(permission.verified_ns), permission.confirmation_frame};
        validity.counts = {static_cast<std::uint32_t>(permission.phase), permission.allowed ? 1U : 0U, 0, 0};
        diagnostics::emit(validity);
        validity.type = diagnostics::Type::EvidenceClock;
        validity.values = {static_cast<std::uint64_t>(permission.evidence.mapping),
            static_cast<std::uint64_t>(permission.evidence.anchor_ns),
            static_cast<std::uint64_t>(context.decision_clock ? context.decision_clock() : tracking::monotonicNow()),
            static_cast<std::uint64_t>(permission.deadline_ns), permission.confirmation_source_us,
            permission.attempt, permission.revision};
        validity.counts = {permission.evidence.valid ? 1U : 0U, 0, 0, 0}; diagnostics::emit(validity);
    }
};
} // namespace
/**
 * @brief Shared tracking worker with provisional acquisition and bounded detection evidence.
 * A source observation precedes the one result snapshot and current submission.
 * Only fresh later-image verification authorizes trusted filtering and output.
 */
class HybridTrackerImp : public HybridTracker
{
    friend struct HybridTrackerDiagnosticTestAccess;
public:
    HybridTrackerImp(const TrackerSettings& settings, TrackerComponents components, DetectionThreadLauncher launcher);
    virtual ~HybridTrackerImp();

    virtual bool init() override;
    virtual void reset() override;
    void finishRound() override { if (m_detector) { m_detector->finishRound(); } }
    virtual TrackerOutput update(const FrameContext& ctx) override;
    std::shared_ptr<tracking::OutputValidity> validity() const override { return m_validity; }
    virtual cv::Rect2f lastNanoBox() const override { return m_tracker ? m_tracker->lastNanoBox() : cv::Rect2f(); }
    virtual float lastNanoScore() const override { return m_tracker ? m_tracker->lastScore() : -1.0f; }

private:
    /** @brief 补帧缓存条目：帧号 + 帧 + 当时的跟踪框/KF 预测框 */
    struct BufferedFrame
    {
        uint64_t frame_id = 0;
        uint64_t update_id{};
        FrameRef frame;
        cv::Rect2f tracker_box;
        bool has_tracker_box = false;
        cv::Rect2f kf_pred_box;
        bool has_kf_pred = false;
        tracking::EvidenceClock evidence{};
    };

    /** @brief 用历史帧初始化跟踪器并对后续帧前向追踪以追平进度 */
    bool catchupTracker(const BufferedFrame& hist,
                        const std::vector<FrameRef>& catchup_frames,
                        const cv::Rect2f& yolo_box);

    /** @brief 计算两个矩形的 IoU */
    static float calculateIOU(const cv::Rect2f& boxA, const cv::Rect2f& boxB);

    /** @brief 检测边界框是否发生跳变（中心距离超阈值 或 尺寸比例超界） */
    static bool isBoxJump(const cv::Rect2f& prev_box, const cv::Rect2f& curr_box,
                          float max_pos_jump, float max_size_ratio);

    /** @brief 计算 ROI 区域（Python calculate_roi 语义：int 截断 + min_size 下限 + 裁剪） */
    static cv::Rect calculateROI(const cv::Rect2f& box, int img_w, int img_h,
                                 float scale_factor, const cv::Size& min_size);

    static cv::Rect2f toIntRect(const cv::Rect2f& r)
    {
        return cv::Rect2f(static_cast<int>(r.x), static_cast<int>(r.y),
                          static_cast<int>(r.width), static_cast<int>(r.height));
    }

    static BBoxRect toBBoxRect(const cv::Rect2f& r, bool valid)
    {
        return BBoxRect{
            static_cast<int>(r.x + r.width * 0.5f + 0.5f),
            static_cast<int>(r.y + r.height * 0.5f + 0.5f),
            static_cast<int>(r.width + 0.5f),
            static_cast<int>(r.height + 0.5f),
            valid && r.width > 0 && r.height > 0};
    }

    void clearBoxes()
    {
        m_has_display_box = false;
        m_has_previous_box = false;
        m_has_raw_tracker_box = false;
    }

    // --- 设置 ---
    TrackerSettings m_settings;
    TrackerComponents m_components;
    DetectionThreadLauncher m_launcher;
    bool m_initialized{false};

    // --- 状态 ---
    bool m_tracker_initialized = false;
    bool m_watchdog_active{false};
    cv::Rect2f m_display_box;          // 整数化显示框（Python: tuple(map(int, ...))）
    bool m_has_display_box = false;
    cv::Rect2f m_previous_box;
    bool m_has_previous_box = false;
    cv::Rect2f m_raw_tracker_box;      // 原始 float 跟踪结果，喂 KF 避免整数量化噪声
    bool m_has_raw_tracker_box = false;
    std::string m_display_info = "Detecting...";
    cv::Scalar m_display_color = cv::Scalar(0, 255, 255); // 黄色 (BGR)
    int m_frame_count = 0;
    uint64_t m_frame_id_counter = 0;
    uint64_t m_update_counter{};
    std::uint64_t m_generation{};
    int64_t m_last_timestamp_us = 0;
    int m_jump_counter = 0;            // 跟踪框连续跳变计数
    int m_yolo_jump_count = 0;         // YOLO 检测连续跳变计数
    int m_yolo_lost = 0;               // YOLO 连续未确认次数（无目标/低IoU重初始化/未确认跳变）
    int m_init_fail_streak = 0;         // 检测初始化连续失败次数（防死循环保护）
    int m_no_detection_frames = 0;     // 未收到任何检测信号的帧数（TRT 卡死保护）
    TrackerState m_state = TrackerState::Detecting;

    std::deque<BufferedFrame> m_frame_buffer;
    struct Measurement {
        std::uint64_t round{}, attempt{}, frame_id{}, capture_us{};
        cv::Rect2f box{};
        tracking::EvidenceClock evidence{};
        cv::Rect2f raw_box{};
        float score{-1};
        bool replayed{};
    };
    std::deque<Measurement> m_measurements{};
    std::shared_ptr<tracking::OutputValidity> m_validity{std::make_shared<tracking::OutputValidity>()};
    std::function<tracking::Nanoseconds()> m_clock{tracking::monotonicNow};
    std::uint64_t m_attempt_start_update{}, m_last_disposed_request{};


    // --- 组件 ---
    std::unique_ptr<YoloV8Detector> m_detector;
    std::unique_ptr<NanoDcfTracker> m_tracker;
    std::unique_ptr<RotationKalmanFilter> m_kf;
};

std::shared_ptr<HybridTracker> HybridTracker::CreateInstance(
    const TrackerSettings& settings, TrackerComponents components, DetectionThreadLauncher launcher)
{
    return std::make_shared<HybridTrackerImp>(settings, std::move(components), std::move(launcher));
}

HybridTrackerImp::HybridTrackerImp(
    const TrackerSettings& settings, TrackerComponents components, DetectionThreadLauncher launcher)
    : m_settings(settings), m_components(std::move(components)), m_launcher(std::move(launcher))
{
}

HybridTrackerImp::~HybridTrackerImp()
{
    // 先等待检测结束，再允许缓存、跟踪器和模型成员析构。
    if (m_detector) { m_detector->stop(); }
}

bool HybridTrackerImp::init()
{
    tracking::validateTimeouts(m_settings.confirmation_timeout_ms, m_settings.candidate_timeout_ms,
        m_settings.expiry_check_interval_ms);
    if (m_settings.yolo_interval <= 0 || m_settings.frame_buffer_size <= 0 || m_settings.lost_max_count <= 0) {
        throw std::invalid_argument("Tracker cadence, history capacity and attempt count must be positive");
    }
    if (m_initialized) { return true; }
    if (!m_components.complete()) {
        std::cerr << "Incomplete tracker components" << std::endl;
        m_components = {};
        return false;
    }
    try {
        m_detector = std::make_unique<YoloV8Detector>(
            std::move(m_components.yolo), std::move(m_components.yolo_preprocessor),
            m_settings.yolo_conf_threshold, m_settings.yolo_nms_threshold,
            m_settings.target_class_id, m_settings.yolo_recall_relax_ratio,
            m_settings.max_box_area_ratio, std::move(m_launcher));

        NanoDcfTracker::Params tracker_params;
        tracker_params.backbone_path = m_settings.nanotrack_backbone_path;
        tracker_params.head_path = m_settings.nanotrack_head_path;
        tracker_params.enable_dcf = m_settings.enable_dcf;
        tracker_params.dcf = m_settings.dcf;
        tracker_params.max_div_factor = m_settings.dcf_max_div_factor;
        // enable_dcf=false 时的中心平滑 KF 噪声（与主管线 KF 共用调参入口）
        tracker_params.center_kf.r_pos = m_settings.kf_r_pos;
        tracker_params.center_kf.q_a = m_settings.kf_q_a;
        m_tracker = std::make_unique<NanoDcfTracker>(tracker_params,
            std::make_unique<NanoTracker>(
                std::move(m_components.nano),
                std::move(m_components.nano_preprocessor)));

        reset();
        if (!m_detector->init()) {
            throw std::runtime_error("Failed to start YoloV8Detector");
        }
        m_initialized = true;
        return true;
    } catch (const std::exception &error) {
        std::cerr << "HybridTracker initialization failed: " << error.what() << std::endl;
        if (m_detector) { m_detector->stop(); }
        m_detector.reset();
        m_tracker.reset();
        m_kf.reset();
        m_components = {};
        return false;
    }
}

void HybridTrackerImp::reset()
{
    if (!m_detector || !m_tracker)
        return;

    RotationKalmanFilter::Params kf_params;
    kf_params.fx = m_settings.kf_fx;
    kf_params.fy = m_settings.kf_fy;
    kf_params.cx0 = m_settings.kf_cx0;
    kf_params.cy0 = m_settings.kf_cy0;
    kf_params.r_pos = m_settings.kf_r_pos;
    kf_params.r_size = m_settings.kf_r_size;
    kf_params.q_a = m_settings.kf_q_a;
    kf_params.q_size = m_settings.kf_q_size;
    kf_params.history_len = m_settings.kf_history_len;
    m_kf = std::make_unique<RotationKalmanFilter>(kf_params);

    // 只清空跟踪状态，TRT 引擎保留（Python can_reuse_nanotrack 的等价物）
    m_tracker->reset();

    m_tracker_initialized = false; m_watchdog_active = false;
    clearBoxes();
    ++m_generation;
    m_validity->reset(m_generation);
    m_measurements.clear(); m_last_disposed_request = 0;
    m_frame_count = 0;
    m_frame_id_counter = 0;
    m_last_timestamp_us = 0;
    m_frame_buffer.clear();
    m_jump_counter = 0;
    m_yolo_jump_count = 0;
    m_yolo_lost = 0;
    m_init_fail_streak = 0;
    m_no_detection_frames = 0;
    m_state = TrackerState::Detecting;
    m_display_info = "Detecting...";
    m_display_color = cv::Scalar(0, 255, 255); // 黄色 (BGR)
}

bool HybridTrackerImp::catchupTracker(const BufferedFrame& hist,
                                       const std::vector<FrameRef>& catchup_frames,
                                       const cv::Rect2f& yolo_box)
{
    auto initialization{diagnostics::event(diagnostics::Type::InitializeStart)};
    initialization.frame = hist.frame->diagnosticIdentity(); initialization.source_tracker_frame_id = hist.frame_id;
    initialization.source_update_id = hist.update_id; initialization.box = diagnosticBox(yolo_box);
    initialization.flags |= diagnostics::BoxValid;
    diagnostics::emit(initialization);
    const auto init_start{diagnostics::monotonicNs()};
    const bool initialized{m_tracker->init(hist.frame, yolo_box)};
    initialization.type = diagnostics::Type::InitializeEnd;
    initialization.duration_ns = diagnostics::monotonicNs() - init_start;
    if (initialized) { initialization.flags |= diagnostics::Complete; }
    else { initialization.reason = diagnostics::Reason::InitFailed; initialization.flags |= diagnostics::Critical; }
    diagnostics::emit(initialization);
    if (!initialized)
    {
        m_tracker->reset();
        return false;
    }
    const auto permission{m_validity->snapshot()};
    m_display_box = yolo_box;
    m_has_display_box = true;
    m_previous_box = m_display_box;
    m_has_previous_box = true;
    std::uint32_t successful{};
    for (const auto &frame : catchup_frames) {
        cv::Rect2f box{};
        const auto start{diagnostics::monotonicNs()};
        const bool ok{m_tracker->update(frame, box) && std::isfinite(m_tracker->lastScore())
            && m_tracker->lastScore() >= 0 && tracking::validRectangle(box)};
        auto step{diagnostics::event(diagnostics::Type::CatchupStep,
            ok ? diagnostics::Reason::None : diagnostics::Reason::NanoUpdateFailed)};
        step.frame = frame->diagnosticIdentity(); step.frame_time_us = frame->timestampUs();
        step.flags |= diagnostics::FrameTimeKnown;
        step.source_tracker_frame_id = hist.frame_id; step.source_update_id = hist.update_id;
        step.duration_ns = diagnostics::monotonicNs() - start;
        step.box = diagnosticBox(box); step.score = m_tracker->lastScore(); step.flags |= diagnostics::ScoreKnown;
        if (ok) { step.flags |= diagnostics::Complete | diagnostics::BoxValid; ++successful; }
        diagnostics::emit(step);
        if (!ok) {
            auto failed{diagnostics::event(diagnostics::Type::CatchupEnd, diagnostics::Reason::NanoUpdateFailed)};
            failed.source_tracker_frame_id = hist.frame_id; failed.source_update_id = hist.update_id;
            failed.counts[0] = successful + 1; failed.counts[1] = successful; failed.counts[2] = 1;
            diagnostics::emit(failed); m_tracker->reset(); clearBoxes(); return false;
        }
        for (const auto &saved : m_frame_buffer) {
            if (saved.frame == frame) {
                m_measurements.push_back({m_generation, permission.attempt, saved.frame_id,
                    frame->timestampUs(), box, saved.evidence, m_tracker->lastNanoBox(), m_tracker->lastScore(), true});
                break;
            }
        }
        m_display_box = box; m_previous_box = box;
    }
    auto summary{diagnostics::event(diagnostics::Type::CatchupEnd)};
    summary.source_tracker_frame_id = hist.frame_id; summary.source_update_id = hist.update_id;
    summary.counts[0] = static_cast<std::uint32_t>(catchup_frames.size()); summary.counts[1] = successful;
    summary.flags |= diagnostics::Complete; diagnostics::emit(summary);
    return true;
}

TrackerOutput HybridTrackerImp::update(const FrameContext &ctx) {
    if (m_tracker) { m_tracker->clearDiagnostics(); }
    TrackerOutput out{};
    diagnostics::current_update = ++m_update_counter;
    diagnostics::current_tracker_frame = 0; diagnostics::current_request = 0;
    diagnostics::DetailScope detail{};
    UpdateDiagnostic observation{ctx, out};
    auto entered{diagnostics::event(diagnostics::Type::UpdateStart)};
    if (ctx.frame) {
        entered.frame = ctx.frame->diagnosticIdentity(); entered.frame_time_us = ctx.frame->timestampUs();
        entered.flags |= diagnostics::FrameTimeKnown;
    }
    entered.prediction_us = ctx.now_us; entered.flags |= diagnostics::PredictionKnown; diagnostics::emit(entered);
    m_clock = ctx.decision_clock ? ctx.decision_clock : tracking::monotonicNow;
    const auto sourceNow{[&] { return ctx.source_clock_us ? ctx.source_clock_us() : ctx.now_us; }};
    m_validity->expire(m_clock(), sourceNow());
    out.permission = m_validity->snapshot();
    if (!m_detector || !m_tracker || !m_kf || !ctx.frame) {
        observation.reason = diagnostics::Reason::InvalidInput; return out;
    }
    const auto compatibility{m_components.compatibility->validate(ctx.frame)};
    if (!compatibility.reason.empty()) {
        observation.reason = diagnostics::Reason::InvalidInput; out.info = compatibility.describe(); return out;
    }
    const auto frame_timestamp_us{static_cast<std::int64_t>(ctx.frame->timestampUs())};
    if (frame_timestamp_us <= m_last_timestamp_us) {
        observation.reason = diagnostics::Reason::RepeatedTime; return out;
    }
    m_last_timestamp_us = frame_timestamp_us;
    ++m_frame_count; const auto current_frame_id{++m_frame_id_counter};
    diagnostics::current_tracker_frame = current_frame_id;
    const int frame_w{ctx.frame->width()}, frame_h{ctx.frame->height()};
    const cv::Rect2f bounds{0, 0, static_cast<float>(frame_w), static_cast<float>(frame_h)};
    auto permission{m_validity->snapshot()};
    if (m_tracker_initialized && (permission.phase == tracking::Phase::Searching
        || (permission.phase != tracking::Phase::VerifiedTracking && m_clock() >= permission.attempt_deadline_ns))) {
        m_validity->abandon(permission); m_tracker_initialized = false; clearBoxes(); m_measurements.clear();
    }
    permission = m_validity->snapshot();
    cv::Vec4d prediction{};
    const bool predicted{permission.allowed && m_kf->predictOnly(frame_timestamp_us, ctx.R_cam_at_frame, prediction)};
    const cv::Rect2f prediction_box{predicted ? tracking::filterRectangle(prediction) : cv::Rect2f{}};
    BufferedFrame buffered{};
    buffered.frame_id = current_frame_id; buffered.update_id = m_update_counter; buffered.frame = ctx.frame;
    buffered.kf_pred_box = prediction_box; buffered.has_kf_pred = predicted; buffered.evidence = ctx.evidence;
    m_frame_buffer.push_back(buffered);
    auto added{diagnostics::event(diagnostics::Type::HistoryAdd)}; added.frame = ctx.frame->diagnosticIdentity();
    diagnostics::emit(added);
    if (m_frame_buffer.size() > static_cast<std::size_t>(m_settings.frame_buffer_size)) {
        auto removed{diagnostics::event(diagnostics::Type::HistoryEvict)};
        removed.frame = m_frame_buffer.front().frame->diagnosticIdentity();
        removed.source_tracker_frame_id = m_frame_buffer.front().frame_id;
        removed.source_update_id = m_frame_buffer.front().update_id;
        diagnostics::emit(removed); m_frame_buffer.pop_front();
    }
    bool measured{};
    bool reset_filter{};
    cv::Rect2f measured_box{};
    if (m_tracker_initialized) {
        measured = m_tracker->update(ctx.frame, measured_box) && std::isfinite(m_tracker->lastScore())
            && m_tracker->lastScore() >= 0 && tracking::validRectangle(measured_box)
            && !(measured_box & bounds).empty();
        if (measured) {
            m_display_box = measured_box; m_has_display_box = true;
            m_previous_box = measured_box; m_has_previous_box = true;
            m_raw_tracker_box = measured_box; m_has_raw_tracker_box = true;
            m_measurements.push_back({m_generation, permission.attempt, current_frame_id,
                ctx.frame->timestampUs(), measured_box, ctx.evidence, m_tracker->lastNanoBox(), m_tracker->lastScore(), false});
        } else {
            m_validity->revoke(tracking::LossReason::TrackingFailure, m_clock(), sourceNow(), permission);
            m_validity->abandon(m_validity->snapshot()); m_tracker_initialized = false; clearBoxes();
        }
    }
    SourceComparison source{};
    source.generation = m_generation; source.frame_id = current_frame_id; source.update_id = m_update_counter;
    source.capture_time_us = ctx.frame->timestampUs(); source.width = frame_w; source.height = frame_h;
    source.attempt = m_tracker_initialized ? permission.attempt : 0; source.permission_revision = permission.revision; source.evidence = ctx.evidence;
    source.tracker_box = measured_box; source.tracker_valid = measured;
    source.prediction_box = prediction_box; source.prediction_valid = predicted;
    m_frame_buffer.back().tracker_box = measured_box; m_frame_buffer.back().has_tracker_box = measured;
    // Exactly one snapshot. Current submission precedes all saved-result initialization and arbitration.
    YoloResult result{};
    const bool has_result{m_detector->get_result(result)};
#ifdef DRONE_TRACKER_DIAGNOSTIC_TESTING
    HybridTrackerDiagnosticTestAccess::afterSnapshot();
#endif
    if (!m_tracker_initialized || permission.phase != tracking::Phase::VerifiedTracking
        || m_frame_count % m_settings.yolo_interval == 0) {
        cv::Rect roi{};
        const auto base{predicted ? prediction_box : (m_has_display_box ? m_display_box : cv::Rect2f{})};
        if (tracking::validRectangle(base)) {
            const auto [width, height]{m_detector->getInputSize()};
            roi = calculateROI(base, frame_w, frame_h, m_settings.roi_scale_factor, {width / 2, height / 2});
        }
        m_detector->request(ctx.frame, current_frame_id, roi, source);
    } else { diagnostics::emit(diagnostics::event(diagnostics::Type::RequestSkipped, diagnostics::Reason::Interval)); }
    if (has_result) {
        m_no_detection_frames = 0;
        diagnostics::current_request = result.request_id;
        auto decision{diagnostics::event(diagnostics::Type::Decision)};
        decision.source_tracker_frame_id = result.frame_id; decision.source_update_id = result.source_update_id;
        decision.flags |= diagnostics::DecisionDetails;
        decision.counts[0] = m_yolo_lost;
        decision.threshold = m_settings.iou_reset_threshold;
        decision.highest_score = m_settings.iou_calibrate_threshold;
        auto original{diagnostics::event(diagnostics::Type::SourceContext,
            result.source.tracker_valid ? diagnostics::Reason::None : diagnostics::Reason::ContextUnavailable)};
        original.source_tracker_frame_id = result.frame_id; original.source_update_id = result.source_update_id;
        original.frame_time_us = result.source.capture_time_us; original.flags |= diagnostics::FrameTimeKnown;
        original.box = diagnosticBox(result.source.tracker_box);
        original.other_box = diagnosticBox(result.source.prediction_box);
        if (result.source.tracker_valid) { original.flags |= diagnostics::BoxValid; }
        if (result.source.prediction_valid) { original.flags |= diagnostics::OtherBoxValid; }
        original.values[1] = result.source.generation;
        original.values[2] = m_no_detection_frames;
        const bool undisposed{result.request_id > m_last_disposed_request};
        m_last_disposed_request = std::max(m_last_disposed_request, result.request_id);
        const auto now{m_clock()};
        const bool clock_ok{tracking::usableClock(result.source.evidence, now)
            && result.source.capture_time_us <= ctx.frame->timestampUs()};
        const bool fresh{tracking::fresh(result.source.evidence, now, m_settings.confirmation_timeout_ms)};
        const bool same_round{result.source.generation == m_generation};
        const bool source_identity{result.source.frame_id == result.frame_id
            && result.source.capture_time_us > 0 && result.source.capture_time_us <= ctx.frame->timestampUs()};
        const bool completed_search{result.reason == diagnostics::Reason::None || result.reason == diagnostics::Reason::NoTarget};
        permission = m_validity->snapshot();
        const bool previous_attempt{result.source.attempt && result.source.attempt != permission.attempt
            && result.source.update_id > m_attempt_start_update};
        auto history{std::find_if(m_frame_buffer.begin(), m_frame_buffer.end(), [&](const auto &entry) {
            return entry.frame_id == result.frame_id;
        })};
        auto lookup{diagnostics::event(diagnostics::Type::HistoryLookup,
            history == m_frame_buffer.end() ? diagnostics::Reason::HistoryMiss : diagnostics::Reason::None)};
        lookup.values[0] = m_frame_buffer.front().frame_id;
        lookup.values[1] = m_frame_buffer.back().frame_id;
        original.values[0] = history != m_frame_buffer.end();
        diagnostics::emit(original);
        lookup.source_tracker_frame_id = result.frame_id; lookup.source_update_id = result.source_update_id;
        if (result.has_target) { diagnostics::emit(lookup); }
        // The seed exception is limited to searching, original pixels and eligible undisposed identities.
        const auto seed{[&](bool revoked_seed = false) {
            if (!undisposed || !same_round || !source_identity || !clock_ok || !completed_search || previous_attempt || !result.has_target
                || (result.source.attempt && !revoked_seed)
                || history == m_frame_buffer.end() || !tracking::validRectangle(result.best.box)
                || (result.best.box & cv::Rect2f{0, 0, static_cast<float>(result.source.width),
                    static_cast<float>(result.source.height)}).empty()) { return false; }
            const auto begun{m_validity->begin(m_generation, result.frame_id, result.request_id,
                m_clock(), m_settings.candidate_timeout_ms)};
            if (!begun) { return false; }
            m_attempt_start_update = m_update_counter; m_yolo_lost = 0; m_measurements.clear();
            std::vector<FrameRef> replay{};
            for (auto entry{std::next(history)}; entry != m_frame_buffer.end(); ++entry) { replay.push_back(entry->frame); }
            if (!catchupTracker(*history, replay, result.best.box)) {
                m_validity->abandon(*begun); m_tracker_initialized = false; clearBoxes(); return false;
            }
            m_tracker_initialized = m_validity->prepared(*begun);
            m_watchdog_active = m_tracker_initialized;
            if (m_clock() >= begun->attempt_deadline_ns) {
                m_validity->abandon(*begun); m_tracker_initialized = false; clearBoxes();
            }
            if (m_tracker_initialized) {
                decision.flags |= diagnostics::SeedUsed;
                if (!fresh) { decision.flags |= diagnostics::StaleSeedUsed; }
            }
            return m_tracker_initialized;
        }};
        if (!undisposed || !same_round || !source_identity || previous_attempt || !clock_ok || !completed_search) {
            decision.reason = same_round ? diagnostics::Reason::ContextUnavailable : diagnostics::Reason::PreviousRound;
        } else if (!m_tracker_initialized) {
            seed();
        } else if (fresh) {
            permission = m_validity->snapshot();
            cv::Rect2f comparison{};
            bool comparable{};
            bool replay_origin{};
            const auto saved{std::find_if(m_measurements.begin(), m_measurements.end(), [&](const auto &entry) {
                return entry.round == m_generation && entry.attempt == permission.attempt
                    && entry.frame_id == result.frame_id && entry.capture_us == result.source.capture_time_us;
            })};
            if (saved != m_measurements.end()) { comparison = saved->box; comparable = true; replay_origin = saved->replayed;
                decision.score = saved->score; decision.flags |= diagnostics::ScoreKnown; }
            else if (result.source.attempt == permission.attempt && result.source.tracker_valid) {
                comparison = result.source.tracker_box; comparable = true;
            }
            const bool later{result.frame_id > permission.seed_frame};
            if (comparable && later) {
                const cv::Rect2f source_bounds{0, 0, static_cast<float>(result.source.width),
                    static_cast<float>(result.source.height)};
                comparison &= source_bounds;
                int matches{};
                auto candidates{result.candidates};
                if (candidates.empty() && result.has_target) { candidates.push_back(result.best); }
                for (const auto &candidate : candidates) {
                    if (tracking::validRectangle(candidate.box)) {
                        const auto overlap{calculateIOU(comparison, candidate.box & source_bounds)};
                        if (!(decision.flags & diagnostics::OtherBoxValid) || overlap > decision.iou) {
                            decision.iou = overlap;
                            decision.other_box = diagnosticBox(candidate.box & source_bounds);
                            decision.flags |= diagnostics::OtherBoxValid;
                        }
                        if (overlap >= m_settings.iou_reset_threshold) { ++matches; }
                    }
                }
                decision.box = diagnosticBox(comparison); decision.flags |= diagnostics::BoxValid;
                decision.values[0] = replay_origin; decision.values[1] = candidates.size(); decision.values[2] = matches;
                const auto appearance{tracking::appearanceValidation(m_tracker->lastScore())};
                const auto motion{tracking::motionValidation(comparison, result.best.box)};
                if (matches == 1 && !comparison.empty() && appearance.allow && motion.allow) {
                    const bool verified_before{permission.allowed};
                    if (m_validity->confirm(permission, result.frame_id, result.source.capture_time_us,
                        result.source.evidence, m_clock, m_settings.confirmation_timeout_ms, sourceNow())) {
                        m_yolo_lost = 0; reset_filter = !verified_before;
                        m_state = verified_before ? TrackerState::Tracking : TrackerState::TargetFound;
                    }
                } else {
                    ++m_yolo_lost;
                    decision.reason = !result.has_target ? diagnostics::Reason::NoTarget : diagnostics::Reason::LowIou;
                    const auto reason{!result.has_target ? tracking::LossReason::NoTarget
                        : matches > 1 ? tracking::LossReason::Ambiguous : tracking::LossReason::Disagreement};
                    const bool revoked{m_validity->revoke(reason, m_clock(), sourceNow(), permission)};
                    if (revoked || m_yolo_lost >= m_settings.lost_max_count) {
                        decision.flags |= diagnostics::Critical;
                        m_validity->abandon(m_validity->snapshot()); m_tracker_initialized = false; clearBoxes();
                        if (revoked && result.has_target) { seed(true); }
                    }
                }
            } else { decision.reason = diagnostics::Reason::ContextUnavailable; }
        }
        decision.counts[1] = m_yolo_lost; decision.counts[2] = 0; decision.counts[3] = 0;
        auto association{decision}; association.type = diagnostics::Type::CandidateMatch;
        association.values[3] = static_cast<std::uint64_t>(result.source.evidence.anchor_ns);
        association.values[4] = static_cast<std::uint64_t>(now);
        association.values[5] = fresh; association.values[6] = undisposed && same_round && clock_ok;
        association.counts[2] = result.stage_kind; association.counts[3] = result.stage_index;
        diagnostics::emit(association);
        diagnostics::emit(decision);
    } else if (m_watchdog_active) {
        ++m_no_detection_frames;
        const auto limit{static_cast<std::int64_t>(m_settings.lost_max_count) * m_settings.yolo_interval * 3};
        if (m_no_detection_frames > limit) {
            // Cleanup has no authority to extend permission or emit another loss after expiry.
            m_validity->revoke(tracking::LossReason::TrackingFailure, m_clock(), sourceNow());
            m_validity->abandon(m_validity->snapshot()); m_tracker_initialized = false; clearBoxes();
            auto timeout{diagnostics::event(diagnostics::Type::Decision, diagnostics::Reason::NoResultTimeout)};
            timeout.counts[0] = m_no_detection_frames; timeout.values[0] = limit; diagnostics::emit(timeout);
            m_no_detection_frames = 0; m_watchdog_active = false; m_tracker->reset();
        }
    }
    m_jump_counter = 0; m_yolo_jump_count = 0; // Motion and appearance validators are nonblocking, not_evaluated.
    m_validity->expire(m_clock(), sourceNow());
    permission = m_validity->snapshot();
    if (permission.phase != tracking::Phase::VerifiedTracking) { m_state = TrackerState::Detecting; }
    else if (!reset_filter) { m_state = TrackerState::Tracking; }
    if (permission.allowed && m_has_display_box) {
        const auto box{m_display_box};
        const cv::Vec4d center{box.x + box.width / 2, box.y + box.height / 2, box.width, box.height};
        if (reset_filter) { m_kf->initialize(center, frame_timestamp_us, ctx.R_cam_at_frame); out.reset_los = true; }
        else if (measured) { m_kf->update(center, frame_timestamp_us, ctx.R_cam_at_frame); }
        cv::Vec4d realtime{};
        if (m_kf->predictOnly(ctx.now_us, ctx.R_cam_now, realtime)) {
            const cv::Rect2d rectangle{realtime[0] - realtime[2] / 2, realtime[1] - realtime[3] / 2,
                realtime[2], realtime[3]};
            if (std::isfinite(rectangle.x) && std::isfinite(rectangle.y) && std::isfinite(rectangle.width)
                && std::isfinite(rectangle.height) && rectangle.width > 0 && rectangle.height > 0) {
                out.observation_box = rectangle & cv::Rect2d{0, 0, static_cast<double>(frame_w), static_cast<double>(frame_h)};
            }
            if (out.observation_box.empty()) {
                m_validity->revoke(tracking::LossReason::Geometry, m_clock(), sourceNow(), permission);
            } else { out.realtime_box = toBBoxRect(cv::Rect2f{out.observation_box}, true); }
        } else { m_validity->revoke(tracking::LossReason::Geometry, m_clock(), sourceNow(), permission); }
    }
    permission = m_validity->snapshot();
    out.permission = permission; out.consumed = true;
    out.tracking = permission.allowed && !out.observation_box.empty();
    out.display_box = toBBoxRect(m_display_box & bounds, out.tracking);
    out.state = permission.allowed ? m_state : TrackerState::Detecting;
    out.info = permission.allowed ? "Verified tracking" : m_tracker_initialized ? "Verifying provisional" : "Searching";
    out.color = permission.allowed ? 0x00ff00 : 0xffff00;
    // Measurements own values only. Preserve outstanding requests even after source-pixel eviction.
    const auto outstanding{m_detector->outstandingFrames()};
    m_measurements.erase(std::remove_if(m_measurements.begin(), m_measurements.end(), [&](const auto &entry) {
        return entry.attempt != permission.attempt || (entry.frame_id < m_frame_buffer.front().frame_id
            && std::find(outstanding.begin(), outstanding.end(), entry.frame_id) == outstanding.end());
    }), m_measurements.end());
    return out;
}

float HybridTrackerImp::calculateIOU(const cv::Rect2f& boxA, const cv::Rect2f& boxB)
{
    const float xA = std::max(boxA.x, boxB.x);
    const float yA = std::max(boxA.y, boxB.y);
    const float xB = std::min(boxA.x + boxA.width, boxB.x + boxB.width);
    const float yB = std::min(boxA.y + boxA.height, boxB.y + boxB.height);

    const float interArea = std::max(0.0f, xB - xA) * std::max(0.0f, yB - yA);
    const float boxAArea = boxA.width * boxA.height;
    const float boxBArea = boxB.width * boxB.height;

    const float denominator = boxAArea + boxBArea - interArea;
    return (denominator == 0) ? 0.0f : (interArea / denominator);
}

bool HybridTrackerImp::isBoxJump(const cv::Rect2f& prev_box, const cv::Rect2f& curr_box,
                                 float max_pos_jump, float max_size_ratio)
{
    if (prev_box.width <= 0 || prev_box.height <= 0 || curr_box.width <= 0 || curr_box.height <= 0)
        return false;

    const float prev_cx = prev_box.x + prev_box.width / 2.0f;
    const float prev_cy = prev_box.y + prev_box.height / 2.0f;
    const float curr_cx = curr_box.x + curr_box.width / 2.0f;
    const float curr_cy = curr_box.y + curr_box.height / 2.0f;

    const float distance = std::sqrt(std::pow(curr_cx - prev_cx, 2) + std::pow(curr_cy - prev_cy, 2));

    const float size_ratio_w = curr_box.width / prev_box.width;
    const float size_ratio_h = curr_box.height / prev_box.height;

    const bool pos_jump = distance > max_pos_jump;
    const bool size_jump = (size_ratio_w > max_size_ratio || size_ratio_w < 1.0f / max_size_ratio ||
                            size_ratio_h > max_size_ratio || size_ratio_h < 1.0f / max_size_ratio);

    return pos_jump || size_jump;
}

cv::Rect HybridTrackerImp::calculateROI(const cv::Rect2f& box, int img_w, int img_h,
                                        float scale_factor, const cv::Size& min_size)
{
    return tracking::searchRegion(box, img_w, img_h, scale_factor, min_size);
}

#ifdef DRONE_TRACKER_DIAGNOSTIC_TESTING
thread_local std::function<void()> HybridTrackerDiagnosticTestAccess::snapshot_hook{};
void HybridTrackerDiagnosticTestAccess::afterSnapshot() {
    if (snapshot_hook) { snapshot_hook(); }
}
void HybridTrackerDiagnosticTestAccess::jump(HybridTracker &tracker, int counter, float position, float size) {
    auto &implementation{dynamic_cast<HybridTrackerImp &>(tracker)};
    implementation.m_jump_counter = counter;
    implementation.m_settings.max_position_jump = position;
    implementation.m_settings.max_size_jump_ratio = size;
}
void HybridTrackerDiagnosticTestAccess::prime(HybridTracker &tracker, const FrameRef &frame, int lost, int watchdog) {
    auto &value{dynamic_cast<HybridTrackerImp &>(tracker)};
    value.m_detector->finishRound();
    value.m_tracker->init(frame, {40, 30, 20, 20});
    value.m_tracker_initialized = true; value.m_watchdog_active = true;
    value.m_clock = [anchor = static_cast<std::int64_t>(frame->timestampUs() * 1000 + 1000000000)] { return anchor; };
    value.m_kf->initialize({50, 40, 20, 20}, frame->timestampUs(), cv::Matx33d::eye());
    const auto old{value.m_validity->snapshot()};
    if (old.allowed) { value.m_validity->revoke(tracking::LossReason::TrackingFailure, value.m_clock(), 0); }
    while (const auto event = value.m_validity->pending()) { value.m_validity->dispose(*event); }
    const auto attempt{value.m_validity->begin(value.m_generation, 0, 0, value.m_clock(), 1000000)};
    if (attempt) {
        value.m_validity->prepared(*attempt);
        value.m_validity->confirm(*attempt, 1, frame->timestampUs(),
            {tracking::ClockMapping::StandaloneAdmissionV1, value.m_clock(), true}, value.m_clock(), 1000000);
    }
    value.m_yolo_lost = lost; value.m_no_detection_frames = watchdog;
    value.m_settings.max_position_jump = 10000;
    value.m_settings.max_size_jump_ratio = 10000;
    value.m_display_box = {40, 30, 20, 20}; value.m_has_display_box = true;
    value.m_previous_box = value.m_display_box; value.m_has_previous_box = true;
}
void HybridTrackerDiagnosticTestAccess::skipSubmission(HybridTracker &tracker) {
    dynamic_cast<HybridTrackerImp &>(tracker).m_frame_count = 0;
}
void HybridTrackerDiagnosticTestAccess::result(HybridTracker &tracker, YoloResult result) {
    auto &value{dynamic_cast<HybridTrackerImp &>(tracker)};
    if (!result.source.frame_id) { result.source.frame_id = result.frame_id; }
    if (!result.source.capture_time_us) { result.source.capture_time_us = std::max<std::int64_t>(1, value.m_last_timestamp_us); }
    const bool inferred_source{!result.source.generation};
    if (inferred_source) { result.source.generation = value.m_generation; }
    auto &detector{*value.m_detector};
    std::lock_guard<std::mutex> lock{detector.m_mutex};
    if (!result.request_id) { result.request_id = ++detector.m_request_counter; }
    if (inferred_source && !result.source.attempt && value.m_tracker_initialized) {
        result.source.attempt = value.m_validity->snapshot().attempt;
    }
    if (!result.source.evidence.valid) {
        result.source.evidence = {tracking::ClockMapping::StandaloneAdmissionV1, value.m_clock(), true};
    }
    detector.m_last_result = result; detector.m_result_ready = true;
}
std::array<int, 3> HybridTrackerDiagnosticTestAccess::counters(const HybridTracker &tracker) {
    const auto &value{dynamic_cast<const HybridTrackerImp &>(tracker)};
    return {value.m_yolo_lost, value.m_no_detection_frames, value.m_tracker_initialized};
}
bool HybridTrackerDiagnosticTestAccess::idle(const HybridTracker &tracker) {
    const auto &implementation{dynamic_cast<const HybridTrackerImp &>(tracker)};
    const auto &detector{*implementation.m_detector};
    std::lock_guard<std::mutex> lock{detector.m_mutex};
    return !detector.m_executing && !detector.m_new_request;
}
#endif
