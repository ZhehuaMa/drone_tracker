#ifndef YOLOV8DETECTOR_H
#define YOLOV8DETECTOR_H

#include <array>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <opencv2/opencv.hpp>

#include "preprocess/image_preprocessor.h"
#include "model/model_backend.h"
#include "frame/frame.h"
#include "diagnostics/event.h"
#include "detection_thread.h"
#include "output_validity.h"
#include "preprocess/preprocess_geometry.h"


// 用于保存检测结果的结构体
struct Detection
{
    cv::Rect2f box;
    float score;
    int classId;

    bool operator<(const Detection& other) const
    {
        return score < other.score; // 用于优先队列，分数较高的优先
    }
};

/** @brief Value-only comparison context. No frame or image ownership is retained.
 * A submitted copy is immutable until consumption, overwrite, supersession or round cleanup.
 * Tracker validity means a genuine observation of this source frame, never a held display box.
 */
struct SourceComparison {
    std::uint64_t generation{}, frame_id{}, update_id{}, capture_time_us{};
    std::uint64_t attempt{}, permission_revision{};
    tracking::EvidenceClock evidence{};
    int width{}, height{};
    cv::Rect2f tracker_box{}, prediction_box{};
    bool tracker_valid{}, prediction_valid{};
};

// 检测结果：单框 + 它所对应的帧号（异步检测延迟的关键——主循环据此找到
// 历史帧做补帧初始化）
struct YoloResult
{
    uint64_t frame_id = 0;
    bool has_target = false;
    Detection best{};
    std::uint64_t request_id{}, source_update_id{};
    diagnostics::FrameIdentity source_identity{};
    diagnostics::Reason reason{};
    SourceComparison source{};
    std::vector<Detection> candidates{};
    std::uint32_t stage_kind{}, stage_index{};
    cv::Rect searched_region{};
    std::uint64_t job_id{}, plan_id{}, fallback_plan_id{};
    std::uint64_t dispatch_attempt{}, dispatch_revision{};
    bool targeted{}, complete_coverage{true};
    // Eligibility sampled at publication/take; original stage/source identity stays unchanged.
    diagnostics::Reason plan_cancellation_reason{diagnostics::Reason::None};
};

/** @brief Value-only worker purpose. Original submitted metadata is never rewritten.
 * Seed acceptance is sent before template preparation; taking a result separately frees capacity.
 */
struct DetectorControl {
    tracking::Permission permission{};
    std::uint64_t oldest_frame{}, accepted_job{}, rejected_job{}, agreement_frame{}, partial_job{};
    cv::Rect hint{};
    int confirmation_timeout_ms{100};
    std::function<tracking::Nanoseconds()> clock{tracking::monotonicNow};
};

class YoloV8Detector
{
public:
    YoloV8Detector(std::unique_ptr<model::YoloBackend> model,
                   std::unique_ptr<YoloPreprocessor> preprocessor, float conf_thresh, float nms_thresh,
                   int target_class_id, float recall_relax_ratio, float max_box_area_ratio = 0.8f,
                   DetectionThreadLauncher launcher = launchDetectionThread);
    ~YoloV8Detector();

    /** @brief 校验注入模型的输入契约并启动工作线程 */
    bool init();

    /** @brief 请求对新帧进行检测（ROI -> 全图 -> 自适应切片级联）
     * @param frame 要处理的图像
     * @param frame_id 帧号，随结果一并返回
     * @param roi 感兴趣区域 (如果为空则直接全图+切片)
     * @return 如果成功提交检测，则为 true
     */
    bool request(const FrameRef& frame, uint64_t frame_id,
                 const cv::Rect& roi = cv::Rect(), const SourceComparison &source = {});

    /** @brief Report pending occupancy; this does not block replacement or describe execution.
     * Valid input atomically replaces the one unstarted slot, never executing input.
     * Unread results, cleanup and stop still block admission. Invalid input preserves the slot.
     */
    bool isBusy() const;

    /** @brief 获取最新的检测结果（可能为空目标，信号本身表示检测已完成）
     * @return 如果成功获取了新结果，则为 true
     */
    bool get_result(YoloResult& result);
    std::array<std::uint64_t, 4> outstandingFrames() const;
    /** @brief Publish a short phase/disposition snapshot without waiting for inference. */
    void control(const DetectorControl &control);

