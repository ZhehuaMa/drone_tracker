#ifndef NANOTRACK_SEARCH_DIRECT_DIRECT_SESSION_H
#define NANOTRACK_SEARCH_DIRECT_DIRECT_SESSION_H

#include <memory>
#include <string>
#include <vector>

#include "native_attributes.h"
#include "model/model_backend.h"
#include "platform/rk3576/rknn_session.h"
#include "shared_memory_api.h"

namespace search_direct {
/**
 * @brief 测试专用单模型会话：既能普通提交，也能把输入或输出绑定到共享设备内存。
 *
 * 普通提交指 `rknn_inputs_set`，由 Runtime 接收主机数据并按需要转换。
 * 绑定指 `rknn_set_io_mem`，只是指定该模型的某个输入或输出使用哪块设备内存，
 * 本身不执行模型，也不复制特征。
 *
 * 本类与生产的 `rk3576::RknnSession` 并列存在，不改写生产会话的输出所有权约定。
 * 同一时刻只允许串行调用；销毁前必须先释放本会话登记的全部设备内存。
 */
class DirectSession final {
public:
    static model::Result<std::unique_ptr<DirectSession>> load(const std::string &path, SharedMemoryApi api);
    ~DirectSession() noexcept;
    DirectSession(const DirectSession &) = delete;
    DirectSession &operator=(const DirectSession &) = delete;
    /** @brief 仅供 load 工厂创建未初始化对象；应通过 load 取得可用实例。 */
    DirectSession(std::string path, SharedMemoryApi api) : path_{std::move(path)}, api_{api} {}

    bool ready() const noexcept { return ready_; }
    const std::vector<rknn_tensor_attr> &inputs() const noexcept { return inputs_; }
    const std::vector<rknn_tensor_attr> &outputs() const noexcept { return outputs_; }
    const std::vector<rknn_tensor_attr> &nativeInputs() const noexcept { return native_inputs_; }
    const std::vector<rknn_tensor_attr> &nativeOutputs() const noexcept { return native_outputs_; }
    const std::vector<model::TensorDescriptor> &outputDescriptors() const noexcept { return descriptors_; }
    const std::string &identity() const noexcept { return path_; }
    std::string describe() const;
    SessionAttributes attributes() const;

    /**
     * @brief 普通提交若干输入；允许只提交模型输入的一部分。
     *
     * 只提交一部分是搜索直连路径的需要：head 的模板输入走普通提交，搜索输入走共享内存。
     * 每次普通提交都会使本会话已建立的共享内存绑定失效，调用方必须在执行前重新绑定。
     */
    model::Status submit(const std::vector<rk3576::SessionInput> &inputs);
    /** @brief 把某个输入或输出绑定到共享设备内存；记录为 `io_mem_bind` 计时区间。 */
    model::Status bind(const rknn_tensor_attr &attribute, rknn_tensor_mem *memory);
    /** @brief 同步执行一次模型；失败时确认设备停止并使会话失效。 */
    model::Status run();
    /**
     * @brief 确认本次同步执行已经结束，供不取回输出的一侧使用。
     *
     * 本会话初始化时没有设置任何异步标志，因此阻塞的 `rknn_run` 返回成功即代表推理结束。
     * 若初始化标志不是 0，说明前提被破坏，按仓库致命诊断策略终止进程。
     */
    void confirmCompleted();
    /** @brief 按普通接口取回全部输出并复制为独立 Float32 存储。 */
    model::ExecutionResult fetch();

    model::Result<rknn_tensor_mem *> allocate(std::uint32_t bytes);
    model::Result<rknn_tensor_mem *> importFrom(const rknn_tensor_mem &source);
    /**
     * @brief 在 CPU 读取共享内存前按设备写入结果同步缓存。
     *
     * 仅供消费者执行完成后的诊断读取使用；正式计时路径不调用。
     */
    model::Status syncFromDevice(rknn_tensor_mem *memory);
    /** @brief 释放本会话登记的内存句柄；失败按致命诊断终止，不继续使用该存储。 */
    model::Status releaseMemory(rknn_tensor_mem *memory);
    std::size_t registeredMemories() const noexcept { return memories_.size(); }
    void invalidate() noexcept;

private:
    model::Diagnostic initialize();
    model::Diagnostic checkAttribute(const rknn_tensor_attr &attribute, bool input);
    model::Diagnostic queryNative(std::vector<rknn_tensor_attr> &target, bool input);
    model::Diagnostic failure(const std::string &operation, int status) const;
    void confirmStopped();
    /** @brief 使会话不可再执行；不在此处销毁 context，共享内存仍需按所有权顺序释放。 */
    void markFailed() noexcept { ready_ = false; }

    std::string path_;
    SharedMemoryApi api_;
    rknn_context context_{};
    bool ready_{false};
    bool running_{false};
    std::uint32_t init_flags_{};
    rknn_sdk_version version_{};
    std::vector<rknn_tensor_attr> inputs_{};
    std::vector<rknn_tensor_attr> outputs_{};
    std::vector<rknn_tensor_attr> native_inputs_{};
    std::vector<rknn_tensor_attr> native_outputs_{};
    std::vector<model::TensorDescriptor> descriptors_{};
    std::vector<rknn_tensor_mem *> memories_{};
};
} // namespace search_direct
#endif // NANOTRACK_SEARCH_DIRECT_DIRECT_SESSION_H
