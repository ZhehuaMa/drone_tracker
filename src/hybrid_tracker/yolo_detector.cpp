#include "model/validation.h"
#include "yolo_detector.h"
#include "diagnostics/recorder.h"
#include "diagnostics/timing.h"
#include "platform/cpu_affinity.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <numeric>
#include <cstring>
#include <utility>
#include "preprocess/input_validation.h"

#define TILE_OVERLAP_RATIO  0.2f  // 切片重叠比例

namespace {
void cancelledResult(const YoloResult &result, diagnostics::Reason reason) {
    if (!result.job_id) { return; }
    auto event{diagnostics::event(diagnostics::Type::StageDisposition, reason)};
    event.request_id = result.request_id;
    event.source_tracker_frame_id = result.frame_id; event.source_update_id = result.source_update_id;
    event.frame = result.source_identity;
    event.values = {result.job_id, result.plan_id, result.fallback_plan_id, result.source.attempt,
        0, 0, result.source.permission_revision};
    event.counts[3] = result.complete_coverage ? 1U : 0U;
    diagnostics::emit(event);
}
} // namespace

YoloV8Detector::YoloV8Detector(std::unique_ptr<model::YoloBackend> model,
    std::unique_ptr<YoloPreprocessor> preprocessor, float conf_thresh, float nms_thresh,
                               int target_class_id, float recall_relax_ratio, float max_box_area_ratio, DetectionThreadLauncher launcher)
    : model_(std::move(model)),
      preprocessor_(std::move(preprocessor)),
      m_conf_thresh(conf_thresh),
      m_conf_thresh_current(conf_thresh),
      m_relax_ratio(recall_relax_ratio),
      m_nms_thresh(nms_thresh),
      m_max_box_area_ratio(max_box_area_ratio),
      m_target_class_id(target_class_id),
      m_launcher(std::move(launcher)),
      m_result_ready(false),
      m_stop_requested(false)
{
}

YoloV8Detector::~YoloV8Detector() { stop(); }

bool YoloV8Detector::init() {
    if (!model_ || !preprocessor_) { return false; }
    try {
        input_contract_ = model_->inputContract();
        const auto size{yoloInputSize(input_contract_)};
        const auto count{model_->classCount()};
        if (!size) {
            std::cerr << "YOLO initialization failed: " << size.message << std::endl;
            return false;
        }
        if (!count) {
            std::cerr << model::describe(count.error) << std::endl;
            return false;
        }
        if (m_target_class_id < 0 || m_target_class_id >= count.value) {
            std::cerr << "YOLO target class " << m_target_class_id << " outside 0.." << count.value - 1 << std::endl;
            return false;
        }
        class_count_ = count.value;
        input_h = size.value.height;
        input_w = size.value.width;
        m_stop_requested = false;
        m_worker_thread = cpu_affinity::start(cpu_affinity::Role::Yolo, [this] { workerThreadLoop(); }, m_launcher);
        if (!m_worker_thread.joinable()) { return false; }
        return true;
    } catch (const std::exception &error) {
        std::cerr << "YOLO initialization failed: " << error.what() << std::endl;
        return false;
    }
}

void YoloV8Detector::stop()
{
    auto cancelled{diagnostics::event(diagnostics::Type::RequestCancel, diagnostics::Reason::Stopped)};
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (m_stop_requested.exchange(true)) { return; }
        if (m_new_request) {
            cancelled.request_id = m_next_request.request_id;
            cancelled.source_tracker_frame_id = m_next_request.frame_id;
            cancelled.source_update_id = m_next_request.source_update_id;
            cancelled.frame = m_next_request.identity;
        }
    }
    const auto previous{diagnostics::current_producer};
    diagnostics::current_producer = diagnostics::Producer::Control;
    if (cancelled.request_id) { diagnostics::emit(cancelled); }
    diagnostics::current_producer = previous;
    m_cv_request.notify_all();
    if (m_worker_thread.joinable()) { m_worker_thread.join(); }
    std::lock_guard<std::mutex> lock{m_mutex};
    if (m_result_ready) {
        const auto producer{diagnostics::current_producer};
        diagnostics::current_producer = diagnostics::Producer::Control;
        cancelledResult(m_last_result, diagnostics::Reason::Stopped);
        diagnostics::current_producer = producer;
    }
    const auto producer{diagnostics::current_producer};
    diagnostics::current_producer = diagnostics::Producer::Control;
    cancelPlan(diagnostics::Reason::Stopped);
    diagnostics::current_producer = producer;
    m_next_request = {}; m_new_request = false;
    m_last_result = {}; m_result_ready = false;
}

