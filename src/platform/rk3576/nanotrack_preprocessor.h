#ifndef RK3576_NANOTRACK_PREPROCESSOR_H
#define RK3576_NANOTRACK_PREPROCESSOR_H

#include <array>
#include <stdexcept>

#include "preprocess/image_preprocessor.h"
#include "preprocess/reusable_input.h"
#include "preprocess/target_mean.h"

namespace rk3576 {
/** @brief 明确拥有 Float32 三平面存储的准备结果。 */
class FloatInput final : public PreparedModelInput {
public:
    FloatInput(std::shared_ptr<const std::vector<float>> values, ModelInputContract contract)
        : values_{std::move(values)}, contract_{std::move(contract)} {
        if (!values_) {
            throw std::invalid_argument("FloatInput requires storage");
        }
    }
    const ModelInputContract &contract() const noexcept override { return contract_; }
    const std::shared_ptr<const std::vector<float>> &values() const noexcept { return values_; }

private:
    std::shared_ptr<const std::vector<float>> values_;
    ModelInputContract contract_;
};
/**
 * @brief 同一实例仍须串行调用；模板与搜索复用一个单通道 Float32 小窗。
 * 源映射只在复制有效区域时存活，缩放前释放。小窗每次完整写入或补齐，返回后可重写。
 * 消费者持有的 FloatInput 使用独立三平面存储，由模板/搜索各自的缓存租约保护。
 */
class NanoTrackPreprocessor final : public ::NanoTrackPreprocessor {
public:
    PreparedInputResult prepareTemplate(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                        const ModelInputContract &contract) override;
    PreparedInputResult prepareSearch(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                      const ModelInputContract &contract) override;
    void reset() noexcept override;

private:
    PreparedInputResult prepare(const FrameRef &frame, const cv::Point2f &position, int original_size,
                                const ModelInputContract &contract, bool is_template);
    cv::Mat window_{}; // 临时工作区，不借给消费者，也不持有源帧。
    TargetMean mean_{};
    std::array<ReusableInput<std::vector<float>>, 2> cache_{};
};
} // namespace rk3576
#endif // RK3576_NANOTRACK_PREPROCESSOR_H
