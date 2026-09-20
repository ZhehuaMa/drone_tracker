#include "rknn_runtime.h"

#include "rknn_yolo_contract.h"

namespace rk3576 {
model::Result<std::unique_ptr<RknnRuntime>> RknnRuntime::load(const std::string &path, RknnApi api) {
    using Result = model::Result<std::unique_ptr<RknnRuntime>>;
    auto session{RknnSession::load(path, api)};
    if (!session) {
        if (session.error.code == model::ModelError::ContractMismatch) {
            session.error.expected +=
                "; YOLO input: NHWC [1,640,640,3], INT8 AFFINE zp=-128 scale=1/255 or FP16 NONE/AFFINE; "
                "submit UINT8 NHWC pass_through=0; compatibility requires mean=0/std=255 "
                "(not verified by attribute query)";
        }
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
std::string RknnRuntime::describe() const {
    return session_->describe() + "C=" + std::to_string(classes_) +
        "\nsubmission: UINT8 NHWC [1,640,640,3], 1228800 bytes, pass_through=0; "
        "Runtime performs input conversion\ncompatibility requires mean=0/std=255 "
        "(not verified by attribute query)\n";
}
} // namespace rk3576
