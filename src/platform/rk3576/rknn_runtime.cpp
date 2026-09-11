#include "rknn_runtime.h"

#include "rknn_yolo_contract.h"

namespace rk3576 {
model::Result<std::unique_ptr<RknnRuntime>> RknnRuntime::load(const std::string &path, RknnApi api) {
    using Result = model::Result<std::unique_ptr<RknnRuntime>>;
    auto session{RknnSession::load(path, api)};
    if (!session) {
        return Result::failure(session.error);
    }
    const auto classes{validateYoloSession(*session.value)};
    if (!classes) {
        return Result::failure(classes.error);
    }
    return {std::make_unique<RknnRuntime>(std::move(session.value), classes.value)};
}
model::ExecutionResult RknnRuntime::execute(const std::vector<std::uint8_t> &input) {
    // 旧入口接受借用 vector；同步调用内复制为明确拥有的字节存储。
    auto storage{std::make_shared<const std::vector<std::uint8_t>>(input)};
    return session_->execute({{0,
                               {{1, 640, 640, 3}, TensorElementType::Uint8, TensorLayout::Nhwc, storage->size(), {}},
                               storage->data(),
                               storage->size(),
                               storage}});
}
std::string RknnRuntime::describe() const { return session_->describe() + "C=" + std::to_string(classes_) + "\n"; }
} // namespace rk3576
