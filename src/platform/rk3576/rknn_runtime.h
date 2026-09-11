#ifndef RK3576_RKNN_RUNTIME_H
#define RK3576_RKNN_RUNTIME_H

#include <array>
#include <memory>
#include <string>

#include "model/model_executor.h"

#include <rknn_api.h>

namespace rk3576 {
/** @brief 仅平台内部使用的可注入 C API 边界；测试替换调用，不模拟公共检测器。 */
struct RknnApi {
    decltype(&rknn_init) init{};
    decltype(&rknn_query) query{};
    decltype(&rknn_inputs_set) inputs_set{};
    decltype(&rknn_run) run{};
    decltype(&rknn_wait) wait{};
    decltype(&rknn_outputs_get) outputs_get{};
    decltype(&rknn_outputs_release) outputs_release{};
    decltype(&rknn_destroy) destroy{};
};
RknnApi nativeRknnApi();
/** @brief 串行同步 YOLO Runtime；调用者必须在销毁前停止并 join 检测线程。 */
class RknnRuntime final {
public:
    static model::Result<std::unique_ptr<RknnRuntime>> load(const std::string &path, RknnApi api);
    ~RknnRuntime() noexcept;
    RknnRuntime(const RknnRuntime &) = delete;
    RknnRuntime &operator=(const RknnRuntime &) = delete;
    int classCount() const noexcept { return classes_; }
    const std::string &identity() const noexcept { return path_; }
    std::string describe() const;
    model::ExecutionResult execute(const std::vector<std::uint8_t> &input);
    /** @brief 仅供 load 工厂创建未初始化对象；应通过 load 取得可用实例。 */
    RknnRuntime(std::string path, RknnApi api) : path_{std::move(path)}, api_{api} {}

private:
    model::Diagnostic initialize();
    model::Diagnostic checkAttribute(const rknn_tensor_attr &attr, bool input);
    model::Diagnostic failure(const std::string &operation, int status) const;
    void invalidate() noexcept;
    void confirmStopped();
    std::string path_;
    RknnApi api_;
    rknn_context context_{};
    bool ready_{false};
    bool running_{false};
    int classes_{0};
    rknn_sdk_version version_{};
    rknn_tensor_attr input_{};
    std::array<rknn_tensor_attr, 6> outputs_{};
    std::vector<model::TensorDescriptor> descriptors_{};
};
} // namespace rk3576
#endif // RK3576_RKNN_RUNTIME_H
