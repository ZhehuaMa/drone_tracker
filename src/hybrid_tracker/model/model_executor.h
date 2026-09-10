#ifndef MODEL_EXECUTOR_H
#define MODEL_EXECUTOR_H

#include <string>
#include <utility>
#include "model/tensor.h"

namespace model {
/** @brief 模型原生输入或输出的契约；-1 轴使用 min/max 限定输入范围。 */
struct TensorSpec {
    std::string name{};
    std::size_t index{};
    TensorShape shape{};
    TensorShape minimum{};
    TensorShape maximum{};
    TensorElementType type{TensorElementType::Float32};
    TensorLayout layout{TensorLayout::Linear};
};
struct ModelDescription {
    std::string identity{};
    std::vector<TensorSpec> inputs{};
    std::vector<TensorSpec> outputs{};
};
enum class ModelError { None, LoadFailure, ContractMismatch, PlatformTypeMismatch, NotReady, ExecutionFailure };
/** @brief 分类诊断；契约失败同时说明张量身份及预期、实际属性。 */
struct Diagnostic {
    ModelError code{ModelError::None};
    std::string model{};
    std::string tensor{};
    std::string expected{};
    std::string actual{};
    std::string message{};
    explicit operator bool() const noexcept { return code != ModelError::None; }
};
std::string describe(const Diagnostic &diagnostic);
/** @brief 失败结果不携带部分输出。 */
template <typename T> struct Result {
    T value{};
    Diagnostic error{};
    explicit operator bool() const noexcept { return !error; }
    static Result failure(Diagnostic error) { return {T{}, std::move(error)}; }
};
using ExecutionResult = Result<std::vector<Tensor>>;
/** @brief 单实例仅支持串行调用。成功返回前已同步，输入可释放、输出可由 CPU 读取。 */
class ModelExecutor {
public:
    virtual ~ModelExecutor() = default;
    virtual const ModelDescription &description() const noexcept = 0;
    virtual ExecutionResult execute(const std::vector<Tensor> &inputs) = 0;
};
using LoadResult = Result<std::unique_ptr<ModelExecutor>>;
} // namespace model
#endif // MODEL_EXECUTOR_H
