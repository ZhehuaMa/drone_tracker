#include "model/yolo_output_adapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "model/validation.h"

namespace model {
namespace {
constexpr std::array<int, 3> kSizes{80, 40, 20};
constexpr std::size_t kCandidates{8400};
class OwnedFloats final : public TensorStorage {
public:
    explicit OwnedFloats(std::size_t count) : values(count) {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};
Diagnostic mismatch(const std::string &identity, std::size_t index, const std::string &actual) {
    return {ModelError::ContractMismatch,
            identity,
            "output#" + std::to_string(index),
            "six finite NCHW outputs: box64/classesC at 80,40,20",
            actual,
            "Invalid YOLO output"};
}
} // namespace
Result<int> yoloSixOutputClasses(const std::vector<TensorDescriptor> &outputs, const std::string &identity) {
    if (outputs.size() != 6) {
        return Result<int>::failure(mismatch(identity, 0, "count=" + std::to_string(outputs.size())));
    }
    int classes{0};
    for (std::size_t i{0}; i < outputs.size(); ++i) {
        const auto &d{outputs[i]};
        if (const auto error{validateDescriptor(d, identity, "output#" + std::to_string(i))}) {
            return Result<int>::failure(error);
        }
        const auto &s{d.shape};
        if (s.size() != 4 || s[0] != 1 || s[2] != kSizes[i / 2] || s[3] != kSizes[i / 2] ||
            d.layout != TensorLayout::Nchw || (i % 2 == 0 && s[1] != 64)) {
            return Result<int>::failure(mismatch(identity, i, "shape/layout"));
        }
        if (i == 1) {
            classes = s[1];
        }
        if (i % 2 == 1 && s[1] != classes) {
            return Result<int>::failure(mismatch(identity, i, "inconsistent C=" + std::to_string(s[1])));
        }
    }
    if (classes > std::numeric_limits<std::int32_t>::max() - 4) {
        return Result<int>::failure(mismatch(identity, 1, "attribute axis overflow"));
    }
    const auto bytes{tensorBytes({1, classes + 4, 8400}, TensorElementType::Float32, identity, "prediction")};
    if (!bytes) {
        return Result<int>::failure(bytes.error);
    }
    return {classes};
}
Result<YoloPrediction> adaptYoloSixOutputs(const std::vector<Tensor> &outputs, const std::string &identity) {
    std::vector<TensorDescriptor> descriptors;
    for (const auto &tensor : outputs) {
        descriptors.push_back(tensor.descriptor);
    }
    const auto classes{yoloSixOutputClasses(descriptors, identity)};
    if (!classes) {
        return Result<YoloPrediction>::failure(classes.error);
    }
    for (std::size_t i{0}; i < outputs.size(); ++i) {
        if (const auto error{validateTensor(outputs[i], {"output", i, descriptors[i].shape}, identity)}) {
            return Result<YoloPrediction>::failure(error);
        }
        const auto view{outputs[i].read()};
        if (!std::all_of(view.data(), view.data() + view.size(), [](float v) { return std::isfinite(v); })) {
            return Result<YoloPrediction>::failure(mismatch(identity, i, "NaN/Inf"));
        }
    }
    auto storage{std::make_shared<OwnedFloats>((static_cast<std::size_t>(classes.value) + 4) * kCandidates)};
    auto &result{storage->values};
    std::size_t offset{0};
    for (std::size_t scale{0}; scale < kSizes.size(); ++scale) {
        const int size{kSizes[scale]};
        const std::size_t cells{static_cast<std::size_t>(size * size)};
        const float stride{640.0f / size};
        const auto box{outputs[2 * scale].read()};
        const auto scores{outputs[2 * scale + 1].read()};
        for (std::size_t cell{0}; cell < cells; ++cell) {
            std::array<double, 4> distances{};
            for (std::size_t side{0}; side < 4; ++side) {
                double maximum{-std::numeric_limits<double>::infinity()};
                for (std::size_t bin{0}; bin < 16; ++bin) {
                    maximum = std::max(maximum, static_cast<double>(box.data()[(side * 16 + bin) * cells + cell]));
                }
                double sum{0};
                double weighted{0};
                for (std::size_t bin{0}; bin < 16; ++bin) {
                    const double weight{std::exp(box.data()[(side * 16 + bin) * cells + cell] - maximum)};
                    sum += weight;
                    weighted += bin * weight;
                }
                distances[side] = weighted / sum;
            }
            const double x{static_cast<double>(cell % size) + 0.5};
            const double y{static_cast<double>(cell / size) + 0.5};
            result[offset + cell] = static_cast<float>((x + (distances[2] - distances[0]) / 2) * stride);
            result[kCandidates + offset + cell] = static_cast<float>((y + (distances[3] - distances[1]) / 2) * stride);
            result[2 * kCandidates + offset + cell] = static_cast<float>((distances[0] + distances[2]) * stride);
            result[3 * kCandidates + offset + cell] = static_cast<float>((distances[1] + distances[3]) * stride);
            for (std::size_t c{0}; c < static_cast<std::size_t>(classes.value); ++c) {
                const double logit{scores.data()[c * cells + cell]};
                const double exp{std::exp(-std::abs(logit))};
                result[(4 + c) * kCandidates + offset + cell] =
                    static_cast<float>(logit >= 0 ? 1 / (1 + exp) : exp / (1 + exp));
            }
        }
        offset += cells;
    }
    if (!std::all_of(result.begin(), result.end(), [](float v) { return std::isfinite(v); })) {
        return Result<YoloPrediction>::failure(mismatch(identity, 0, "nonfinite prediction"));
    }
    return {{{{{1, classes.value + 4, 8400}, TensorElementType::Float32, TensorLayout::Linear, storage->byteSize(), {}},
              storage},
             1,
             2}};
}
} // namespace model
