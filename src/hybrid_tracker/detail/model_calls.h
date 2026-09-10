#ifndef DETAIL_MODEL_CALLS_H
#define DETAIL_MODEL_CALLS_H

#include <memory>
#include <string>
#include <vector>
#include "preprocess/model_input.h"
#include <opencv2/core/mat.hpp>

namespace tracker_detail {
/** @brief 内部输出存储所有者，确保以下借用的 OpenCV 图像头不会悬空。 */
struct OutputStorage {
    virtual ~OutputStorage() = default;
};
struct ModelOutputs {
    std::shared_ptr<OutputStorage> storage{};
    std::vector<cv::Mat> tensors{};
};
/** @brief 模型调用测试接入点；同步消费借用输入，返回受所有者保护的原始输出。 */
class YoloModelCalls {
  public:
    virtual ~YoloModelCalls() = default;
    virtual ModelInputContract inputContract() const = 0;
    virtual ModelOutputs infer(const PreparedModelInput &input) = 0;
};
/** @brief 目标模板特征归本对象所有；公共算法仍负责全部 Head 输出数学处理。 */
class NanoModelCalls {
  public:
    virtual ~NanoModelCalls() = default;
    virtual void setTemplate(const PreparedModelInput &input) = 0;
    virtual ModelOutputs search(const PreparedModelInput &input) = 0;
};
/** @brief 生产装配工厂，创建时加载现有 TensorRT 引擎；测试不链接此实现。 */
std::unique_ptr<YoloModelCalls> createYoloModelCalls(const std::string &path);
std::unique_ptr<NanoModelCalls> createNanoModelCalls(const std::string &backbone, const std::string &head);
} // namespace tracker_detail

#endif // DETAIL_MODEL_CALLS_H
