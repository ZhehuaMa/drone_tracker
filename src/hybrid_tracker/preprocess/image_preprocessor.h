#ifndef PREPROCESS_IMAGE_PREPROCESSOR_H
#define PREPROCESS_IMAGE_PREPROCESSOR_H

#include "frame/frame.h"
#include "model_input.h"
#include "preprocess_geometry.h"

/** @brief 区域输入及其坐标变换的自有结果；input 共享持有存储租约。 */
struct YoloPreparedRegion {
    PreparedInputRef input{};
    LetterboxTransform transform{};
};

/**
 * @brief 一次 YOLO 请求的独立帧快照，调用方独占持有。
 *
 * 按 ROI、全图、切片顺序调用 prepare。每次同步推理结束并释放结果后，
 * 才能准备下一区域；否则返回 InputBusy。方法不得被并发调用。
 */
class YoloFrameSession {
  public:
    /** @brief 释放快照；已返回的模型输入仍由其自身租约保护。 */
    virtual ~YoloFrameSession() = default;
    /** @brief 返回快照尺寸的副本，不转移图像资源。 */
    virtual cv::Size sourceSize() const noexcept = 0;
    /**
     * @brief 借用区域参数并返回受租约保护的模型输入。
     * @param region 默认 cv::Rect{} 表示全图；其他区域必须为正尺寸且完全在帧内。
     * @return 失败时无模型输入，旧结果不被修改，会话可继续处理其他合法区域。
     */
    virtual PreprocessResult<YoloPreparedRegion> prepare(const cv::Rect &region = {}) = 0;
};

/** @brief YOLO 快照工厂；由检测器独占拥有，方法不得并发调用。 */
class YoloPreprocessor {
  public:
    /** @brief 释放工厂资源，不使已返回会话或输入失效。 */
    virtual ~YoloPreprocessor() = default;
    /**
     * @brief 借用帧及契约，完成隔离复制后转移新会话的独占所有权。
     * @return 成功会话不再持有源帧；失败不返回会话，也不触发推理。
     */
    virtual PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &frame,
                                                                           const ModelInputContract &contract) = 0;
};

/**
 * @brief 一个 NanoTrack 目标的预处理状态，由跟踪器独占持有。
 *
 * 顺序为 reset、prepareTemplate、零到多次 prepareSearch；不得并发调用。
 * 每次同步推理后释放输入结果。相同缓存的旧结果存活时返回 InputBusy。
 */
class NanoTrackPreprocessor {
  public:
    /** @brief 释放目标状态；已返回输入仍由自身租约保护。 */
    virtual ~NanoTrackPreprocessor() = default;
    /**
     * @brief 借用帧、中心与契约，计算当前目标均值并返回 127×127 模板输入。
     * @return 失败不返回输入且使模板状态失效，之后必须重新准备模板。
     */
    virtual PreparedInputResult prepareTemplate(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                                const ModelInputContract &contract) = 0;
    /**
     * @brief 借用参数，使用成功模板的均值准备 255×255 搜索输入。
     * @return 无模板时返回 ContractMismatch；失败不得修改目标状态或旧输入。
     */
    virtual PreparedInputResult prepareSearch(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                              const ModelInputContract &contract) = 0;
    /** @brief 使目标均值及填充缓存失效；不使消费者已持有的输入失效。 */
    virtual void reset() noexcept = 0;
};

#endif // PREPROCESS_IMAGE_PREPROCESSOR_H