    /** @brief 获取模型输入尺寸 */
    std::pair<int, int> getInputSize() const;

    /** @brief 停止工作线程 */
    void stop();
    /** @brief 停止提交 update 后调用；撤销排队任务，等待帧访问结束并清空旧结果。 */
    void finishRound();

    /** @brief 自适应重叠切片（Python create_overlapping_tiles 移植）：
     * 短边固定 2 份，长边自适应最少份数(>=2)使每块尽量正方形 */
    static std::vector<cv::Rect> createOverlappingTiles(int width, int height, float overlap_ratio);

private:
    friend struct HybridTrackerDiagnosticTestAccess;
    /** @brief 工作线程的主循环 */
    void workerThreadLoop();
    /** @brief Cancel the one plan under m_mutex, retaining its terminal reason before releasing pixels. */
    void cancelPlan(diagnostics::Reason reason);
    /** @brief Read bounded cancellation values while m_mutex is held. */
    diagnostics::Reason planCancellationReason(std::uint64_t plan_id) const;

    void detectOnImage(YoloFrameSession &session, const cv::Rect &region, std::vector<Detection> &results);

    /** @brief 对模型输出进行后处理（解码, NMS）
     * @param output 模型的原始输出
     * @param lb 预处理中的 letterbox 变换参数
     * @param global_offset 全局坐标偏移量 (用于切片或ROI)
     * @param results [out] 检测结果列表
     */
    void postprocess(const float* output, const TensorShape &output_dims, const LetterboxTransform& lb, const cv::Size& image_size,
        const cv::Point2f& global_offset, std::vector<Detection>& results);

    std::unique_ptr<model::YoloBackend> model_;
    std::unique_ptr<YoloPreprocessor> preprocessor_;
    ModelInputContract input_contract_{};

    int class_count_{0};
    int input_w = 0;
    int input_h = 0;
    
    // --- 阈值 ---
    float m_conf_thresh;
    float m_conf_thresh_current;
    float m_relax_ratio;
    float m_nms_thresh;
    float m_max_box_area_ratio;
    int m_target_class_id;

    // --- 线程通信 ---
    DetectionThreadLauncher m_launcher;
    std::thread m_worker_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv_request;
    std::condition_variable m_cv_idle{};
    bool m_executing{false};
    bool m_cleaning{false};

    struct Request {
        FrameRef frame;
        uint64_t frame_id = 0;
        cv::Rect roi;
        std::uint64_t request_id{}, source_update_id{};
        diagnostics::FrameIdentity identity{};
        SourceComparison source{};
    } m_next_request;

    struct Plan {
        Request request{};
        std::vector<cv::Rect> regions{};
        std::size_t next{};
        std::uint64_t id{}, waiting_job{}, initial_job{};
        bool waiting{}, searching{}, fallback{};
    };
    std::optional<Plan> m_plan{};
    // At most an active, unread and locally consumed result can reference cancelled plans.
    std::array<std::pair<std::uint64_t, diagnostics::Reason>, 4> m_cancelled_plans{};
    std::size_t m_cancelled_plan_cursor{};
    DetectorControl m_control{};
    bool m_control_set{};
    std::uint64_t m_job_counter{}, m_plan_counter{};
    std::size_t m_tile_rotation{};
    unsigned m_targeted_burst{};
    std::uint64_t m_job_id{}, m_plan_id{}, m_fallback_plan_id{};
    std::uint64_t m_dispatch_attempt{}, m_dispatch_revision{};
    bool m_targeted{}, m_complete_coverage{};
    std::uint64_t m_prepare_ns{}, m_infer_ns{}, m_postprocess_ns{};

    void publishResult(const Request &request, bool has_target, const Detection &best, diagnostics::Reason reason, const std::vector<Detection> &candidates = {});
    std::uint64_t m_request_counter{}, m_inflight_id{}, m_inflight_frame{};
    diagnostics::Event m_stage{};
    std::uint32_t m_stage_index{}, m_stage_kind{};
    diagnostics::Reason m_execution_reason{};
    bool m_new_request = false;
    std::atomic<bool> m_result_ready;
    std::atomic<bool> m_stop_requested;
    YoloResult m_last_result;
};

#endif // YOLOV8DETECTOR_H
