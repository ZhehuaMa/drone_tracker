#include "rknn_nanotrack_contract.h"

namespace rk3576 {
TensorShape nchwShape(const rknn_tensor_attr &a) {
    if (a.n_dims != 4) {
        return {};
    }
    const auto n{static_cast<std::int32_t>(a.dims[0])};
    const auto x{static_cast<std::int32_t>(a.dims[1])};
    const auto y{static_cast<std::int32_t>(a.dims[2])};
    const auto z{static_cast<std::int32_t>(a.dims[3])};
    return a.fmt == RKNN_TENSOR_NCHW ? TensorShape{n, x, y, z} : TensorShape{n, z, x, y};
}
model::Result<NanoTrackRoles> validateNanoTrackSessions(const RknnSession &z, const RknnSession &x,
                                                        const RknnSession &head) {
    using Result = model::Result<NanoTrackRoles>;
    // 类型和受支持的量化形式已由会话校验。FP16/Float32 可能带 AFFINE 元数据，
    // 仍按浮点值交接，不以量化标记拒绝，也不应用 scale/zp 再次反量化。
    for (const auto *session : {&z, &x}) {
        const int image{session == &z ? 127 : 255};
        const int feature{session == &z ? 8 : 16};
        if (session->inputs().size() != 1 || session->outputs().size() != 1 ||
            nchwShape(session->inputs()[0]) != TensorShape{1, 3, image, image} ||
            nchwShape(session->outputs()[0]) != TensorShape{1, 96, feature, feature}) {
            return Result::failure({model::ModelError::ContractMismatch, session->identity(), "Backbone",
                                    "one image [1,3," + std::to_string(image) + "," + std::to_string(image) +
                                        "] -> one feature [1,96," + std::to_string(feature) + "," +
                                        std::to_string(feature) + "]",
                                    session->describe(), "Backbone shape mismatch"});
        }
    }
    if (head.inputs().size() != 2 || head.outputs().size() != 2) {
        return Result::failure({model::ModelError::ContractMismatch, head.identity(), "input/output count", "2/2",
                                head.describe(), "Head requires exactly two inputs and outputs"});
    }
    NanoTrackRoles roles;
    for (bool input : {true, false}) {
        bool first{false};
        bool second{false};
        for (const auto &a : input ? head.inputs() : head.outputs()) {
            const auto shape{nchwShape(a)};
            if (shape == (input ? TensorShape{1, 96, 8, 8} : TensorShape{1, 2, 15, 15}) && !first) {
                first = true;
                (input ? roles.template_input : roles.classification_output) = a.index;
            } else if (shape == (input ? TensorShape{1, 96, 16, 16} : TensorShape{1, 4, 15, 15}) && !second) {
                second = true;
                (input ? roles.search_input : roles.location_output) = a.index;
            } else {
                return Result::failure({model::ModelError::ContractMismatch, head.identity(),
                                        std::string(input ? "input#" : "output#") + std::to_string(a.index),
                                        input ? "unique template [1,96,8,8] and search [1,96,16,16]"
                                              : "unique classification [1,2,15,15] and location [1,4,15,15]",
                                        head.describe(), "Unknown or duplicate Head role"});
            }
        }
    }
    return {roles};
}
} // namespace rk3576
