#ifndef RK3576_RKNN_RUNTIME_H
#define RK3576_RKNN_RUNTIME_H

#include "rknn_session.h"

namespace rk3576 {
/** @brief YOLO 兼容入口；执行与设备生命周期全部委托给通用会话。 */
class RknnRuntime final {
public:
    static model::Result<std::unique_ptr<RknnRuntime>> load(const std::string &path, RknnApi api);
    RknnRuntime(std::unique_ptr<RknnSession> session, int classes) : session_{std::move(session)}, classes_{classes} {}
    int classCount() const noexcept { return classes_; }
    const std::string &identity() const noexcept { return session_->identity(); }
    std::string describe() const;
    model::ExecutionResult execute(const std::vector<std::uint8_t> &input);

private:
    std::unique_ptr<RknnSession> session_;
    int classes_{};
};
} // namespace rk3576
#endif // RK3576_RKNN_RUNTIME_H