bool YoloV8Detector::request(const FrameRef& frame, uint64_t frame_id, const cv::Rect& roi, const SourceComparison &source)
{
    auto decision{diagnostics::event(diagnostics::Type::RequestDecision)};
    decision.request_id = ++m_request_counter;
    decision.source_update_id = diagnostics::current_update;
    decision.source_tracker_frame_id = frame_id;
    if (frame) {
        decision.frame = frame->diagnosticIdentity();
        decision.frame_time_us = frame->timestampUs();
        decision.flags |= diagnostics::FrameTimeKnown;
    }
    if (validatePreprocessFrame(frame) != PreprocessError::None) {
        decision.reason = diagnostics::Reason::InvalidInput; diagnostics::emit(decision); return false;
    }
    bool accepted{};
    auto superseded{diagnostics::event(diagnostics::Type::RequestCancel, diagnostics::Reason::Superseded)};
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        decision.values[0] = m_executing; decision.values[1] = m_new_request;
        decision.values[2] = m_result_ready; decision.values[3] = m_inflight_id;
        decision.values[4] = m_next_request.request_id; decision.values[5] = m_last_result.request_id;
        if (m_cleaning) { decision.reason = diagnostics::Reason::Cleaning; }
        else if (m_stop_requested) { decision.reason = diagnostics::Reason::Stopped; }
        else if (m_result_ready) { decision.reason = diagnostics::Reason::UnreadResult; }
        else {
            if (m_new_request) {
                superseded.request_id = m_next_request.request_id;
                superseded.source_tracker_frame_id = m_next_request.frame_id;
                superseded.source_update_id = m_next_request.source_update_id;
                superseded.frame = m_next_request.identity;
                superseded.values[0] = decision.request_id;
            }
            m_next_request = {frame, frame_id, roi, decision.request_id, diagnostics::current_update,
                              decision.frame, source};
            m_new_request = true; m_result_ready = false;
            m_cv_request.notify_one(); accepted = true; decision.flags |= diagnostics::Complete;
        }
    }
    if (superseded.request_id) { diagnostics::emit(superseded); }
    diagnostics::emit(decision);
    return accepted;
}

void YoloV8Detector::finishRound() {
    auto pending{diagnostics::event(diagnostics::Type::RequestCancel, diagnostics::Reason::Cleaning)};
    auto unread{pending};
    YoloResult cancelled{};
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_cleaning = true;
        if (m_new_request) { pending.request_id = m_next_request.request_id; pending.source_tracker_frame_id = m_next_request.frame_id; pending.source_update_id = m_next_request.source_update_id; pending.frame = m_next_request.identity; }
        m_new_request = false; m_next_request = {}; cancelPlan(diagnostics::Reason::Cleaning);
        m_cv_request.notify_all();
        m_cv_idle.wait(lock, [this] { return !m_executing; });
        if (m_result_ready) { unread.request_id = m_last_result.request_id; unread.source_tracker_frame_id = m_last_result.frame_id; unread.source_update_id = m_last_result.source_update_id; unread.frame = m_last_result.source_identity; cancelled = m_last_result; }
        m_last_result = {}; m_result_ready = false; m_cleaning = false;
        m_control = {}; m_control_set = false; m_targeted_burst = 0;
        m_cancelled_plans = {}; m_cancelled_plan_cursor = 0;
    }
    if (pending.request_id) { diagnostics::emit(pending); }
    if (unread.request_id) { diagnostics::emit(unread); }
    cancelledResult(cancelled, diagnostics::Reason::Cleaning);
}

bool YoloV8Detector::isBusy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_new_request;
}

