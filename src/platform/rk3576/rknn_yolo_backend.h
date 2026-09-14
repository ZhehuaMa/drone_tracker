#ifndef RK3576_RKNN_YOLO_BACKEND_H
#define RK3576_RKNN_YOLO_BACKEND_H

#include "model/model_backend.h"

namespace rk3576 {
class RknnRuntime;
struct RknnApi;
/** @brief 正式 YOLO 接口适配，SDK 类型仅存在于平台私有 Runtime 头文件。 */
class RknnYoloBackend final : public model::YoloBackend {
public:
    explicit RknnYoloBackend(std::unique_ptr<RknnRuntime> runtime);
    ~RknnYoloBackend() override;
    ModelInputContract inputContract() const override;
    model::Result<int> classCount() const override;
    model::Result<model::YoloPrediction> infer(const PreparedInputRef &input) override;

private:
    std::unique_ptr<RknnRuntime> runtime_;
};
/** @brief 加载兼容模型，失败不返回半初始化后端。 */
model::Result<std::unique_ptr<model::YoloBackend>> loadYoloBackend(const std::string &path);
model::Result<std::unique_ptr<model::YoloBackend>> loadYoloBackend(
    const std::string &path, RknnApi api, std::string &description);
} // namespace rk3576
#endif // RK3576_RKNN_YOLO_BACKEND_H
