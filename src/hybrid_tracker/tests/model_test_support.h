#ifndef MODEL_TEST_SUPPORT_H
#define MODEL_TEST_SUPPORT_H

#include <stdexcept>
#include "model/validation.h"

namespace model_test {
class Storage final : public model::TensorStorage {
public:
    explicit Storage(std::size_t count, float value) : values_(count, value) {}
    const float *hostData() const noexcept override { return values_.data(); }
    std::size_t byteSize() const noexcept override { return values_.size() * sizeof(float); }

private:
    std::vector<float> values_;
};
inline model::Tensor tensor(TensorShape shape, float value = 0.0f) {
    const auto bytes{model::tensorBytes(shape, TensorElementType::Float32, "fixture", "tensor")};
    if (!bytes) {
        throw std::invalid_argument(model::describe(bytes.error));
    }
    return {{std::move(shape), TensorElementType::Float32, TensorLayout::Linear, bytes.value, {}},
            std::make_shared<Storage>(bytes.value / sizeof(float), value)};
}
inline void require(bool success, const char *message) {
    if (!success) {
        throw std::runtime_error(message);
    }
}
} // namespace model_test
#endif // MODEL_TEST_SUPPORT_H
