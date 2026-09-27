#ifndef PLATFORM_FRAME_SOURCE_OPS_H
#define PLATFORM_FRAME_SOURCE_OPS_H

#include <functional>

#include "platform/platform_factory.h"

/** @brief 启动前的录像参数；由公共调用方生成，帧源决定是否实际录像。 */
struct FrameRecordingConfig {
    bool enabled{};
    std::string directory{};
    std::string prefix{};
};

/** @brief Jetson 旧相机录像边界；RK3576 节点的 need_save 只进入 bag，不调用此函数。 */
inline void configureFrameSourceRecording(FrameSource &source, const FrameRecordingConfig &config) {
    source.configureRecording(config.enabled, config.directory, config.prefix);
}

/**
 * @brief 限时等待首帧，随后排空积压并返回最新帧；必须由原单一消费者调用。
 * @param on_first_frame 首帧到达后、排空之前调用，用于保留节点的处理计时起点。
 * @return 超时返回 false，不执行回调或非阻塞取帧，frame 保持帧源的等待结果。
 */
inline bool waitForLatestFrame(FrameSource &source, FrameRef &frame, std::chrono::microseconds timeout,
                              const std::function<void()> &on_first_frame = {}) {
    if (!source.waitFrame(frame, timeout)) {
        return false;
    }
    if (on_first_frame) {
        on_first_frame();
    }
    FrameRef newer{};
    while (source.tryFrame(newer)) {
        frame = std::move(newer);
    }
    return true;
}

#endif // PLATFORM_FRAME_SOURCE_OPS_H
