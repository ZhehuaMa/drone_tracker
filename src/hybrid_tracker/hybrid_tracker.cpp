#include "hybrid_tracker.h"
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
    }
};
} // namespace
/**
 * @brief airsim_tracker_node.process_frame_callback 的移植。
 *
 * 每帧流程：时间戳单调性检查 -> KF 预测(只读) -> 补帧缓存 ->
 * 检测/跟踪分支（跳变抑制 + YOLO 再检测仲裁）-> KF 校正/重置 ->
 * predict_only(now) 实时外推 -> 返回结果。
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

    m_tracker_initialized = false;
    clearBoxes();
    ++m_generation;
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
    m_display_box = toIntRect(yolo_box);
    m_has_display_box = true;
    m_previous_box = m_display_box;
    m_has_previous_box = true;

    std::uint32_t successful{};
    for (const auto& f : catchup_frames)
    {
        cv::Rect2f t_box;
        const auto step_start{diagnostics::monotonicNs()};
        const bool step_ok{m_tracker->update(f, t_box)};
        auto step{diagnostics::event(diagnostics::Type::CatchupStep)};
        step.frame = f->diagnosticIdentity(); step.frame_time_us = f->timestampUs();
        step.flags |= diagnostics::FrameTimeKnown;
        step.source_tracker_frame_id = hist.frame_id; step.source_update_id = hist.update_id;
        step.duration_ns = diagnostics::monotonicNs() - step_start;
        step.box = diagnosticBox(t_box); step.score = m_tracker->lastScore(); step.flags |= diagnostics::ScoreKnown;
        if (step_ok) { step.flags |= diagnostics::Complete | diagnostics::BoxValid; ++successful; }
        diagnostics::emit(step);
        if (step_ok)
        {
            m_display_box = toIntRect(t_box);
            m_previous_box = m_display_box;
        }
    }
    auto summary{diagnostics::event(diagnostics::Type::CatchupEnd)};
    summary.source_tracker_frame_id = hist.frame_id; summary.source_update_id = hist.update_id;
    summary.counts[0] = static_cast<std::uint32_t>(catchup_frames.size()); summary.counts[1] = successful;
    summary.counts[2] = summary.counts[0] - successful; summary.flags |= diagnostics::Complete;
    diagnostics::emit(summary);
    return true;
}

TrackerOutput HybridTrackerImp::update(const FrameContext& ctx)
{
    if (m_tracker) { m_tracker->clearDiagnostics(); }
    TrackerOutput out;
    diagnostics::current_update = ++m_update_counter;
    diagnostics::current_tracker_frame = 0;
    diagnostics::current_request = 0;
    diagnostics::DetailScope detail{};
    UpdateDiagnostic observation{ctx, out};
    auto entered{diagnostics::event(diagnostics::Type::UpdateStart)};
    if (ctx.frame) { entered.frame = ctx.frame->diagnosticIdentity(); entered.frame_time_us = ctx.frame->timestampUs(); entered.flags |= diagnostics::FrameTimeKnown; }
    entered.prediction_us = ctx.now_us; entered.flags |= diagnostics::PredictionKnown;
    diagnostics::emit(entered);
    out.state = m_state;
    out.info = m_display_info;
    out.color = (uint32_t(m_display_color[2]) << 16) | (uint32_t(m_display_color[1]) << 8) | uint32_t(m_display_color[0]);

    if (!m_detector || !m_tracker || !m_kf || !ctx.frame) {
        observation.reason = diagnostics::Reason::InvalidInput;
        return out;
    }

    const auto compatibility{m_components.compatibility->validate(ctx.frame)};
    if (!compatibility.reason.empty()) {
        observation.reason = diagnostics::Reason::InvalidInput;
        out.info = compatibility.describe();
        std::cerr << out.info << std::endl;
        return out;
    }

    const int64_t frame_timestamp_us = static_cast<int64_t>(ctx.frame->timestampUs());

    // 防止重复帧或时间倒退导致卡尔曼滤波协方差坍缩
    if (frame_timestamp_us <= m_last_timestamp_us) {
        observation.reason = diagnostics::Reason::RepeatedTime;
        return out; // consumed = false，静默丢弃
    }
    m_last_timestamp_us = frame_timestamp_us;

    m_frame_count++;
    m_frame_id_counter++;
    const uint64_t current_frame_id = m_frame_id_counter;
    diagnostics::current_tracker_frame = current_frame_id;

    const int frame_w = ctx.frame->width();
    const int frame_h = ctx.frame->height();

    // 提前获取卡尔曼滤波的预测框，用于本帧的跳变检测和 ROI 生成。
    // 注意：KF 预测是 (cx, cy, w, h) 中心格式，而下游 calculateROI/isBoxJump
    // 按左上角格式解析——与 Python 参考实现保持逐行为一致（其语义如此）。
    cv::Vec4d kf_pred;
    const bool has_kf_pred = m_kf->predictOnly(frame_timestamp_us, ctx.R_cam_at_frame, kf_pred);
    cv::Rect2f kf_pred_box;
    if (has_kf_pred)
        kf_pred_box = cv::Rect2f(kf_pred[0], kf_pred[1], kf_pred[2], kf_pred[3]);

    // 维护缓存队列（保存最新 frame_buffer_size 帧）
    BufferedFrame buffered;
    buffered.frame_id = current_frame_id;
    buffered.update_id = diagnostics::current_update;
    buffered.frame = ctx.frame;
    buffered.has_tracker_box = false;
    buffered.kf_pred_box = kf_pred_box;
    buffered.has_kf_pred = has_kf_pred;
    m_frame_buffer.push_back(buffered);
    auto added{diagnostics::event(diagnostics::Type::HistoryAdd)};
    added.frame = ctx.frame->diagnosticIdentity(); diagnostics::emit(added);
    if (static_cast<int>(m_frame_buffer.size()) > m_settings.frame_buffer_size) {
        auto removed{diagnostics::event(diagnostics::Type::HistoryEvict)};
        removed.frame = m_frame_buffer.front().frame->diagnosticIdentity();
        removed.source_tracker_frame_id = m_frame_buffer.front().frame_id;
        removed.source_update_id = m_frame_buffer.front().update_id;
        diagnostics::emit(removed); m_frame_buffer.pop_front();
    }

    SourceComparison source{};
    source.generation = m_generation;
    source.frame_id = current_frame_id;
    source.update_id = m_update_counter;
    source.capture_time_us = ctx.frame->timestampUs();
    source.width = frame_w; source.height = frame_h;
    source.prediction_box = kf_pred_box; source.prediction_valid = has_kf_pred;

    bool measurement_updated = false;
    bool kf_needs_reset = false;

    // ------------------------------------------------------------------
    // 分支 1：跟踪器未初始化 -> 检测（ROI(KF预测) -> 全图 -> 切片 由 worker 完成）
    // ------------------------------------------------------------------
    if (!m_tracker_initialized)
    {
        m_display_info = "Detecting...";
        m_display_color = cv::Scalar(0, 255, 255); // 黄色
        m_state = TrackerState::Detecting;

        {
            cv::Rect roi;
            if (has_kf_pred)
            {
                // 即使追踪器未初始化，如果有 KF 预测，也可以用作 ROI！
                const auto [in_w, in_h] = m_detector->getInputSize();
                roi = calculateROI(kf_pred_box, frame_w, frame_h,
                                   m_settings.roi_scale_factor * 1.5f, cv::Size(in_w / 2, in_h / 2));
            }
            m_detector->request(ctx.frame, current_frame_id, roi, source);
        }

        YoloResult res;
        if (m_detector->get_result(res) && res.has_target && res.source.generation == m_generation)
        {
            // 找到检测结果对应的缓存帧，补帧初始化并前向追平
            bool found = false;
            size_t idx = 0;
            for (size_t i = 0; i < m_frame_buffer.size(); ++i)
            {
                if (m_frame_buffer[i].frame_id == res.frame_id)
                {
                    found = true;
                    idx = i;
                    break;
                }
            }

            diagnostics::current_request = res.request_id;
            auto lookup{diagnostics::event(diagnostics::Type::HistoryLookup,
                found ? diagnostics::Reason::None : diagnostics::Reason::HistoryMiss, !found)};
            lookup.request_id = res.request_id; lookup.source_tracker_frame_id = res.frame_id;
            lookup.source_update_id = res.source_update_id;
            lookup.values[0] = m_frame_buffer.front().frame_id; lookup.values[1] = m_frame_buffer.back().frame_id;
            diagnostics::emit(lookup);
            auto context_event{diagnostics::event(diagnostics::Type::SourceContext,
                found ? diagnostics::Reason::ContextUnavailable : diagnostics::Reason::RecoveryUnavailable)};
            context_event.request_id = res.request_id;
            context_event.source_tracker_frame_id = res.frame_id;
            context_event.source_update_id = res.source_update_id;
            context_event.frame_time_us = res.source.capture_time_us;
            context_event.flags |= diagnostics::FrameTimeKnown;
            context_event.values[0] = found;
            context_event.values[1] = res.source.generation;
            diagnostics::emit(context_event);
            if (found)
            {
                std::vector<FrameRef> catchup_frames;
                for (size_t i = idx + 1; i < m_frame_buffer.size(); ++i)
                    catchup_frames.push_back(m_frame_buffer[i].frame);

                if (catchupTracker(m_frame_buffer[idx], catchup_frames, res.best.box))
                {
                    m_yolo_lost = 0;
                    m_init_fail_streak = 0;
                    m_tracker_initialized = true;
                    kf_needs_reset = true;
                    m_state = TrackerState::TargetFound;
                    m_display_info = "Target Found";
                    m_display_color = cv::Scalar(0, 255, 255);
                    std::cout << "Tracker initialized at [" << res.best.box.x << ", " << res.best.box.y
                              << ", " << res.best.box.width << ", " << res.best.box.height
                              << "] and caught up " << catchup_frames.size() << " frames" << std::endl;
                }
                else
                {
                    // 初始化失败保护：连续失败多半因为 KF 预测漂移导致 ROI
                    // 停在错误区域——重置 KF，让检测回到全图/切片级联
                    if (++m_init_fail_streak >= 10)
                    {
                        std::cout << "Tracker init failed " << m_init_fail_streak
                                  << " times in a row, resetting KF and detection ROI" << std::endl;
                        m_kf->reset();
                        m_init_fail_streak = 0;
                    }
                }
            }
        }
    }
    // ------------------------------------------------------------------
    // 分支 2：已初始化 -> 跟踪 + 定期 YOLO 再检测仲裁
    // ------------------------------------------------------------------
    else
    {
        bool ok = false;
        cv::Rect2f tracker_box;
        ok = m_tracker->update(ctx.frame, tracker_box);

        if (ok)
        {
            const cv::Rect2f tracker_box_i = toIntRect(tracker_box);

            // 跳变检测：跟踪框需同时偏离 KF 预测位置与上一帧位置才判定失效
            bool jump_from_kf = true;
            if (has_kf_pred)
                jump_from_kf = isBoxJump(kf_pred_box, tracker_box_i,
                                         m_settings.max_position_jump, m_settings.max_size_jump_ratio);

            bool jump_from_prev = true;
            if (m_has_previous_box)
                jump_from_prev = isBoxJump(m_previous_box, tracker_box_i,
                                           m_settings.max_position_jump, m_settings.max_size_jump_ratio);

            auto jump{diagnostics::event(diagnostics::Type::Decision)};
            jump.flags |= diagnostics::DecisionDetails | diagnostics::NanoCounter | diagnostics::BoxValid;
            jump.box = diagnosticBox(tracker_box_i);
            jump.other_box = diagnosticBox(m_previous_box);
            if (m_has_previous_box) { jump.flags |= diagnostics::OtherBoxValid; }
            jump.counts[0] = m_jump_counter;
            jump.counts[1] = m_jump_counter;
            jump.counts[3] = m_settings.jump_tolerance;
            jump.score = m_settings.max_position_jump;
            jump.threshold = m_settings.max_size_jump_ratio;
            jump.values[0] = 1; // NanoTrack 跳变计数；区别 YOLO 未确认计数。
            jump.values[1] = jump_from_kf;
            jump.values[2] = jump_from_prev;
            auto basis{diagnostics::event(diagnostics::Type::JumpComparison)};
            basis.values[0] = 1; // NanoTrack 与 KF 比较。
            basis.box = diagnosticBox(kf_pred_box);
            basis.other_box = diagnosticBox(tracker_box_i);
            basis.flags |= diagnostics::OtherBoxValid;
            if (has_kf_pred) { basis.flags |= diagnostics::BoxValid; }
            basis.counts[0] = jump_from_kf;
            basis.score = m_settings.max_position_jump;
            basis.threshold = m_settings.max_size_jump_ratio;
            diagnostics::emit(basis);
            if (jump_from_kf && jump_from_prev)
            {
                jump.reason = diagnostics::Reason::NanoJump;
                m_jump_counter++;
                jump.counts[1] = m_jump_counter;
                std::cout << "Box jump detected! curr=[" << tracker_box_i.x << ", " << tracker_box_i.y
                          << ", " << tracker_box_i.width << ", " << tracker_box_i.height
                          << "] (counter=" << m_jump_counter << ")" << std::endl;

                // 连续多次跳变才重置，否则本帧不喂 KF、保持旧显示框
                if (m_jump_counter >= m_settings.jump_tolerance)
                {
                    std::cout << "Persistent jump, re-initializing tracker..." << std::endl;
                    m_tracker_initialized = false;
                    clearBoxes();
                    m_jump_counter = 0;
                    observation.reason = diagnostics::Reason::NanoJump;
                    jump.flags |= diagnostics::Critical;
                    jump.counts[2] = m_jump_counter;
                    diagnostics::emit(jump);
                    // 提前返回：本帧不发布（Python 的 return True 早退）
                    return out;
                }
                m_display_info = "KF Predict (Jump)";
                m_display_color = cv::Scalar(0, 165, 255); // 橙色
                // measurement_updated 保持 false
            }
            else
            {
                m_jump_counter = 0;
                m_display_box = tracker_box_i;
                m_has_display_box = true;
                m_previous_box = tracker_box_i;
                m_has_previous_box = true;
                // 保存原始 float 的追踪结果，避免整数截断引入量化噪声
                m_raw_tracker_box = tracker_box;
                m_has_raw_tracker_box = true;
                measurement_updated = true;
                m_display_info = "Tracking (nanotrack)";
                m_display_color = cv::Scalar(0, 255, 0); // 绿色
            }
            jump.counts[2] = m_jump_counter;
            diagnostics::emit(jump);
            m_state = TrackerState::Tracking;
        }
        else
        {
            // 追踪丢失，不喂给 KF
            m_display_info = "Predicting";
            m_display_color = cv::Scalar(0, 165, 255); // 橙色
            m_state = TrackerState::Tracking;
        }

        // Snapshot the actual source observation before any delayed detector arbitration.
        source.tracker_box = toIntRect(tracker_box);
        source.tracker_valid = ok && source.tracker_box.width > 0 && source.tracker_box.height > 0;
        m_frame_buffer.back().tracker_box = source.tracker_box;
        m_frame_buffer.back().has_tracker_box = source.tracker_valid;

        // 定期请求 YOLO 检测（ROI 优先）
        if (m_frame_count % m_settings.yolo_interval == 0)
        {
            {
                cv::Rect roi;
                const cv::Rect2f roi_base = has_kf_pred ? kf_pred_box
                                           : (m_has_display_box ? m_display_box : cv::Rect2f());
                if (roi_base.width > 0 && roi_base.height > 0)
                {
                    const auto [in_w, in_h] = m_detector->getInputSize();
                    roi = calculateROI(roi_base, frame_w, frame_h,
                                       m_settings.roi_scale_factor, cv::Size(in_w / 2, in_h / 2));
                }
                m_detector->request(ctx.frame, current_frame_id, roi, source);
            }
        } else {
            diagnostics::emit(diagnostics::event(diagnostics::Type::RequestSkipped, diagnostics::Reason::Interval));
        }

        // 检查 YOLO 检测结果
        YoloResult res;
        if (m_detector->get_result(res))
        {
            diagnostics::current_request = res.request_id;
            auto decision{diagnostics::event(diagnostics::Type::Decision)};
            decision.source_tracker_frame_id = res.frame_id; decision.source_update_id = res.source_update_id;
            decision.counts[0] = m_yolo_lost; decision.counts[2] = m_yolo_jump_count;
            decision.flags |= diagnostics::DecisionDetails;
            decision.values[2] = ok;
            decision.values[3] = m_settings.lost_max_count;
            decision.values[4] = m_settings.jump_tolerance;
            const auto state_before{m_state};
            m_no_detection_frames = 0; // 收到检测信号，重置无信号计数
            if (res.source.generation != m_generation) {
                decision.reason = diagnostics::Reason::PreviousRound;
            }
            else if (res.has_target)
            {
                const cv::Rect2f& best_match_box = res.best.box;

                // 获取历史帧对应的 tracker_box / kf_pred_box
                bool found = false;
                size_t idx = 0;
                for (size_t i = 0; i < m_frame_buffer.size(); ++i)
                {
                    if (m_frame_buffer[i].frame_id == res.frame_id)
                    {
                        found = true;
                        idx = i;
                        break;
                    }
                }
                auto lookup{diagnostics::event(diagnostics::Type::HistoryLookup,
                    found ? diagnostics::Reason::None : diagnostics::Reason::HistoryMiss, !found)};
                lookup.request_id = res.request_id; lookup.source_tracker_frame_id = res.frame_id;
                lookup.source_update_id = res.source_update_id;
                lookup.values[0] = m_frame_buffer.front().frame_id; lookup.values[1] = m_frame_buffer.back().frame_id;
                diagnostics::emit(lookup);
                const BufferedFrame* hist = found ? &m_frame_buffer[idx] : nullptr;
                std::vector<FrameRef> catchup_frames;
                if (found)
                {
                    for (size_t i = idx + 1; i < m_frame_buffer.size(); ++i)
                        catchup_frames.push_back(m_frame_buffer[i].frame);
                }

                // 边缘目标仲裁：目标贴边/半出画时，YOLO 只能看到可见部分而
                // 跟踪器估计完整外接框，两者对"不可见部分"的估计天然不同——
                // 直接比较会导致 IoU 偏低、跳变误判，进而反复重初始化/判丢，
                // 表现为边缘 bbox 抖动。这里把参与比较的框统一裁剪到图像范围，
                // 只比较可见部分。
                const cv::Rect2f image_rect(0.0f, 0.0f,
                                            static_cast<float>(res.source.width), static_cast<float>(res.source.height));
                auto clipped = [&image_rect](cv::Rect2f r) { r &= image_rect; return r; };
                const cv::Rect2f yolo_box_vis = clipped(best_match_box);
                const auto &comparison{res.source};
                const cv::Rect2f hist_tracker_vis = clipped(comparison.tracker_box);
                const bool comparable{comparison.tracker_valid};
                const float current_iou{comparable ? calculateIOU(hist_tracker_vis, yolo_box_vis) : 0.0f};
                bool yolo_jump_kf{true};
                if (comparison.prediction_valid) {
                    yolo_jump_kf = isBoxJump(clipped(comparison.prediction_box), yolo_box_vis,
                                            m_settings.max_position_jump, m_settings.max_size_jump_ratio);
                }
                bool yolo_jump_prev{false};
                if (comparable) {
                    yolo_jump_prev = isBoxJump(hist_tracker_vis, yolo_box_vis,
                                              m_settings.max_position_jump, m_settings.max_size_jump_ratio);
                }
                auto context{diagnostics::event(diagnostics::Type::SourceContext,
                    comparable ? diagnostics::Reason::None : diagnostics::Reason::ContextUnavailable)};
                context.source_tracker_frame_id = res.frame_id;
                context.source_update_id = res.source_update_id;
                context.frame_time_us = comparison.capture_time_us;
                context.flags |= diagnostics::FrameTimeKnown;
                context.box = diagnosticBox(hist_tracker_vis);
                context.other_box = diagnosticBox(clipped(comparison.prediction_box));
                if (comparable) { context.flags |= diagnostics::BoxValid; }
                if (comparison.prediction_valid) { context.flags |= diagnostics::OtherBoxValid; }
                context.values[0] = found; context.values[1] = comparison.generation;
                context.values[2] = m_no_detection_frames;
                context.iou = current_iou;
                diagnostics::emit(context);
                const bool yolo_jump = yolo_jump_kf && yolo_jump_prev;
                auto basis{diagnostics::event(diagnostics::Type::JumpComparison)};
                basis.source_tracker_frame_id = res.frame_id;
                basis.source_update_id = res.source_update_id;
                basis.values[0] = 2; // YOLO 与历史 KF 比较，使用裁剪后的实际框。
                basis.other_box = diagnosticBox(yolo_box_vis);
                basis.flags |= diagnostics::OtherBoxValid;
                if (comparison.prediction_valid) {
                    basis.box = diagnosticBox(clipped(comparison.prediction_box));
                    basis.flags |= diagnostics::BoxValid;
                }
                basis.counts[0] = yolo_jump_kf;
                basis.score = m_settings.max_position_jump;
                basis.threshold = m_settings.max_size_jump_ratio;
                diagnostics::emit(basis);
                basis.values[0] = 3; // YOLO 与历史跟踪框或前一显示框比较。
                basis.flags &= ~diagnostics::BoxValid;
                basis.box = diagnosticBox(hist_tracker_vis);
                if (comparable) { basis.flags |= diagnostics::BoxValid; }
                basis.counts[0] = yolo_jump_prev;
                diagnostics::emit(basis);
                decision.box = diagnosticBox(hist_tracker_vis); decision.other_box = diagnosticBox(yolo_box_vis);
                decision.iou = current_iou; decision.threshold = m_settings.iou_reset_threshold;
                decision.highest_score = m_settings.iou_calibrate_threshold;
                decision.flags |= diagnostics::OtherBoxValid;
                if (comparable) { decision.flags |= diagnostics::BoxValid; }

                auto unavailable_recovery = [&] {
                    auto unavailable{context};
                    unavailable.reason = diagnostics::Reason::RecoveryUnavailable;
                    diagnostics::emit(unavailable);
                };
                if (!comparable) {
                    decision.reason = diagnostics::Reason::ContextUnavailable;
                }
                else if (yolo_jump && current_iou < m_settings.iou_calibrate_threshold)
                {
                    decision.reason = diagnostics::Reason::YoloJump;
                    m_yolo_jump_count++;
                    std::cout << "YOLO detection jump (" << m_yolo_jump_count << ")! IOU="
                              << current_iou << std::endl;
                    // 连续多次"跳变"说明目标确实移动了，应该接受新检测
                    if (m_yolo_jump_count >= m_settings.jump_tolerance)
                    {
                        std::cout << "Accepting jumped YOLO detection after consecutive jumps" << std::endl;
                        if (hist && catchupTracker(*hist, catchup_frames, best_match_box))
                        {
                            m_display_info = "Re-initialized (jump accept)";
                            m_display_color = cv::Scalar(255, 255, 0); // 青色
                            kf_needs_reset = true;
                        }
                        m_yolo_jump_count = 0;
                        if (hist) {
                            m_yolo_lost = 0; // Preserve existing image-backed acceptance behavior.
                        } else {
                            unavailable_recovery();
                            ++m_yolo_lost;
                        }
                    }
                    else
                    {
                        m_yolo_lost++;     // 未获确认的跳变检测
                    }
                }
                else if (!ok || current_iou < m_settings.iou_reset_threshold)
                {
                    decision.reason = current_iou < m_settings.iou_reset_threshold
                        ? diagnostics::Reason::LowIou : diagnostics::Reason::NanoUpdateFailed;
                    m_yolo_jump_count = 0;
                    // 重新初始化追踪器（复用已加载的 TRT 引擎）
                    if (hist)
                    {
                        if (catchupTracker(*hist, catchup_frames, best_match_box))
                        {
                            m_display_info = "Re-initialized (nanotrack)";
                            m_display_color = cv::Scalar(255, 255, 0); // 青色
                            kf_needs_reset = true;
                        }
                        else
                        {
                            std::cerr << "Failed to re-initialize tracker" << std::endl;
                        }
                    }
                    else { unavailable_recovery(); }
                    m_yolo_lost++;         // 低 IoU 重初始化：本帧未获确认
                }
                // IOU 足够高时不覆盖当前追踪框，避免引入滞后
                else if (current_iou > m_settings.iou_calibrate_threshold)
                {
                    m_yolo_jump_count = 0;
                    m_yolo_lost = 0;       // YOLO 确认
                    m_display_info = "YOLO Confirmed";
                    m_display_color = cv::Scalar(0, 0, 255); // 红色
                }
                else
                {
                    m_yolo_jump_count = 0;
                    m_yolo_lost = 0;       // 中间带：跟踪器与检测保持合理重叠
                }
            }
            else
            {
                decision.reason = diagnostics::Reason::NoTarget;
                // YOLO 没有检测到目标
                m_yolo_jump_count = 0;
                m_yolo_lost++;
            }

            // 连续未确认（无目标 / 低 IoU 重初始化 / 未确认跳变）达到上限 ->
            // 判定丢失。仅有"无目标"判据不足以覆盖 YOLO 持续误检的场景：
            // 目标实际丢失后，误检会让跟踪器在垃圾框上无限重初始化而永不报丢。
            if (m_tracker_initialized && m_yolo_lost >= m_settings.lost_max_count)
            {
                m_tracker_initialized = false;
                clearBoxes();
                std::cout << "Target lost (unconfirmed streak " << m_yolo_lost << ")."
                          << " Re-initializing detection." << std::endl;
                m_state = TrackerState::TargetLost;
                decision.flags |= diagnostics::Critical;
            }
            decision.counts[1] = m_yolo_lost; decision.counts[3] = m_yolo_jump_count;
            decision.values[0] = static_cast<std::uint64_t>(state_before);
            decision.values[1] = static_cast<std::uint64_t>(m_state);
            diagnostics::emit(decision);
        }
        else
        {
            // 没有收到检测信号，累加无信号计数；长时间无任何信号（TRT 线程
            // 可能卡死）则强制重置
            m_no_detection_frames++;
            const int no_detect_limit = m_settings.lost_max_count * m_settings.yolo_interval * 3;
            if (m_no_detection_frames > no_detect_limit)
            {
                std::cout << "No YOLO detection signal for " << m_no_detection_frames
                          << " frames, forcing tracker reset..." << std::endl;
                m_tracker_initialized = false;
                clearBoxes();
                auto timeout{diagnostics::event(diagnostics::Type::Decision, diagnostics::Reason::NoResultTimeout, true)};
                timeout.counts[0] = m_no_detection_frames; timeout.values[0] = no_detect_limit;
                diagnostics::emit(timeout);
                m_no_detection_frames = 0;
            }
        }
    }

    // ------------------------------------------------------------------
    // KF 校正/重置 + 实时外推
    // ------------------------------------------------------------------
    if (m_has_display_box)
    {
        if (kf_needs_reset)
        {
            // 跟踪器刚被 YOLO 拉回：重置 KF 以清空错误的旧速度
            const float rx = m_display_box.x, ry = m_display_box.y;
            const float rw = m_display_box.width, rh = m_display_box.height;
            m_kf->initialize(cv::Vec4d(rx + rw / 2.0, ry + rh / 2.0, rw, rh),
                             frame_timestamp_us, ctx.R_cam_at_frame);
        }
        else if (measurement_updated && m_has_raw_tracker_box)
        {
            // 使用原始 float 追踪结果校正（回溯到帧时刻）
            const float rx = m_raw_tracker_box.x, ry = m_raw_tracker_box.y;
            const float rw = m_raw_tracker_box.width, rh = m_raw_tracker_box.height;
            m_kf->update(cv::Vec4d(rx + rw / 2.0, ry + rh / 2.0, rw, rh),
                         frame_timestamp_us, ctx.R_cam_at_frame);
        }

        // predict_only 查询实时位置（不修改滤波器状态和历史）。
        // 发布框裁剪到图像范围：贴边目标的外推可能越界，下游不应收到
        // 图像外的测量（内部 KF 状态保持不裁剪，目标回到画内可自然恢复）
        cv::Vec4d realtime;
        if (m_kf->predictOnly(ctx.now_us, ctx.R_cam_now, realtime))
        {
            cv::Rect2f rt_box(realtime[0] - realtime[2] / 2.0,
                              realtime[1] - realtime[3] / 2.0,
                              realtime[2], realtime[3]);
            rt_box &= cv::Rect2f(0.0f, 0.0f,
                                 static_cast<float>(frame_w), static_cast<float>(frame_h));
            out.realtime_box = toBBoxRect(rt_box, true);
        }
    }

    out.consumed = true;
    out.tracking = m_tracker_initialized && m_has_display_box;
    {
        cv::Rect2f display = m_display_box;
        display &= cv::Rect2f(0.0f, 0.0f,
                              static_cast<float>(frame_w), static_cast<float>(frame_h));
        out.display_box = toBBoxRect(display, m_has_display_box);
    }
    out.state = m_state;
    out.info = m_display_info;
    out.color = (uint32_t(m_display_color[2]) << 16) | (uint32_t(m_display_color[1]) << 8) | uint32_t(m_display_color[0]);
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
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;

    int roi_w = static_cast<int>(box.width * scale_factor);
    int roi_h = static_cast<int>(box.height * scale_factor);

    // 确保ROI不小于最小尺寸
    if (min_size.width > 0)
        roi_w = std::max(roi_w, min_size.width);
    if (min_size.height > 0)
        roi_h = std::max(roi_h, min_size.height);

    int roi_x = static_cast<int>(cx - roi_w / 2.0f);
    int roi_y = static_cast<int>(cy - roi_h / 2.0f);

    // 确保ROI在图像范围内
    roi_x = std::max(0, std::min(roi_x, img_w - roi_w));
    roi_y = std::max(0, std::min(roi_y, img_h - roi_h));
    roi_w = std::min(roi_w, img_w - roi_x);
    roi_h = std::min(roi_h, img_h - roi_y);

    return cv::Rect(roi_x, roi_y, roi_w, roi_h);
}

#ifdef DRONE_TRACKER_DIAGNOSTIC_TESTING
#include "tests/diagnostic_test_access.h"
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
    value.m_tracker_initialized = true;
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
    if (!result.source.generation) { result.source.generation = value.m_generation; }
    auto &detector{*value.m_detector};
    std::lock_guard<std::mutex> lock{detector.m_mutex};
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
