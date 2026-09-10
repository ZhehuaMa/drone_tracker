#include "model/jetson/tensor_rt_model_executor.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include "model/jetson/jetson_storage.h"
#include "model/validation.h"

namespace model {
namespace {
class Logger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char *message) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::fprintf(stderr, "%s\n", message);
        }
    }
};
TensorShape shapeOf(const nvinfer1::Dims &dims) {
    if (dims.nbDims <= 0 || dims.nbDims > nvinfer1::Dims::MAX_DIMS) {
        return {};
    }
    return {dims.d, dims.d + dims.nbDims};
}
nvinfer1::Dims nativeShape(const TensorShape &shape) {
    nvinfer1::Dims dims{};
    if (shape.size() > static_cast<std::size_t>(nvinfer1::Dims::MAX_DIMS)) {
        throw std::invalid_argument("Tensor rank exceeds TensorRT limit");
    }
    dims.nbDims = static_cast<int>(shape.size());
    std::copy(shape.begin(), shape.end(), dims.d);
    return dims;
}
struct Stream {
    Stream() = default;
    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    cudaStream_t value{nullptr};
    ~Stream() noexcept {
        if (value) {
            cudaStreamDestroy(value);
        }
    }
};
class ContractFailure final : public std::exception {
public:
    explicit ContractFailure(Diagnostic diagnostic) : diagnostic_{std::move(diagnostic)} {}
    const Diagnostic &diagnostic() const noexcept { return diagnostic_; }

private:
    Diagnostic diagnostic_;
};
class TensorRtModelExecutor final : public ModelExecutor {
public:
    explicit TensorRtModelExecutor(const std::string &path) {
        description_.identity = path;
        if (cudaGetDevice(&device_) != cudaSuccess) {
            throw std::runtime_error("Cannot query CUDA device");
        }
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) {
            throw std::runtime_error("Cannot open engine file");
        }
        const auto size{file.tellg()};
        if (size <= 0 || static_cast<std::uintmax_t>(size) > std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error("Invalid engine file length");
        }
        std::vector<char> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            throw std::runtime_error("Incomplete engine file read");
        }
        runtime_.reset(nvinfer1::createInferRuntime(logger_));
        if (!runtime_) {
            throw std::runtime_error("Cannot create TensorRT runtime");
        }
        engine_.reset(runtime_->deserializeCudaEngine(bytes.data(), bytes.size()));
        if (!engine_) {
            throw std::runtime_error("Cannot deserialize TensorRT engine");
        }
        for (int i{0}; i < engine_->getNbIOTensors(); ++i) {
            const char *name{engine_->getIOTensorName(i)};
            if (!name) {
                throw std::runtime_error("Missing tensor name");
            }
            const auto type{engine_->getTensorDataType(name)};
            const auto format{engine_->getTensorFormat(name)};
            if (type != nvinfer1::DataType::kFLOAT || format != nvinfer1::TensorFormat::kLINEAR ||
                engine_->getTensorLocation(name) != nvinfer1::TensorLocation::kDEVICE ||
                engine_->isShapeInferenceIO(name)) {
                throw ContractFailure{{ModelError::ContractMismatch, path, name,
                                       "device Float32 LINEAR execution tensor",
                                       "type=" + std::to_string(static_cast<int>(type)) +
                                           " format=" + std::to_string(static_cast<int>(format)),
                                       "Unsupported TensorRT tensor"}};
            }
            const bool input{engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT};
            auto &specs{input ? description_.inputs : description_.outputs};
            TensorSpec spec{name, specs.size(), shapeOf(engine_->getTensorShape(name))};
            if (spec.shape.empty()) {
                throw std::runtime_error("Invalid tensor rank: " + spec.name);
            }
            if (input && std::find(spec.shape.begin(), spec.shape.end(), -1) != spec.shape.end()) {
                spec.minimum = shapeOf(engine_->getProfileShape(name, 0, nvinfer1::OptProfileSelector::kMIN));
                spec.maximum = shapeOf(engine_->getProfileShape(name, 0, nvinfer1::OptProfileSelector::kMAX));
                if (spec.minimum.size() != spec.shape.size() || spec.maximum.size() != spec.shape.size()) {
                    throw std::runtime_error("Missing optimization profile: " + spec.name);
                }
            }
            specs.push_back(std::move(spec));
        }
        if (description_.inputs.empty() || description_.outputs.empty()) {
            throw std::runtime_error("Model must have inputs and outputs");
        }
        context_.reset(engine_->createExecutionContext());
        if (!context_) {
            throw std::runtime_error("Cannot create execution context");
        }
        if (cudaStreamCreate(&stream_.value) != cudaSuccess) {
            throw std::runtime_error("Cannot create CUDA stream");
        }
    }
    const ModelDescription &description() const noexcept override { return description_; }
    ExecutionResult execute(const std::vector<Tensor> &inputs) override {
        if (!ready_) {
            return fail(ModelError::NotReady, "Executor is invalid");
        }
        int current{};
        if (cudaGetDevice(&current) != cudaSuccess || current != device_) {
            return fail(ModelError::PlatformTypeMismatch, "Executor CUDA device differs from current device");
        }
        try {
            std::vector<const JetsonStorage *> storage;
            for (const auto &input : inputs) {
                const auto *native{dynamic_cast<const JetsonStorage *>(input.storage.get())};
                if (!native || native->device() != device_) {
                    return fail(ModelError::PlatformTypeMismatch, "Expected compatible Jetson storage");
                }
                if (!native->buffer().device_ptr || shapeOf(native->buffer().dims) != input.descriptor.shape) {
                    return contractFail("input", "native shape and non-null device address matching descriptor",
                                        "native storage mismatch");
                }
                storage.push_back(native);
            }
            if (const auto error{validateInputs(inputs, description_)}) {
                return ExecutionResult::failure(error);
            }
            for (std::size_t i{0}; i < inputs.size(); ++i) {
                const auto &spec{description_.inputs[i]};
                if (!context_->setInputShape(spec.name.c_str(), nativeShape(inputs[i].descriptor.shape))) {
                    return contractFail(spec.name, "shape accepted by TensorRT", "setInputShape rejected actual shape");
                }
            }
            // 名称接口应使用 inferShapes；旧 allInputDimensionsSpecified 不覆盖 setInputShape。
            if (context_->inferShapes(0, nullptr) != 0) {
                return contractFail("outputs", "all shapes resolved", "inferShapes reports unresolved tensors");
            }
            std::vector<Tensor> outputs;
            outputs.reserve(description_.outputs.size());
            for (const auto &spec : description_.outputs) {
                const auto actual{shapeOf(context_->getTensorShape(spec.name.c_str()))};
                const auto bytes{tensorBytes(actual, spec.type, description_.identity, spec.name)};
                if (!bytes) {
                    return ExecutionResult::failure(bytes.error);
                }
                if (!continuous(spec.name, actual)) {
                    return contractFail(spec.name, "continuous output strides", "non-contiguous strides");
                }
                // 检查原生静态轴与实际输出一致；动态轴由已解析的实际尺寸替换。
                TensorSpec resolved{spec};
                for (std::size_t axis{0}; axis < resolved.shape.size(); ++axis) {
                    if (resolved.shape[axis] == -1 && axis < actual.size()) {
                        resolved.shape[axis] = actual[axis];
                    }
                }
                auto buffer{std::make_shared<TensorBuffer>(nativeShape(actual), sizeof(float))};
                Tensor output{{actual, spec.type, TensorLayout::Linear, bytes.value, {}},
                              std::make_shared<JetsonStorage>(std::move(buffer), device_)};
                if (const auto error{validateTensor(output, resolved, description_.identity)}) {
                    return ExecutionResult::failure(error);
                }
                outputs.push_back(std::move(output));
            }
            for (std::size_t i{0}; i < inputs.size(); ++i) {
                const auto &name{description_.inputs[i].name};
                if (!continuous(name, inputs[i].descriptor.shape)) {
                    return contractFail(name, "continuous input strides", "non-contiguous strides");
                }
                if (!context_->setTensorAddress(name.c_str(), storage[i]->buffer().device_ptr)) {
                    return fail(ModelError::ExecutionFailure, "Cannot bind input: " + name);
                }
            }
            for (std::size_t i{0}; i < outputs.size(); ++i) {
                const auto &native{static_cast<const JetsonStorage &>(*outputs[i].storage)};
                if (!context_->setTensorAddress(description_.outputs[i].name.c_str(), native.buffer().device_ptr)) {
                    return fail(ModelError::ExecutionFailure, "Cannot bind output: " + description_.outputs[i].name);
                }
            }
            // 提交到同步之间不进行可能抛异常的分配，所有输入与输出仍由本调用持有。
            const bool submitted{context_->enqueueV3(stream_.value)};
            const cudaError_t synchronized{cudaStreamSynchronize(stream_.value)};
            if (synchronized != cudaSuccess) {
                terminateDeviceFailure(description_.identity.c_str(), cudaGetErrorString(synchronized));
            }
            if (!submitted) {
                ready_ = false;
                return fail(ModelError::ExecutionFailure, "TensorRT submission failed; executor invalidated");
            }
            return {std::move(outputs)};
        } catch (const std::exception &error) {
            return fail(ModelError::ExecutionFailure, error.what());
        }
    }