bool YoloV8Detector::get_result(YoloResult& result)
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_result_ready || m_cleaning) { return false; }
        result = m_last_result; m_last_result = {}; m_result_ready = false;
        const auto cancellation{planCancellationReason(result.plan_id ? result.plan_id : result.fallback_plan_id)};
        if (cancellation != diagnostics::Reason::None) {
            result.plan_cancellation_reason = cancellation;
            result.complete_coverage = false;
        }
    }
    m_cv_request.notify_all();
    auto value{diagnostics::event(diagnostics::Type::ResultRead, result.reason)};
    value.request_id = result.request_id; value.source_update_id = result.source_update_id;
    value.frame_time_us = result.source.capture_time_us;
    if (result.source.capture_time_us) { value.flags |= diagnostics::FrameTimeKnown; }
    value.source_tracker_frame_id = result.frame_id; value.frame = result.source_identity;
    if (result.has_target) { value.flags |= diagnostics::HasTarget | diagnostics::BoxValid; }
    value.box = {result.best.box.x, result.best.box.y, result.best.box.width, result.best.box.height};
    value.score = result.best.score; diagnostics::emit(value);
    auto credit{diagnostics::event(diagnostics::Type::SchedulerTiming)};
    credit.values = {result.job_id, result.plan_id, diagnostics::monotonicNs(), 0, 0, 0, 0};
    credit.counts[0] = 1; // Take releases capacity, independently of subsequent arbitration.
    diagnostics::emit(credit);
    return true;
}
std::array<std::uint64_t, 4> YoloV8Detector::outstandingFrames() const {
    std::lock_guard<std::mutex> lock{m_mutex};
    return {m_inflight_frame, m_new_request ? m_next_request.frame_id : 0,
        m_result_ready ? m_last_result.frame_id : 0, m_plan ? m_plan->request.frame_id : 0};
}
void YoloV8Detector::publishResult(const Request &request, bool has_target, const Detection &best, diagnostics::Reason reason, const std::vector<Detection> &candidates) {
    auto overwritten{diagnostics::event(diagnostics::Type::ResultOverwrite, diagnostics::Reason::None, true)};
    bool complete_coverage{};
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        const auto cancellation{planCancellationReason(m_plan_id ? m_plan_id : m_fallback_plan_id)};
        complete_coverage = m_complete_coverage && cancellation == diagnostics::Reason::None;
        if (m_result_ready) {
            overwritten.request_id = m_last_result.request_id;
            overwritten.source_tracker_frame_id = m_last_result.frame_id;
            overwritten.values[0] = request.request_id;
        }
        m_last_result = YoloResult{request.frame_id, has_target, best, request.request_id,
            request.source_update_id, request.identity, reason, request.source, candidates, m_stage_kind, static_cast<std::uint32_t>(m_stage.values[1]),
            m_stage_kind == 1 ? cv::Rect{0, 0, request.frame ? request.frame->width() : request.source.width,
                request.frame ? request.frame->height() : request.source.height}
                : cv::Rect{static_cast<int>(m_stage.box[0]), static_cast<int>(m_stage.box[1]),
                    static_cast<int>(m_stage.box[2]), static_cast<int>(m_stage.box[3])},
            m_job_id, m_plan_id, m_fallback_plan_id, m_dispatch_attempt, m_dispatch_revision,
            m_targeted, complete_coverage, cancellation};
        m_result_ready = true;
    }
    if (overwritten.request_id) { diagnostics::emit(overwritten); }
    auto value{diagnostics::event(diagnostics::Type::DetectEnd, reason,
        reason == diagnostics::Reason::InferenceException || reason == diagnostics::Reason::ImagePrepareFailed)};
    value.request_id = request.request_id; value.source_update_id = request.source_update_id;
    value.source_tracker_frame_id = request.frame_id; value.frame = request.identity;
    value.box = {best.box.x, best.box.y, best.box.width, best.box.height}; value.score = best.score;
    value.flags |= diagnostics::Complete | diagnostics::ScoreKnown;
    if (has_target) { value.flags |= diagnostics::HasTarget | diagnostics::BoxValid; }
    value.values = {m_job_id, m_plan_id, m_fallback_plan_id, m_dispatch_attempt,
        m_dispatch_revision, m_targeted ? 1U : 0U, complete_coverage ? 1U : 0U};
    diagnostics::emit(value);
    auto timing{diagnostics::event(diagnostics::Type::SchedulerTiming)};
    timing.values = {m_job_id, m_plan_id, m_prepare_ns, m_infer_ns, m_postprocess_ns, 0, 0};
    diagnostics::emit(timing);
}

