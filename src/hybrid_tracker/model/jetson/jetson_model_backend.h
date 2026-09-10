#ifndef JETSON_MODEL_BACKEND_H
#define JETSON_MODEL_BACKEND_H

#include <functional>
#include "model/model_backend.h"

namespace model {
/** @brief 注入仅负责存储适配的函数，使真实串联逻辑可独立于设备 SDK 测试。 */
using InputAdapter = std::function<Result<Tensor>(const PreparedInputRef &, const std::string &)>;
class JetsonYoloBackend final : public YoloBackend {
public:
    JetsonYoloBackend(std::unique_ptr<ModelExecutor> executor, InputAdapter adapter);
    ModelInputContract inputContract() const override;
    Result<YoloPrediction> infer(const PreparedInputRef &input) override;

private:
    std::unique_ptr<ModelExecutor> executor_;
    InputAdapter adapter_;
};
class JetsonNanoTrackBackend final : public NanoTrackBackend {
public:
    JetsonNanoTrackBackend(std::unique_ptr<ModelExecutor> backbone, std::unique_ptr<ModelExecutor> head,
                           InputAdapter adapter);
    Status setTemplate(const PreparedInputRef &input) override;
    Result<NanoPrediction> search(const PreparedInputRef &input) override;
    void reset() noexcept override;

private:
    Result<Tensor> features(const PreparedInputRef &input, const ModelInputContract &contract, std::size_t head_index);
    std::unique_ptr<ModelExecutor> backbone_;
    std::unique_ptr<ModelExecutor> head_;
    InputAdapter adapter_;
    Tensor template_{};
};
} // namespace model
#endif // JETSON_MODEL_BACKEND_H
