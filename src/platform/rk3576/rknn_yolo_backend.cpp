#include "rknn_yolo_backend.h"

#include <stdexcept>

#include "model/yolo_output_adapter.h"
#include "rknn_runtime.h"
#include "yolo_preprocessor.h"

namespace rk3576 {
RknnYoloBackend::RknnYoloBackend(std::unique_ptr<RknnRuntime> runtime) : runtime_{std::move(runtime)} {
    if (!runtime_) {
        throw std::invalid_argument("RKNN Runtime is required");
    }
}
RknnYoloBackend::~RknnYoloBackend() = default;
ModelInputContract RknnYoloBackend::inputContract() const { return yoloContract(); }
model::Result<int> RknnYoloBackend::classCount() const { return {runtime_->classCount()}; }
model::Result<model::YoloPrediction> RknnYoloBackend::infer(const PreparedInputRef &input) {
    const auto bytes{std::dynamic_pointer_cast<const ByteInput>(input)};
    if (!bytes) {
        return model::Result<model::YoloPrediction>::failure({model::ModelError::PlatformTypeMismatch, runtime_->identity(),
                                                              "input", "ByteInput", "foreign storage",
                                                              "Incompatible prepared input"});
    }
    auto outputs{runtime_->execute(bytes->bytes())};
    if (!outputs) {
        return model::Result<model::YoloPrediction>::failure(outputs.error);
    }
    return model::adaptYoloSixOutputs(outputs.value, runtime_->identity());
}
} // namespace rk3576