std::vector<cv::Rect> YoloV8Detector::createOverlappingTiles(int width, int height, float overlap_ratio)
{
    // 短边固定 2 份，长边自适应份数使每块尽量正方形；
    // tile_size 保证 n 块恰好覆盖长度 L 且相邻重叠 overlap_ratio
    const float base_tile = std::min(width, height) / 2.0f;
    const int n_long = std::max(2, static_cast<int>(std::lround(std::max(width, height) / base_tile)));
    int n_h, n_w;
    if (height <= width) {
        n_h = 2;
        n_w = n_long;
    } else {
        n_h = n_long;
        n_w = 2;
    }

    auto axis_params = [overlap_ratio](int length, int n) {
        const float tile_size = length / (n - (n - 1) * overlap_ratio);
        const float step = tile_size * (1.0f - overlap_ratio);
        std::vector<int> starts(n);
        for (int i = 0; i < n; ++i)
            starts[i] = static_cast<int>(std::lround(i * step));
        // 每块的 [起点, 终点)，最后一块顶到 length
        std::vector<std::pair<int, int>> sizes(n);
        for (int i = 0; i < n; ++i) {
            const int s = starts[i];
            const int e = (i + 1 < n) ? starts[i + 1] + static_cast<int>(std::lround(tile_size - step))
                                      : length;
            sizes[i] = {s, std::min(e, length)};
        }
        return sizes;
    };

    const auto rows = axis_params(height, n_h);
    const auto cols = axis_params(width, n_w);
    std::vector<cv::Rect> tiles;
    for (const auto& [y0, y1] : rows)
        for (const auto& [x0, x1] : cols)
            tiles.emplace_back(x0, y0, x1 - x0, y1 - y0);
    return tiles;
}

std::pair<int, int> YoloV8Detector::getInputSize() const
{
    return {input_w, input_h};
}


void YoloV8Detector::cancelPlan(diagnostics::Reason reason) {
    if (!m_plan) { return; }
    m_cancelled_plans[m_cancelled_plan_cursor++ % m_cancelled_plans.size()] = {m_plan->id, reason};
    auto event{diagnostics::event(diagnostics::Type::PlanCancelled, reason)};
    event.values = {m_plan->id, m_plan->request.request_id, m_plan->request.frame_id,
        m_control.permission.attempt, m_control.permission.revision, m_plan->next, 0};
    event.frame = m_plan->request.identity;
    diagnostics::emit(event); m_plan.reset(); ++m_tile_rotation;
}

diagnostics::Reason YoloV8Detector::planCancellationReason(std::uint64_t plan_id) const {
    if (plan_id) {
        for (const auto &cancelled : m_cancelled_plans) {
            if (cancelled.first == plan_id) { return cancelled.second; }
        }
    }
    return diagnostics::Reason::None;
}

