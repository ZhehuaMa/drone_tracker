#ifndef MODEL_VALIDATION_H
#define MODEL_VALIDATION_H

#include "model/model_backend.h"

namespace model {
Result<std::size_t> tensorBytes(const TensorShape &shape, TensorElementType type, const std::string &identity,
                                const std::string &name);
Diagnostic validateTensor(const Tensor &tensor, const TensorSpec &spec, const std::string &identity);
Diagnostic validateInputs(const std::vector<Tensor> &inputs, const ModelDescription &description);
Diagnostic validateDescriptor(const TensorDescriptor &descriptor, const std::string &identity, const std::string &name);
Result<YoloPrediction> yoloPrediction(const std::vector<Tensor> &outputs, const std::string &identity);
Result<NanoPrediction> nanoPrediction(const std::vector<Tensor> &outputs, const std::string &identity);
/** @brief 设备停止状态无法确认时使用，不执行任何栈展开或静态析构。 */
[[noreturn]] void terminateDeviceFailure(const char *identity, const char *reason) noexcept;
} // namespace model
#endif // MODEL_VALIDATION_H
