#ifndef DRONE_TRACKER_DIAGNOSTIC_TEST_ACCESS_H
#define DRONE_TRACKER_DIAGNOSTIC_TEST_ACCESS_H
#include "hybrid_tracker.h"
/** @brief 仅测试产物提供定义；读取原检测锁内的空闲状态，避免睡眠猜测异步完成。 */
struct HybridTrackerDiagnosticTestAccess {
    static void jump(HybridTracker &tracker, int counter, float position, float size);
    static bool idle(const HybridTracker &tracker);
};
#endif // DRONE_TRACKER_DIAGNOSTIC_TEST_ACCESS_H