void YoloV8Detector::control(const DetectorControl &control) {
    std::lock_guard<std::mutex> lock{m_mutex};
    if (m_cleaning || m_stop_requested
        || (m_control_set && (control.permission.round < m_control.permission.round
            || (control.permission.round == m_control.permission.round
                && control.permission.revision < m_control.permission.revision)))) { return; }
    if (m_plan) {
        if (m_plan->waiting_job == control.partial_job && !m_plan->searching) {
            m_plan->waiting = false;
        }
        const bool different_round{m_plan->request.source.generation != control.permission.round};
        const bool history_lost{control.oldest_frame && m_plan->request.frame_id < control.oldest_frame};
        const bool superseded{control.agreement_frame && control.agreement_frame >= m_plan->request.frame_id
            && control.permission.attempt == m_control.permission.attempt};
        const bool abandoned{m_control_set && m_control.permission.attempt
            && (control.permission.attempt != m_control.permission.attempt
                || (control.permission.phase == tracking::Phase::Searching
                    && m_control.permission.phase != tracking::Phase::Searching))};
        const bool disposed{m_plan->waiting_job && (m_plan->waiting_job == control.accepted_job
            || m_plan->waiting_job == control.rejected_job)};
        if (different_round || history_lost || superseded || abandoned || disposed) {
            cancelPlan(superseded ? diagnostics::Reason::Superseded
                : history_lost ? diagnostics::Reason::HistoryMiss
                : different_round ? diagnostics::Reason::PreviousRound : diagnostics::Reason::Cleaning);
        }
    }
    m_control = control; m_control_set = true;
    auto event{diagnostics::event(diagnostics::Type::DetectorControl)};
    event.values = {control.permission.round, control.permission.attempt, control.permission.revision,
        control.accepted_job, control.rejected_job, control.agreement_frame, control.oldest_frame};
    event.counts = {static_cast<std::uint32_t>(control.permission.phase), 0, 0, 0};
    diagnostics::emit(event);
    m_cv_request.notify_all();
}

