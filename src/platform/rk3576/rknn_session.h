#ifndef RK3576_RKNN_SESSION_H
#define RK3576_RKNN_SESSION_H

#include <memory>
#include <string>
#include <vector>

#include "model/model_executor.h"

#include "rknn_api.h"

namespace rk3576 {
/** @brief 平台私有输入；owner 保持地址有效，字节输入不使用 float 指针。 */
struct SessionInput {
    std::uint32_t index{};
    model::TensorDescriptor descriptor{};
    const void *data{};
    std::size_t bytes{};
    std::shared_ptr<const void> owner{};
};
/** @brief 单模型串行同步会话；销毁前必须结束所有调用。 */
class RknnSession final {
public:
    static model::Result<std::unique_ptr<RknnSession>> load(const std::string &path, RknnApi api);
    ~RknnSession() noexcept;
    RknnSession(const RknnSession &) = delete;
    RknnSession &operator=(const RknnSession &) = delete;
    bool ready() const noexcept { return ready_; }
    const std::vector<rknn_tensor_attr> &inputs() const noexcept { return inputs_; }
    const std::vector<rknn_tensor_attr> &outputs() const noexcept { return outputs_; }
    const std::vector<model::TensorDescriptor> &outputDescriptors() const noexcept { return descriptors_; }
    const std::string &identity() const noexcept { return path_; }
    std::string describe() const;
    model::ExecutionResult execute(const std::vector<SessionInput> &inputs);
    /** @brief 仅供 load 工厂创建未初始化对象；应通过 load 取得可用实例。 */
    RknnSession(std::string path, RknnApi api) : path_{std::move(path)}, api_{api} {}

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
    rknn_sdk_version version_{};
    std::vector<rknn_tensor_attr> inputs_{};
    std::vector<rknn_tensor_attr> outputs_{};
    std::vector<model::TensorDescriptor> descriptors_{};
};
} // namespace rk3576
#endif // RK3576_RKNN_SESSION_H
