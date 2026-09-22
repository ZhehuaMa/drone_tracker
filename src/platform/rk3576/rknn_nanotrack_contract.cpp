#include "rknn_nanotrack_contract.h"

#include <limits>
#include <sstream>

namespace rk3576 {
namespace {
std::string attributeText(const rknn_tensor_attr &a) {
    std::ostringstream out;
    out << "#" << a.index << " [";
    for (std::uint32_t i{}; i < a.n_dims && i < RKNN_MAX_DIMS; ++i) {
        out << a.dims[i] << ',';
    }
    out << "] type=" << get_type_string(a.type) << " fmt=" << get_format_string(a.fmt)
        << " elems=" << a.n_elems << " bytes=" << a.size << " stride=" << a.size_with_stride;
    return out.str();
}
model::Diagnostic reject(const std::string &identity, const std::string &tensor, const std::string &expected,
                         const std::string &actual, const std::string &message) {
    return {model::ModelError::ContractMismatch, identity, tensor, expected, actual, message};
}
std::uint32_t effectiveWidth(const rknn_tensor_attr &a) { return a.w_stride != 0 ? a.w_stride : a.dims[3]; }
model::Diagnostic checkNative(const rknn_tensor_attr &native, const rknn_tensor_attr &logical,
                              const std::string &identity, const std::string &tensor) {
    if (native.n_dims != 5 || native.fmt != RKNN_TENSOR_NC1HWC2 || native.type != RKNN_TENSOR_FLOAT16) {
        return reject(identity, tensor, "FP16 NC1HWC2", attributeText(native), "Unsupported native feature layout");
    }
    const auto shape{nchwShape(logical)};
    if (shape.size() != 4 || native.dims[0] != static_cast<std::uint32_t>(shape[0]) ||
        native.dims[2] != static_cast<std::uint32_t>(shape[2]) ||
        native.dims[3] != static_cast<std::uint32_t>(shape[3])) {
        return reject(identity, tensor, "native batch and spatial dimensions match logical shape",
                      attributeText(native), "Native/logical shape mismatch");
    }
    const auto group{native.dims[4]};
    const auto groups{native.dims[1]};
    if (group == 0 || groups != (static_cast<std::uint32_t>(shape[1]) + group - 1) / group) {
        return reject(identity, tensor, "native channel groups cover logical channels",
                      attributeText(native), "Native channel grouping mismatch");
    }
    std::uint64_t elements{1};
    for (std::uint32_t i{}; i < native.n_dims; ++i) {
        elements *= native.dims[i];
    }
    if (elements != native.n_elems || elements > std::numeric_limits<std::uint32_t>::max() / 2U ||
        native.size != elements * 2U || effectiveWidth(native) < native.dims[3] ||
        native.size_with_stride != static_cast<std::uint64_t>(native.dims[0]) * groups * native.dims[2] *
                                      effectiveWidth(native) * group * 2U) {
        return reject(identity, tensor, "native shape, FP16 size, stride and capacity are consistent",
                      attributeText(native), "Native capacity contract mismatch");
    }
    return {};
}
} // namespace
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
            return Result::failure({model::ModelError::ContractMismatch, session->identity(), session == &z ? "template_backbone" : "search_backbone",
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
model::Result<SharedFeatureContract> matchSharedFeature(const rknn_tensor_attr &producer_native,
                                                        const rknn_tensor_attr &producer_logical,
                                                        const rknn_tensor_attr &consumer_native,
                                                        const rknn_tensor_attr &consumer_logical,
                                                        const std::string &identity) {
    using Result = model::Result<SharedFeatureContract>;
    if (const auto error{checkNative(producer_native, producer_logical, identity, "search native output")}) {
        return Result::failure(error);
    }
    if (const auto error{checkNative(consumer_native, consumer_logical, identity, "head native search input")}) {
        return Result::failure(error);
    }
    if (producer_native.n_dims != consumer_native.n_dims) {
        return Result::failure(reject(identity, "shared search feature", "same native shape",
                                      attributeText(consumer_native), "Native rank mismatch"));
    }
    for (std::uint32_t i{}; i < producer_native.n_dims; ++i) {
        if (producer_native.dims[i] != consumer_native.dims[i]) {
            return Result::failure(reject(identity, "shared search feature", attributeText(producer_native),
                                          attributeText(consumer_native), "Native shape mismatch"));
        }
    }
    if (effectiveWidth(producer_native) != effectiveWidth(consumer_native) ||
        producer_native.size_with_stride != consumer_native.size_with_stride ||
        producer_native.qnt_type != consumer_native.qnt_type || producer_native.zp != consumer_native.zp ||
        producer_native.scale != consumer_native.scale) {
        return Result::failure(reject(identity, "shared search feature", attributeText(producer_native),
                                      attributeText(consumer_native), "Native stride, capacity or quantization mismatch"));
    }
    SharedFeatureContract contract;
    contract.producer = producer_native;
    contract.consumer = consumer_native;
    contract.consumer.pass_through = 1;
    contract.bytes = producer_native.size_with_stride;
    contract.description = "search_handoff=native_fp16_shared bind_policy=rebind_after_each_inputs_set shared_bytes=" +
                           std::to_string(contract.bytes);
    return {contract};
}
} // namespace rk3576