void YoloV8Detector::workerThreadLoop() {
    diagnostics::current_producer = diagnostics::Producer::Yolo;
    diagnostics::positionPoint(200);
    while (true) {
        Request req{};
        cv::Rect region{};
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            const auto runnable{[this] {
                return m_stop_requested || (!m_cleaning && !m_result_ready
                    && ((m_new_request && (!m_plan || (m_control_set
                        && m_control.permission.phase != tracking::Phase::Searching)))
                        || (m_plan && !m_plan->waiting)));
            }};
            while (!runnable()) {
                const std::uint32_t cause{m_result_ready ? 2U : m_plan && m_plan->waiting ? 3U : 4U};
                const auto wait_start{diagnostics::monotonicNs()};
                m_cv_request.wait(lock);
                auto waiting{diagnostics::event(diagnostics::Type::SchedulerTiming)};
                waiting.values = {m_job_id, m_plan ? m_plan->id : 0, wait_start,
                    diagnostics::monotonicNs(), 0, 0, 0};
                waiting.counts = {cause, m_new_request ? 1U : 0U, m_plan ? 1U : 0U,
                    m_result_ready ? 1U : 0U};
                diagnostics::emit(waiting);
            }
            if (m_stop_requested) { break; }
            const auto now{m_control.clock()};
            const bool expired_purpose{m_control_set && ((m_control.permission.allowed
                && now >= m_control.permission.deadline_ns)
                || (m_control.permission.phase != tracking::Phase::Searching
                    && !m_control.permission.allowed && now >= m_control.permission.attempt_deadline_ns))};
            const bool searching{!m_control_set || m_control.permission.phase == tracking::Phase::Searching
                || expired_purpose};
            if (expired_purpose && m_plan && !m_plan->searching) {
                cancelPlan(m_control.permission.allowed ? diagnostics::Reason::StaleEvidence
                    : diagnostics::Reason::CandidateExpired);
            }
            if (m_plan && m_control_set && m_control.permission.round
                && !tracking::usableClock(m_plan->request.source.evidence, now)) {
                cancelPlan(diagnostics::Reason::InvalidClock);
            }
            if (m_new_request && !searching && m_next_request.roi.empty() && !m_control.hint.empty()) {
                m_next_request.roi = m_control.hint & cv::Rect{0, 0,
                    m_next_request.frame->width(), m_next_request.frame->height()};
            }
            if (m_new_request && m_control_set
                && (m_next_request.source.generation != m_control.permission.round
                    || (m_control.oldest_frame && m_next_request.frame_id < m_control.oldest_frame)
                    || (m_control.permission.round && !tracking::usableClock(m_next_request.source.evidence, now))
                    || (!searching && !tracking::fresh(m_next_request.source.evidence, now,
                        m_control.confirmation_timeout_ms)))) {
                auto cancelled{diagnostics::event(diagnostics::Type::RequestCancel,
                    m_next_request.source.generation != m_control.permission.round
                        ? diagnostics::Reason::PreviousRound
                        : m_control.oldest_frame && m_next_request.frame_id < m_control.oldest_frame
                            ? diagnostics::Reason::HistoryMiss
                            : !tracking::usableClock(m_next_request.source.evidence, now)
                                ? diagnostics::Reason::InvalidClock : diagnostics::Reason::StaleEvidence)};
                cancelled.request_id = m_next_request.request_id;
                cancelled.source_tracker_frame_id = m_next_request.frame_id;
                cancelled.source_update_id = m_next_request.source_update_id;
                cancelled.frame = m_next_request.identity; diagnostics::emit(cancelled);
                m_next_request = {}; m_new_request = false;
            }
            const auto create_plan{[&](const Request &source, bool fallback) {
                Plan plan{};
                plan.request = source; plan.id = ++m_plan_counter; plan.searching = searching;
                plan.fallback = fallback;
                plan.regions.push_back({});
                auto tiles{createOverlappingTiles(source.frame->width(), source.frame->height(), TILE_OVERLAP_RATIO)};
                if (!tiles.empty()) {
                    std::rotate(tiles.begin(), tiles.begin() + m_tile_rotation % tiles.size(), tiles.end());
                }
                plan.regions.insert(plan.regions.end(), tiles.begin(), tiles.end());
                if (!fallback && !source.roi.empty()) { plan.regions.insert(plan.regions.begin(), source.roi); }
                m_plan = std::move(plan);
            }};
            if (!m_plan && m_new_request && (searching || m_next_request.roi.empty())) {
                create_plan(m_next_request, false);
                m_next_request = {}; m_new_request = false;
            }
            const bool targeted{m_new_request && !searching && !m_next_request.roi.empty()};
            const bool discovery{m_plan && !m_plan->waiting};
            if (!targeted && !discovery) { continue; }
            m_targeted = targeted && (!discovery || m_targeted_burst < 2);
            m_job_id = ++m_job_counter; m_plan_id = 0; m_fallback_plan_id = 0;
            m_complete_coverage = false;
            m_dispatch_attempt = m_control.permission.attempt;
            m_dispatch_revision = m_control.permission.revision;
            if (m_targeted) {
                req = std::move(m_next_request); m_next_request = {}; m_new_request = false;
                region = req.roi; m_stage_kind = 0; m_stage_index = 0;
                m_targeted_burst = std::min(2U, m_targeted_burst + 1);
                if (!m_plan) {
                    create_plan(req, true);
                    m_plan->initial_job = m_job_id;
                    m_plan->waiting = true; m_plan->waiting_job = m_job_id;
                    m_fallback_plan_id = m_plan->id;
                }
            } else {
                req = m_plan->request; region = m_plan->regions[m_plan->next];
                m_stage_index = static_cast<std::uint32_t>(m_plan->next);
                m_stage_kind = region.empty() ? 1 : (m_plan->next == 0 && region == req.roi ? 0 : 2);
                ++m_plan->next; m_plan_id = m_plan->id;
                m_complete_coverage = m_plan->next == m_plan->regions.size();
                m_targeted_burst = 0;
            }
            // The mutex reserves the single empty result slot for this one active call.
            m_executing = true; m_inflight_id = req.request_id; m_inflight_frame = req.frame_id;
        }
        diagnostics::current_request = req.request_id;
        diagnostics::current_update = req.source_update_id;
        diagnostics::current_tracker_frame = req.frame_id;
        diagnostics::DetailScope detail{};
        auto started{diagnostics::event(diagnostics::Type::DetectStart)};
        started.frame = req.identity; started.source_update_id = req.source_update_id;
        started.source_tracker_frame_id = req.frame_id;
        started.values = {m_job_id, m_plan_id, m_fallback_plan_id, m_dispatch_attempt,
            m_dispatch_revision, m_targeted ? 1U : 0U, m_targeted_burst};
        diagnostics::emit(started);
        m_execution_reason = diagnostics::Reason::None;
        m_prepare_ns = 0; m_infer_ns = 0; m_postprocess_ns = 0;
        std::vector<Detection> candidates{};
        Detection best{};
        std::unique_ptr<YoloFrameSession> session{};
        try {
            const auto snapshot_start{diagnostics::monotonicNs()};
            auto prepared{preprocessor_->beginFrame(req.frame, input_contract_)};
            m_prepare_ns = diagnostics::monotonicNs() - snapshot_start;
            if (prepared && prepared.value) {
                session = std::move(prepared.value);
                m_conf_thresh_current = m_stage_kind == 0 ? m_conf_thresh * m_relax_ratio : m_conf_thresh;
                std::vector<Detection> detections{};
                detectOnImage(*session, region, detections);
                for (const auto &detection : detections) {
                    if (detection.classId == m_target_class_id) {
                        candidates.push_back(detection);
                        if (detection.score > best.score) { best = detection; }
                    }
                }
            } else { m_execution_reason = diagnostics::Reason::ImagePrepareFailed; }
        } catch (const std::exception &) { m_execution_reason = diagnostics::Reason::InferenceException; }
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            if (m_plan && (m_plan->id == m_plan_id || m_plan->id == m_fallback_plan_id)) {
                if (m_execution_reason != diagnostics::Reason::None) { cancelPlan(m_execution_reason); }
                else if (m_complete_coverage) { m_plan.reset(); }
                else if (m_fallback_plan_id && candidates.empty()) { m_plan->waiting = false; }
                else if (!candidates.empty() && (m_plan->searching || m_fallback_plan_id)) {
                    m_plan->waiting = true; m_plan->waiting_job = m_job_id;
                }
            }
        }
        // Device execution has completed. Release the session before allowing another call.
        publishResult(req, !candidates.empty(), best, m_execution_reason != diagnostics::Reason::None
            ? m_execution_reason : candidates.empty() ? diagnostics::Reason::NoTarget : diagnostics::Reason::None,
            candidates);
        session.reset();
        req.frame.reset();
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_executing = false; m_inflight_id = 0; m_inflight_frame = 0;
            m_cv_idle.notify_all();
        }
    }
}

