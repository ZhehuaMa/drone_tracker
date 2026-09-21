#include "feature_transfer.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "platform/rk3576/nanotrack_preprocessor.h"
#include "preprocess/input_validation.h"

namespace search_direct {
namespace {
class FloatStorage final : public model::TensorStorage {
public:
    explicit FloatStorage(std::size_t size) : values(size) {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};
} // namespace

model::Tensor toNchw(const model::Tensor &source) {
    if (source.descriptor.layout == TensorLayout::Nchw) {
        return source;
    }
    const auto &s{source.descriptor.shape};
    const int height{s[1]};
    const int width{s[2]};
    const int channels{s[3]};
    auto storage{std::make_shared<FloatStorage>(source.descriptor.bytes / sizeof(float))};
    const auto view{source.read()};
    for (int n{0}; n < s[0]; ++n) {
        for (int c{0}; c < channels; ++c) {
            for (int y{0}; y < height; ++y) {
                for (int x{0}; x < width; ++x) {
                    storage->values[((n * channels + c) * height + y) * width + x] =
                        view.data()[((n * height + y) * width + x) * channels + c];
                }
            }
        }
    }
    return {
        {{s[0], channels, height, width}, TensorElementType::Float32, TensorLayout::Nchw, source.descriptor.bytes, {}},
        std::move(storage)};
}

rk3576::SessionInput submission(const model::Tensor &source, const rknn_tensor_attr &attribute) {
    const auto &s{source.descriptor.shape};
    if (attribute.fmt == RKNN_TENSOR_NCHW) {
        return {attribute.index, source.descriptor, source.storage->hostData(), source.descriptor.bytes,
                source.storage};
    }
    auto storage{std::make_shared<FloatStorage>(source.descriptor.bytes / sizeof(float))};
    const int channels{s[1]};
    const int height{s[2]};
    const int width{s[3]};
    const auto view{source.read()};
    for (int n{0}; n < s[0]; ++n) {
        for (int y{0}; y < height; ++y) {
            for (int x{0}; x < width; ++x) {
                for (int c{0}; c < channels; ++c) {
                    storage->values[((n * height + y) * width + x) * channels + c] =
                        view.data()[((n * channels + c) * height + y) * width + x];
                }
            }
        }
    }
    return {
        attribute.index,
        {{s[0], height, width, channels}, TensorElementType::Float32, TensorLayout::Nhwc, source.descriptor.bytes, {}},
        storage->hostData(),
        source.descriptor.bytes,
        storage};
}

model::Result<model::Tensor> imageTensor(const PreparedInputRef &input, const std::string &identity, int size) {
    using Result = model::Result<model::Tensor>;
    const auto floating{std::dynamic_pointer_cast<const rk3576::FloatInput>(input)};
    if (!floating) {
        return Result::failure({model::ModelError::PlatformTypeMismatch, identity, "input#0", "FloatInput",
                                "foreign storage", "Incompatible prepared input"});
    }
    if (!matchesImageContract(floating->contract(), 1.0f, size) ||
        floating->values()->size() != static_cast<std::size_t>(3 * size * size)) {
        return Result::failure({model::ModelError::ContractMismatch, identity, "input#0",
                                "Float32 NCHW [1,3," + std::to_string(size) + "," + std::to_string(size) + "]",
                                "elements=" + std::to_string(floating->values()->size()), "Invalid image input"});
    }
    auto storage{std::make_shared<FloatStorage>(floating->values()->size())};
    std::copy(floating->values()->begin(), floating->values()->end(), storage->values.begin());
    model::Tensor tensor{
        {floating->contract().shape, TensorElementType::Float32, TensorLayout::Nchw, storage->byteSize(), {}},
        std::move(storage)};
    return {std::move(tensor)};
}

float halfToFloat(std::uint16_t bits) {
    const std::uint32_t sign{static_cast<std::uint32_t>(bits & 0x8000U) << 16};
    const std::uint32_t exponent{static_cast<std::uint32_t>(bits >> 10) & 0x1FU};
    const std::uint32_t mantissa{static_cast<std::uint32_t>(bits) & 0x3FFU};
    std::uint32_t result{};
    if (exponent == 0) {
        if (mantissa == 0) {
            result = sign;
        } else {
            // 次正规数：左移尾数直到出现隐含的最高位，再按左移次数修正指数。
            int shifts{-1};
            std::uint32_t normalized{mantissa};
            do {
                normalized <<= 1;
                ++shifts;
            } while ((normalized & 0x400U) == 0);
            result = sign | (static_cast<std::uint32_t>(127 - 15 - shifts) << 23) | ((normalized & 0x3FFU) << 13);
        }
    } else if (exponent == 0x1F) {
        result = sign | 0x7F800000U | (mantissa << 13);
    } else {
        result = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float value{};
    std::memcpy(&value, &result, sizeof(value));
    return value;
}
} // namespace search_direct
