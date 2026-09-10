#ifndef JETSON_NANOTRACK_PREPROCESSOR_H
#define JETSON_NANOTRACK_PREPROCESSOR_H

#include "preprocess/image_preprocessor.h"

/** @brief Jetson 目标级预处理，独占设备缓存并通过输入结果共享保护输出存储。 */
class JetsonNanoTrackPreprocessor final : public NanoTrackPreprocessor {
  public:
    JetsonNanoTrackPreprocessor();
    ~JetsonNanoTrackPreprocessor() override;
    PreparedInputResult prepareTemplate(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                        const ModelInputContract &contract) override;
    PreparedInputResult prepareSearch(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                      const ModelInputContract &contract) override;
    void reset() noexcept override;

  private:
    struct State;
    std::unique_ptr<State> state_;
    PreparedInputResult prepare(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                const ModelInputContract &contract, bool is_template);
};

#endif // JETSON_NANOTRACK_PREPROCESSOR_H
