#ifndef MODEL_JETSON_STORAGE_H
#define MODEL_JETSON_STORAGE_H

#include "model/model_backend.h"
#include "model/jetson/tensor_buffer.h"

namespace model {
/** @brief 持有映射存储或准备输入租约，仅 Jetson 内部可取得设备地址。 */
class JetsonStorage final : public TensorStorage {
public:
    explicit JetsonStorage(std::shared_ptr<const TensorBuffer> buffer, int device)
        : buffer_{std::move(buffer)}, device_{device} {}
    const float *hostData() const noexcept override { return static_cast<const float *>(buffer_->host_ptr); }
    std::size_t byteSize() const noexcept override { return buffer_->size; }
    const TensorBuffer &buffer() const noexcept { return *buffer_; }
    int device() const noexcept { return device_; }

private:
    std::shared_ptr<const TensorBuffer> buffer_;
    int device_;
};
Result<Tensor> adaptJetsonInput(const PreparedInputRef &input, const std::string &identity);
} // namespace model
#endif // MODEL_JETSON_STORAGE_H
