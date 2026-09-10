#ifndef JETSON_YOLO_PREPROCESSOR_H
#define JETSON_YOLO_PREPROCESSOR_H

#include "preprocess/image_preprocessor.h"

/** @brief Jetson 帧快照工厂；每次成功返回独占会话，不保留源帧。 */
class JetsonYoloPreprocessor final : public YoloPreprocessor {
  public:
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &frame,
                                                                   const ModelInputContract &contract) override;
};

#endif // JETSON_YOLO_PREPROCESSOR_H
