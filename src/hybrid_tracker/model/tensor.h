#ifndef MODEL_TENSOR_H
#define MODEL_TENSOR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

/** @brief 元素编码；设备实现必须明确拒绝不支持的类型。 */
enum class TensorElementType { Float32, Float16, Int8, Uint8, Int32, Unknown };
/** @brief 连续存储的轴顺序；Linear 不给轴附加图像语义。 */
enum class TensorLayout { Nchw, Linear, Unsupported };
using TensorShape = std::vector<std::int32_t>;

namespace model {
/** @brief 整数量化描述，与预处理的像素乘加变换相互独立。 */
struct Quantization {
    std::vector<float> scales{};
    std::vector<std::int32_t> zero_points{};
    std::optional<std::size_t> axis{};
};
/** @brief 一次实际数据的属性；shape 必须为已解析的正尺寸。 */
struct TensorDescriptor {
    TensorShape shape{};
    TensorElementType type{TensorElementType::Float32};
    TensorLayout layout{TensorLayout::Linear};
    std::size_t bytes{};
    std::optional<Quantization> quantization{};
};
/** @brief 存储所有者；这里只允许读取主机地址，设备地址留在平台实现内。 */
class TensorStorage {
public:
    virtual ~TensorStorage() = default;
    virtual const float *hostData() const noexcept = 0;
    virtual std::size_t byteSize() const noexcept = 0;
};
/** @brief 持有存储的只读主机视图；data() 的借用有效期受本对象约束。 */
class HostTensorView {
public:
    explicit HostTensorView(std::shared_ptr<const TensorStorage> storage) : storage_{std::move(storage)} {}
    const float *data() const noexcept { return storage_ ? storage_->hostData() : nullptr; }
    std::size_t size() const noexcept { return storage_ ? storage_->byteSize() / sizeof(float) : 0; }

private:
    std::shared_ptr<const TensorStorage> storage_;
};
/** @brief 输入和输出共用的只读所有权引用，可以比执行器活得更久。 */
struct Tensor {
    TensorDescriptor descriptor{};
    std::shared_ptr<const TensorStorage> storage{};
    HostTensorView read() const { return HostTensorView{storage}; }
};
} // namespace model
#endif // MODEL_TENSOR_H
