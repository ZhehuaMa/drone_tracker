#ifndef MODEL_BACKEND_H
#define MODEL_BACKEND_H

#include "model/model_executor.h"
#include "preprocess/model_input.h"

namespace model {
/** @brief 原始预测的属性轴与候选轴，固定为 [1,4+C,N]。 */
struct YoloPrediction {
    Tensor predictions{};
    std::size_t attribute_axis{1};
    std::size_t candidate_axis{2};
};
struct NanoPrediction {
    Tensor classification{};
    Tensor location{};
};
using Status = Result<bool>;
/** @brief 仅执行 YOLO 模型，框解码和 NMS 仍由公共检测器负责。 */
class YoloBackend {
public:
    virtual ~YoloBackend() = default;
    virtual ModelInputContract inputContract() const = 0;
    /** @brief 从已校验的模型输出描述取得类别数；非法描述返回失败。 */
    virtual Result<int> classCount() const = 0;
    virtual Result<YoloPrediction> infer(const PreparedInputRef &input) = 0;
};
/** @brief 只管理模型模板与串联，不计算裁剪区域或更新目标框。 */
class NanoTrackBackend {
public:
    virtual ~NanoTrackBackend() = default;
    virtual ModelInputContract templateContract() const { return {{1, 3, 127, 127}}; }
    virtual ModelInputContract searchContract() const { return {{1, 3, 255, 255}}; }
    virtual Status setTemplate(const PreparedInputRef &input) = 0;
    virtual Result<NanoPrediction> search(const PreparedInputRef &input) = 0;
    virtual void reset() noexcept = 0;
};
} // namespace model
#endif // MODEL_BACKEND_H
