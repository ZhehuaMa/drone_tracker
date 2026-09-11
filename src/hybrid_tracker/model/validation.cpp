#include "model/validation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace model {
namespace {
std::string shapeText(const TensorShape &shape) {
    std::ostringstream text;
    text << '[';
    for (const auto extent : shape) {
        text << extent << ',';
    }
    return text.str() + ']';
}
Diagnostic contractMismatch(const std::string &identity, const std::string &name, std::string expected,
                            std::string actual) {
    return {ModelError::ContractMismatch, identity,          name,
            std::move(expected),          std::move(actual), "Tensor contract mismatch"};
}
} // namespace
std::string describe(const Diagnostic &diagnostic) {
    return diagnostic.model + ": " + diagnostic.message + " [" + diagnostic.tensor +
           "] expected=" + diagnostic.expected + " actual=" + diagnostic.actual;
}
Result<std::size_t> tensorBytes(const TensorShape &shape, TensorElementType type, const std::string &identity,
                                const std::string &name) {
    if (type != TensorElementType::Float32) {
        return Result<std::size_t>::failure(
            contractMismatch(identity, name, "Float32", std::to_string(static_cast<int>(type))));
    }
    std::size_t bytes{sizeof(float)};
    if (shape.empty()) {
        return Result<std::size_t>::failure(contractMismatch(identity, name, "positive rank", "0"));
    }
    for (const auto extent : shape) {
        if (extent <= 0 || bytes > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(extent)) {
            return Result<std::size_t>::failure(
                contractMismatch(identity, name, "resolved positive shape without overflow", shapeText(shape)));
        }
        bytes *= static_cast<std::size_t>(extent);
    }
    return {bytes};
}
Diagnostic validateDescriptor(const TensorDescriptor &descriptor, const std::string &identity,
                              const std::string &name) {
    const auto bytes{tensorBytes(descriptor.shape, descriptor.type, identity, name)};
    if (!bytes) {
        return bytes.error;
    }
    if ((descriptor.layout != TensorLayout::Linear && descriptor.layout != TensorLayout::Nchw) ||
        descriptor.quantization) {
        return contractMismatch(identity, name, "continuous unquantized Float32", "unsupported layout or quantization");
    }
    if (descriptor.bytes != bytes.value) {
        return contractMismatch(identity, name, std::to_string(bytes.value) + " bytes",
                                std::to_string(descriptor.bytes));
    }
    return {};
}
Diagnostic validateTensor(const Tensor &tensor, const TensorSpec &spec, const std::string &identity) {
    const std::string name{spec.name + "#" + std::to_string(spec.index)};
    if (const auto error{validateDescriptor(tensor.descriptor, identity, name)}) {
        return error;
    }
    const auto &actual{tensor.descriptor};
    if (spec.type != actual.type || (spec.layout != TensorLayout::Linear && spec.layout != TensorLayout::Nchw) ||
        (spec.layout == TensorLayout::Nchw && actual.layout != TensorLayout::Nchw)) {
        return contractMismatch(identity, name, "matching type and continuous layout", "incompatible type/layout");
    }
    if (actual.shape.size() != spec.shape.size()) {
        return contractMismatch(identity, name, shapeText(spec.shape), shapeText(actual.shape));
    }
    for (std::size_t axis{0}; axis < spec.shape.size(); ++axis) {
        if (spec.shape[axis] == -1) {
            if (spec.minimum.size() != spec.shape.size() || spec.maximum.size() != spec.shape.size() ||
                spec.minimum[axis] <= 0 || spec.maximum[axis] < spec.minimum[axis] ||
                actual.shape[axis] < spec.minimum[axis] || actual.shape[axis] > spec.maximum[axis]) {
                return contractMismatch(identity, name, shapeText(spec.minimum) + ".." + shapeText(spec.maximum),
                                        shapeText(actual.shape));
            }
        } else if (spec.shape[axis] != actual.shape[axis]) {
            return contractMismatch(identity, name, shapeText(spec.shape), shapeText(actual.shape));
        }
    }
    if (!tensor.storage || !tensor.storage->hostData() || tensor.storage->byteSize() != actual.bytes) {
        return contractMismatch(identity, name, std::to_string(actual.bytes) + " owned readable bytes",
                                tensor.storage ? std::to_string(tensor.storage->byteSize()) : "missing storage");
    }
    return {};
}
Diagnostic validateInputs(const std::vector<Tensor> &inputs, const ModelDescription &description) {
    if (inputs.size() != description.inputs.size()) {
        return contractMismatch(description.identity, "inputs", std::to_string(description.inputs.size()),
                                std::to_string(inputs.size()));
    }
    for (std::size_t i{0}; i < inputs.size(); ++i) {
        if (description.inputs[i].index != i) {
            return contractMismatch(description.identity, description.inputs[i].name, "native input order",
                                    "invalid index");
        }
        if (const auto error{validateTensor(inputs[i], description.inputs[i], description.identity)}) {
            return error;
        }
    }
    return {};
}
Result<int> yoloClassCount(const TensorDescriptor &descriptor, const std::string &identity) {
    if (const auto error{validateDescriptor(descriptor, identity, "YOLO")}) {
        return Result<int>::failure(error);
    }
    const auto &shape{descriptor.shape};
    if (shape.size() != 3 || shape[0] != 1 || shape[1] < 5 || shape[2] <= 0) {
        return Result<int>::failure(contractMismatch(identity, "YOLO", "[1,4+C,N], C>0, N>0", shapeText(shape)));
    }
    return {shape[1] - 4};
}
Result<YoloPrediction> yoloPrediction(const std::vector<Tensor> &outputs, const std::string &identity) {
    if (outputs.empty()) {
        return Result<YoloPrediction>::failure(contractMismatch(identity, "YOLO", "first prediction output", "none"));
    }
    const auto &tensor{outputs.front()};
    TensorSpec spec{"YOLO", 0, tensor.descriptor.shape};
    if (const auto error{validateTensor(tensor, spec, identity)}) {
        return Result<YoloPrediction>::failure(error);
    }
    const auto count{yoloClassCount(tensor.descriptor, identity)};
    if (!count) {
        return Result<YoloPrediction>::failure(count.error);
    }
    return {{tensor, 1, 2}};
}
Result<NanoPrediction> nanoPrediction(const std::vector<Tensor> &outputs, const std::string &identity) {
    if (outputs.size() != 2) {
        return Result<NanoPrediction>::failure(
            contractMismatch(identity, "Head", "two outputs", std::to_string(outputs.size())));
    }
    NanoPrediction prediction;
    for (const auto &tensor : outputs) {
        TensorSpec spec{"Head output", 0, tensor.descriptor.shape};
        if (const auto error{validateTensor(tensor, spec, identity)}) {
            return Result<NanoPrediction>::failure(error);
        }
        if (tensor.descriptor.shape == TensorShape{1, 2, 15, 15} && !prediction.classification.storage) {
            prediction.classification = tensor;
        } else if (tensor.descriptor.shape == TensorShape{1, 4, 15, 15} && !prediction.location.storage) {
            prediction.location = tensor;
        } else {
            return Result<NanoPrediction>::failure(contractMismatch(
                identity, "Head", "unique classification and location", shapeText(tensor.descriptor.shape)));
        }
    }
    if (!prediction.classification.storage || !prediction.location.storage) {
        return Result<NanoPrediction>::failure(contractMismatch(identity, "Head", "both roles", "missing role"));
    }
    return {std::move(prediction)};
}
[[noreturn]] void terminateDeviceFailure(const char *identity, const char *reason) noexcept {
    std::fprintf(stderr, "Fatal model device failure [%s]: %s\n", identity, reason);
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}
} // namespace model
