#include "model/jetson/jetson_model_backend.h"

#include <stdexcept>
#include "model/jetson/jetson_storage.h"
#include "model/jetson/tensor_rt_model_executor.h"
#include "model/validation.h"

namespace model {
namespace {
std::unique_ptr<ModelExecutor> load(const std::string &path) {
    auto result{loadTensorRtModel(path)};
    if (!result) {
        throw std::runtime_error(describe(result.error));
    }
    return std::move(result.value);
}
} // namespace
std::unique_ptr<YoloBackend> createYoloBackend(const std::string &path) {
    return std::make_unique<JetsonYoloBackend>(load(path), adaptJetsonInput);
}
std::unique_ptr<NanoTrackBackend> createNanoTrackBackend(const std::string &backbone, const std::string &head) {
    auto backbone_executor{load(backbone)};
    auto head_executor{load(head)};
    return std::make_unique<JetsonNanoTrackBackend>(std::move(backbone_executor), std::move(head_executor),
                                                    adaptJetsonInput);
}
} // namespace model
