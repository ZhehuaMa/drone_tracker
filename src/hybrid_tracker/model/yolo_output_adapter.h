#ifndef MODEL_YOLO_OUTPUT_ADAPTER_H
#define MODEL_YOLO_OUTPUT_ADAPTER_H

#include "model/model_backend.h"

namespace model {
/** @brief 校验按框/类别配对的 80、40、20 六输出描述，返回共同类别数。 */
Result<int> yoloSixOutputClasses(const std::vector<TensorDescriptor> &outputs, const std::string &identity);
/** @brief 六份已反量化的原始分数转为自有 [1,4+C,8400]；不筛选候选。 */
Result<YoloPrediction> adaptYoloSixOutputs(const std::vector<Tensor> &outputs, const std::string &identity);
} // namespace model
#endif // MODEL_YOLO_OUTPUT_ADAPTER_H
