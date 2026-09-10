#ifndef PREPROCESS_TARGET_MEAN_H
#define PREPROCESS_TARGET_MEAN_H

#include <exception>
#include "frame/frame.h"
#include <opencv2/core.hpp>

/** @brief 目标级填充值；reset 立即使旧均值失效，模板成功后才允许搜索使用。 */
class TargetMean {
  public:
    void reset() noexcept {
        valid_ = false;
        value_ = 114;
    }
    void initialize(const FrameRef &frame) {
        reset();
        // mapRead 失败按既定语义使用 114；视图离开作用域后才获取设备读租约。
        try {
            const auto view{frame->mapRead()};
            if (view && !view->mat().empty()) {
                value_ = cv::saturate_cast<unsigned char>(cv::mean(view->mat())[0]);
            }
        } catch (const std::exception &) {
            value_ = 114;
        }
    }
    void commit() noexcept { valid_ = true; }
    bool valid() const noexcept { return valid_; }
    unsigned char value() const noexcept { return value_; }

  private:
    bool valid_{false};
    unsigned char value_{114};
};

#endif // PREPROCESS_TARGET_MEAN_H
