#ifndef RK3576_YOLO_PREPROCESSOR_H
#define RK3576_YOLO_PREPROCESSOR_H

#include <stdexcept>

#include "preprocess/image_preprocessor.h"

namespace rk3576 {
/** @brief UINT8 NHWC 实际提交契约；网络内部使用 mean=0/std=255。 */
inline ModelInputContract yoloContract() {
    return {{1, 640, 640, 3}, TensorElementType::Uint8, TensorLayout::Nhwc, 1.0f, 0.0f};
}
/** @brief 平台私有字节输入，持有缓存租约；不伪装成 Float32 TensorStorage。 */
class ByteInput final : public PreparedModelInput {
public:
    explicit ByteInput(std::shared_ptr<const std::vector<std::uint8_t>> bytes) : bytes_{std::move(bytes)} {
        if (!bytes_) {
            throw std::invalid_argument("ByteInput requires storage");
        }
    }
    const ModelInputContract &contract() const noexcept override { return contract_; }
    const std::vector<std::uint8_t> &bytes() const noexcept { return *bytes_; }

private:
    ModelInputContract contract_{yoloContract()};
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
};
/** @brief 只读映射期间复制 Gray8 快照，再以 OpenCV 准备区域。 */
class YoloPreprocessor final : public ::YoloPreprocessor {
public:
    PreprocessResult<std::unique_ptr<YoloFrameSession>> beginFrame(const FrameRef &frame,
                                                                   const ModelInputContract &contract) override;
};
} // namespace rk3576
#endif // RK3576_YOLO_PREPROCESSOR_H
