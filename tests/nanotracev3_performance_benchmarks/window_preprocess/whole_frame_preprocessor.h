#ifndef WINDOW_WHOLE_FRAME_PREPROCESSOR_H
#define WINDOW_WHOLE_FRAME_PREPROCESSOR_H

#include "platform/rk3576/nanotrack_preprocessor.h"

namespace window_preprocess {
/** @brief 灰度裁剪先转浮点再缩放；模板与搜索各持有一个受租约保护的缓存。 */
class WholeFramePreprocessor final : public ::NanoTrackPreprocessor {
public:
    PreparedInputResult prepareTemplate(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                        const ModelInputContract &contract) override;
    PreparedInputResult prepareSearch(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                      const ModelInputContract &contract) override;
    void reset() noexcept override;

private:
    PreparedInputResult prepare(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                const ModelInputContract &contract, bool is_template);
    TargetMean mean_{};
    std::array<ReusableInput<std::vector<float>>, 2> cache_{};
};
} // namespace window_preprocess
#endif // WINDOW_WHOLE_FRAME_PREPROCESSOR_H
