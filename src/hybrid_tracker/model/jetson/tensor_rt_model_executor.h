#ifndef TENSOR_RT_MODEL_EXECUTOR_H
#define TENSOR_RT_MODEL_EXECUTOR_H

#include "model/model_executor.h"

namespace model {
/** @brief 加载现有 TensorRT 文件；失败返回包含模型路径的诊断，无半初始化实例。 */
LoadResult loadTensorRtModel(const std::string &path);
} // namespace model
#endif // TENSOR_RT_MODEL_EXECUTOR_H