private:
    ExecutionResult contractFail(const std::string &name, const std::string &expected,
                                 const std::string &actual) const {
        return ExecutionResult::failure({ModelError::ContractMismatch, description_.identity, name, expected, actual,
                                         "TensorRT contract mismatch"});
    }
    ExecutionResult fail(ModelError code, const std::string &message) const {
        return ExecutionResult::failure({code, description_.identity, {}, {}, {}, message});
    }
    bool continuous(const std::string &name, const TensorShape &shape) const {
        const auto strides{context_->getTensorStrides(name.c_str())};
        if (strides.nbDims != static_cast<int>(shape.size())) {
            return false;
        }
        std::size_t stride{1};
        for (std::size_t i{shape.size()}; i > 0; --i) {
            if (strides.d[i - 1] < 0 || static_cast<std::size_t>(strides.d[i - 1]) != stride) {
                return false;
            }
            stride *= static_cast<std::size_t>(shape[i - 1]);
        }
        return true;
    }
    ModelDescription description_;
    Logger logger_;
    std::unique_ptr<nvinfer1::IRuntime> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;
    Stream stream_{};
    int device_{};
    bool ready_{true};
};
} // namespace
LoadResult loadTensorRtModel(const std::string &path) {
    try {
        return {std::make_unique<TensorRtModelExecutor>(path)};
    } catch (const ContractFailure &error) {
        return LoadResult::failure(error.diagnostic());
    } catch (const std::exception &error) {
        return LoadResult::failure({ModelError::LoadFailure, path, {}, {}, {}, error.what()});
    }
}
} // namespace model
