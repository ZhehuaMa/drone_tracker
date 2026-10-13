#include "recovery.h"

#include <sstream>

namespace window_preprocess {
PerformanceVerdict performanceVerdict(const Distribution &old_recovery, const Distribution &new_recovery,
                                      const Distribution &old_preprocess, const Distribution &new_preprocess) {
    const bool passed{new_preprocess.median <= old_preprocess.median * 0.2
        && new_recovery.median < old_recovery.median && new_recovery.p95 <= old_recovery.p95};
    std::ostringstream report;
    report << "指定录像重建恢复对照：" << (passed ? "通过" : "失败") << "\n单位：纳秒\n"
           << "旧恢复中位数=" << old_recovery.median << " 新恢复中位数=" << new_recovery.median
           << "\n旧恢复P95=" << old_recovery.p95 << " 新恢复P95=" << new_recovery.p95
           << "\n旧预处理中位数=" << old_preprocess.median << " 新预处理中位数=" << new_preprocess.median
           << "\n预处理比例=" << new_preprocess.median / old_preprocess.median
           << "\n仅此输入与窗口；不代表现场约 388 ms 的分项耗时。\n";
    return {passed, report.str(), passed ? "通过\n" : "性能门槛未通过\n", passed ? 0 : 1};
}
} // namespace window_preprocess
