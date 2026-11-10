#ifndef DRONE_TRACKER_TRACKING_VALIDATORS_H
#define DRONE_TRACKER_TRACKING_VALIDATORS_H

#include <opencv2/core.hpp>

namespace tracking {
struct ValidationResult {
    bool allow{true};
    const char *status{"not_evaluated"};
};
// Calibration and labelled quality evaluation are deferred. Structural checks live at their callers.
inline ValidationResult appearanceValidation(float score) {
    (void)score;
    return {};
}
inline ValidationResult motionValidation(const cv::Rect2f &measurement, const cv::Rect2f &reference) {
    (void)measurement; (void)reference;
    return {};
}
} // namespace tracking
#endif // DRONE_TRACKER_TRACKING_VALIDATORS_H
