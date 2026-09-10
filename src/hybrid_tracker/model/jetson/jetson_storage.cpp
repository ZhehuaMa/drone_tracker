#include "model/jetson/jetson_storage.h"

#include "model/validation.h"
#include "preprocess/jetson/jetson_model_input.h"

namespace model {
Result<Tensor> adaptJetsonInput(const PreparedInputRef &input, const std::string &identity) {
    const auto jetson{std::dynamic_pointer_cast<const JetsonPreparedModelInput>(input)};
    if (!jetson) {
        return Result<Tensor>::failure({ModelError::PlatformTypeMismatch, identity, "input", "Jetson input",
                                        "other or null input", "Incompatible prepared input"});
    }
    const auto &contract{jetson->contract()};
    const auto &buffer{jetson->storage()};
    if (buffer.dims.nbDims != static_cast<int>(contract.shape.size())) {
        return Result<Tensor>::failure({ModelError::ContractMismatch, identity, "input", "matching storage rank",
                                        "mismatch", "Invalid prepared storage"});
    }
    for (int axis{0}; axis < buffer.dims.nbDims; ++axis) {
        if (buffer.dims.d[axis] != contract.shape[axis]) {
            return Result<Tensor>::failure({ModelError::ContractMismatch, identity, "input", "matching storage shape",
                                            "mismatch", "Invalid prepared storage"});
        }
    }
    int device{};
    if (cudaGetDevice(&device) != cudaSuccess) {
        return Result<Tensor>::failure(
            {ModelError::ExecutionFailure, identity, {}, {}, {}, "Cannot query CUDA device"});
    }
    // 别名 shared_ptr 保留整个准备输入，因而连同其缓存租约一起保留。
    auto owner{std::shared_ptr<const TensorBuffer>{jetson, &buffer}};
    Tensor tensor{{contract.shape, contract.element_type, contract.layout, buffer.size, {}},
                  std::make_shared<JetsonStorage>(std::move(owner), buffer.device)};
    if (const auto error{validateDescriptor(tensor.descriptor, identity, "input")}) {
        return Result<Tensor>::failure(error);
    }
    return {std::move(tensor)};
}
} // namespace model
