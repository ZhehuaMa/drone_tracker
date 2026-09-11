#include "model/jetson/jetson_model_backend.h"

#include <stdexcept>
#include "model/validation.h"

namespace model {
namespace {
Diagnostic contractError(const std::string &identity, const std::string &reason) {
    return {ModelError::ContractMismatch, identity, "model inputs", "matching image/model contract", reason, reason};
}
bool sameImageContract(const PreparedInputRef &input, const ModelInputContract &expected) {
    if (!input) {
        return false;
    }
    const auto &actual{input->contract()};
    return actual.shape == expected.shape && actual.element_type == expected.element_type &&
           actual.layout == expected.layout && actual.scale == expected.scale && actual.offset == expected.offset;
}
ExecutionResult executeChecked(ModelExecutor &executor, const std::vector<Tensor> &inputs) {
    if (const auto error{validateInputs(inputs, executor.description())}) {
        return ExecutionResult::failure(error);
    }
    return executor.execute(inputs);
}
} // namespace
JetsonYoloBackend::JetsonYoloBackend(std::unique_ptr<ModelExecutor> executor, InputAdapter adapter)
    : executor_{std::move(executor)}, adapter_{std::move(adapter)} {
    if (!executor_ || !adapter_) {
        throw std::invalid_argument("YOLO dependencies are required");
    }
}
ModelInputContract JetsonYoloBackend::inputContract() const {
    const auto &description{executor_->description()};
    if (description.inputs.size() != 1) {
        throw std::invalid_argument(describe(contractError(description.identity, "Expected one input")));
    }
    const auto &spec{description.inputs.front()};
    if (spec.shape.size() != 4 || spec.shape[0] != 1 || spec.shape[1] != 3 || spec.shape[2] <= 0 ||
        spec.shape[3] <= 0 || spec.type != TensorElementType::Float32 ||
        (spec.layout != TensorLayout::Linear && spec.layout != TensorLayout::Nchw)) {
        throw std::invalid_argument(
            describe(contractError(description.identity, "Expected resolved [1,3,H,W] Float32")));
    }
    return {spec.shape, spec.type, TensorLayout::Nchw, 1.0f / 255.0f, 0.0f};
}
Result<int> JetsonYoloBackend::classCount() const {
    const auto &description{executor_->description()};
    if (description.outputs.empty()) {
        return Result<int>::failure(contractError(description.identity, "Missing YOLO output"));
    }
    const auto &spec{description.outputs.front()};
    if (spec.index != 0) {
        return Result<int>::failure(contractError(description.identity, "YOLO output must have index 0"));
    }
    const auto bytes{tensorBytes(spec.shape, spec.type, description.identity, spec.name)};
    if (!bytes) {
        return Result<int>::failure(bytes.error);
    }
    return yoloClassCount({spec.shape, spec.type, spec.layout, bytes.value, {}}, description.identity);
}
Result<YoloPrediction> JetsonYoloBackend::infer(const PreparedInputRef &input) {
    const auto &identity{executor_->description().identity};
    if (!sameImageContract(input, inputContract())) {
        return Result<YoloPrediction>::failure(contractError(identity, "YOLO prepared input mismatch"));
    }
    auto adapted{adapter_(input, identity)};
    if (!adapted) {
        return Result<YoloPrediction>::failure(adapted.error);
    }
    auto outputs{executeChecked(*executor_, {adapted.value})};
    if (!outputs) {
        return Result<YoloPrediction>::failure(outputs.error);
    }
    return yoloPrediction(outputs.value, identity);
}
JetsonNanoTrackBackend::JetsonNanoTrackBackend(std::unique_ptr<ModelExecutor> backbone,
                                               std::unique_ptr<ModelExecutor> head, InputAdapter adapter)
    : backbone_{std::move(backbone)}, head_{std::move(head)}, adapter_{std::move(adapter)} {
    if (!backbone_ || !head_ || !adapter_) {
        throw std::invalid_argument("NanoTrack dependencies are required");
    }
    if (backbone_->description().inputs.size() != 1 || head_->description().inputs.size() != 2) {
        throw std::invalid_argument(
            describe(contractError(head_->description().identity, "Expected one Backbone and two Head inputs")));
    }
}
void JetsonNanoTrackBackend::reset() noexcept { template_ = {}; }
Result<Tensor> JetsonNanoTrackBackend::features(const PreparedInputRef &input, const ModelInputContract &contract,
                                                std::size_t head_index) {
    const auto &identity{backbone_->description().identity};
    if (!sameImageContract(input, contract)) {
        return Result<Tensor>::failure(contractError(identity, "NanoTrack image contract mismatch"));
    }
    auto adapted{adapter_(input, identity)};
    if (!adapted) {
        return adapted;
    }
    auto outputs{executeChecked(*backbone_, {adapted.value})};
    if (!outputs) {
        return Result<Tensor>::failure(outputs.error);
    }
    if (outputs.value.empty()) {
        return Result<Tensor>::failure(contractError(identity, "Missing Backbone feature"));
    }
    const auto &head{head_->description()};
    if (const auto error{validateTensor(outputs.value.front(), head.inputs.at(head_index), head.identity)}) {
        return Result<Tensor>::failure(error);
    }
    return {outputs.value.front()};
}
Status JetsonNanoTrackBackend::setTemplate(const PreparedInputRef &input) {
    reset();
    auto feature{features(input, templateContract(), 0)};
    if (!feature) {
        return Status::failure(feature.error);
    }
    template_ = std::move(feature.value);
    return {true};
}
Result<NanoPrediction> JetsonNanoTrackBackend::search(const PreparedInputRef &input) {
    if (!template_.storage) {
        return Result<NanoPrediction>::failure(
            {ModelError::NotReady, head_->description().identity, {}, {}, {}, "No valid template"});
    }
    auto feature{features(input, searchContract(), 1)};
    if (!feature) {
        return Result<NanoPrediction>::failure(feature.error);
    }
    auto outputs{executeChecked(*head_, {template_, feature.value})};
    if (!outputs) {
        return Result<NanoPrediction>::failure(outputs.error);
    }
    return nanoPrediction(outputs.value, head_->description().identity);
}
} // namespace model