void YoloV8Detector::detectOnImage(YoloFrameSession &session, const cv::Rect &region,
                                   std::vector<Detection> &results) {
    m_stage = diagnostics::event(diagnostics::Type::DetectStage);
    m_stage.values[0] = m_stage_kind; m_stage.values[1] = m_stage_index++;
    m_stage.values[2] = m_job_id; m_stage.values[3] = m_plan_id;
    m_stage.values[4] = m_fallback_plan_id;
    m_stage.box = {static_cast<float>(region.x), static_cast<float>(region.y),
        static_cast<float>(region.width), static_cast<float>(region.height)};
    m_stage.threshold = m_conf_thresh_current;
    const auto stage_start{diagnostics::monotonicNs()};
    auto finish_stage{[this, stage_start](void *) {
        m_stage.duration_ns = diagnostics::monotonicNs() - stage_start;
        if (m_stage.reason != diagnostics::Reason::None) { m_execution_reason = m_stage.reason; }
        diagnostics::emit(m_stage);
    }};
    std::unique_ptr<void, decltype(finish_stage)> stage_guard{this, finish_stage};
    diagnostics::positionPoint(100, m_stage.values[1]);
    const auto prepare_start{diagnostics::monotonicNs()};
    auto prepared{session.prepare(region)};
    m_prepare_ns += diagnostics::monotonicNs() - prepare_start;
    diagnostics::positionPoint(101, m_stage.values[1]);
    if (!prepared || !prepared.value.input) {
        m_stage.reason = diagnostics::Reason::ImagePrepareFailed;
        std::cerr << "YOLO preprocessing failed: " << prepared.message << std::endl;
        return;
    }
    try {
        // Observed marks an actual inference attempt, including a thrown model call.
        m_stage.flags |= diagnostics::Observed;
        diagnostics::positionPoint(102, m_stage.values[1]);
        const auto infer_start{diagnostics::monotonicNs()};
        const auto output{model_->infer(prepared.value.input)};
        m_infer_ns = diagnostics::monotonicNs() - infer_start;
        diagnostics::positionPoint(103, m_stage.values[1]);
        if (!output) {
            m_stage.reason = diagnostics::Reason::InferenceException;
            std::cerr << model::describe(output.error) << std::endl;
            return;
        }
        const auto checked{model::yoloPrediction({output.value.predictions}, "YoloV8Detector")};
        if (!checked || output.value.attribute_axis != checked.value.attribute_axis
            || output.value.candidate_axis != checked.value.candidate_axis) {
            m_stage.reason = diagnostics::Reason::InferenceException; return;
        }
        const auto view{output.value.predictions.read()};
        const auto &dims{output.value.predictions.descriptor.shape};
        if (dims[1] - 4 != class_count_) { m_stage.reason = diagnostics::Reason::InferenceException; return; }
        const auto resolved{resolveYoloRegion(session.sourceSize(), region)};
        if (!resolved) { m_stage.reason = diagnostics::Reason::ImagePrepareFailed; return; }
        m_stage.box = {static_cast<float>(resolved.value.x), static_cast<float>(resolved.value.y),
            static_cast<float>(resolved.value.width), static_cast<float>(resolved.value.height)};
        m_stage.flags |= diagnostics::BoxValid;
        diagnostics::positionPoint(104, m_stage.values[1]);
        const auto postprocess_start{diagnostics::monotonicNs()};
        postprocess(view.data(), dims, prepared.value.transform,
                    resolved.value.size(), cv::Point2f(resolved.value.x, resolved.value.y), results);
        m_postprocess_ns = diagnostics::monotonicNs() - postprocess_start;
        diagnostics::positionPoint(105, m_stage.values[1]);
    } catch (const std::exception &error) {
        m_stage.reason = diagnostics::Reason::InferenceException;
        std::cerr << "YOLO inference failed: " << error.what() << std::endl;
    }
}

