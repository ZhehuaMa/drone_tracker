#ifndef DRONE_TRACKER_DIAGNOSTIC_TEST_ACCESS_H
#define DRONE_TRACKER_DIAGNOSTIC_TEST_ACCESS_H
#include "hybrid_tracker.h"
#include "yolo_detector.h"
#include <array>
/** @brief 仅测试产物提供定义；读取原检测锁内的空闲状态，避免睡眠猜测异步完成。 */
struct HybridTrackerDiagnosticTestAccess {
    /** @brief Bounded handoff after the caller snapshot; compiled only into test-enabled builds. */
    static thread_local std::function<void()> snapshot_hook;
    static void afterSnapshot();
    static void jump(HybridTracker &tracker, int counter, float position, float size);
    static void prime(HybridTracker &tracker, const FrameRef &frame, int lost = 0, int watchdog = 0);
    static void result(HybridTracker &tracker, YoloResult result);
    static std::array<int, 3> counters(const HybridTracker &tracker);
    static void skipSubmission(HybridTracker &tracker);
    static bool idle(const HybridTracker &tracker);
    /** @brief Observe a completed, unread result under the detector lock without consuming it. */
    static bool unreadResult(const YoloV8Detector &detector, YoloResult &result);
};
#endif // DRONE_TRACKER_DIAGNOSTIC_TEST_ACCESS_H
