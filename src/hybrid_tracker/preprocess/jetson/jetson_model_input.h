#ifndef JETSON_MODEL_INPUT_H
#define JETSON_MODEL_INPUT_H

#include "preprocess/model_input.h"
#include "preprocess/reusable_input.h"
#include "model/jetson/tensor_buffer.h"

/** @brief Jetson 输入只读结果，独占使用租借的缓冲区；结果销毁才允许复用。 */
class JetsonPreparedModelInput final : public PreparedModelInput {
  public:
    /** @brief 接收缓存租约和契约副本；只供 Jetson 预处理实现构造。 */
    JetsonPreparedModelInput(std::shared_ptr<TensorBuffer> storage, ModelInputContract contract);
    const ModelInputContract &contract() const noexcept override;
    /** @brief TensorRT 适配器只读借用存储，不能将指针保存到同步推理之外。 */
    const TensorBuffer &storage() const noexcept;

  private:
    std::shared_ptr<TensorBuffer> storage_;
    ModelInputContract contract_;
};

/** @brief 为 Jetson 缓存首次分配输入；检查尺寸溢出与设备分配结果。 */
std::shared_ptr<TensorBuffer> allocateJetsonInput(const ModelInputContract &contract);

#endif // JETSON_MODEL_INPUT_H