void YoloV8Detector::postprocess(const float* output, const TensorShape &output_dims, const LetterboxTransform& lb, const cv::Size& image_size,
    const cv::Point2f& global_offset, std::vector<Detection>& results)
{
    const std::size_t num_proposals{static_cast<std::size_t>(output_dims[2])};
    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;
    std::vector<int> class_ids;
    for (std::size_t i{0}; i < num_proposals; ++i) {
        const float score{output[(4 + static_cast<std::size_t>(m_target_class_id)) * num_proposals + i]};
        ++m_stage.counts[0];
        if (std::isfinite(score)) {
            if (!(m_stage.flags & diagnostics::ScoreKnown) || score > m_stage.highest_score) { m_stage.highest_score = score; }
            m_stage.flags |= diagnostics::ScoreKnown;
        }
        if (score >= m_conf_thresh_current) {
            ++m_stage.counts[1];
            const float cx{output[i]};
            const float cy{output[num_proposals + i]};
            const float w{output[2 * num_proposals + i]};
            const float h{output[3 * num_proposals + i]};
            const float x1{(cx - lb.left - w / 2.0f) / lb.scale_x + global_offset.x};
            const float y1{(cy - lb.top - h / 2.0f) / lb.scale_y + global_offset.y};
            const float x2{(cx - lb.left + w / 2.0f) / lb.scale_x + global_offset.x};
            const float y2{(cy - lb.top + h / 2.0f) / lb.scale_y + global_offset.y};
            if ((x2 - x1) / image_size.width <= m_max_box_area_ratio &&
                (y2 - y1) / image_size.height <= m_max_box_area_ratio) {
                ++m_stage.counts[2];
                boxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                scores.push_back(score);
                class_ids.push_back(m_target_class_id);
            }
        }
    }

    // 执行 NMS
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, m_conf_thresh_current, m_nms_thresh, indices);

    m_stage.counts[3] = static_cast<std::uint32_t>(indices.size());
    m_stage.flags |= diagnostics::Observed | diagnostics::Complete;
    for (int idx : indices)
    {
        results.push_back({cv::Rect2f(boxes[idx]), scores[idx], class_ids[idx]});
    }
}
