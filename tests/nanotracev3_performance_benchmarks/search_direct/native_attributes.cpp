#include "native_attributes.h"

#include <cstring>
#include <limits>
#include <sstream>

namespace search_direct {
namespace {
constexpr std::uint32_t kFloat16Bytes{2};

std::uint32_t effectiveWidth(const rknn_tensor_attr &attribute) {
    // w_stride 是 SDK 标注的只读字段，0 表示"等于宽度"，因此不能按字面值比较。
    return attribute.w_stride != 0 ? attribute.w_stride : attribute.dims[3];
}

bool multiplyCapacity(std::uint64_t &product, std::uint32_t factor) {
    // SDK 的元素数和字节容量均为 uint32_t；在乘法前限制范围，避免中间结果回绕。
    if (factor == 0 || product > std::numeric_limits<std::uint32_t>::max() / factor) {
        return false;
    }
    product *= factor;
    return true;
}

model::Diagnostic reject(const std::string &identity, const std::string &tensor, const std::string &expected,
                         const std::string &actual, const std::string &message) {
    return {model::ModelError::ContractMismatch, identity, tensor, expected, actual, message};
}

/** @brief 校验单侧原生属性是否为本程序支持的 FP16 NC1HWC2 五维特征。 */
model::Diagnostic checkNativeSide(const rknn_tensor_attr &native, const rknn_tensor_attr &logical,
                                  const std::string &identity, const std::string &tensor) {
    if (native.n_dims != 5) {
        return reject(identity, tensor, "五维原生特征 [N,C1,H,W,C2]", describeAttribute(native), "原生维数不支持");
    }
    if (native.fmt != RKNN_TENSOR_NC1HWC2) {
        return reject(identity, tensor, "原生排列 NC1HWC2", describeAttribute(native), "原生排列不支持");
    }
    if (native.type != RKNN_TENSOR_FLOAT16) {
        return reject(identity, tensor, "原生元素类型 FP16", describeAttribute(native), "原生元素类型不支持");
    }
    for (std::uint32_t i{0}; i < native.n_dims; ++i) {
        if (native.dims[i] == 0) {
            return reject(identity, tensor, "所有原生维度为正", describeAttribute(native), "存在零维度");
        }
    }
    const auto shape{rk3576::nchwShape(logical)};
    if (shape.size() != 4) {
        return reject(identity, tensor, "四维普通特征 [N,C,H,W]", describeAttribute(logical), "普通形状不支持");
    }
    const auto batch{static_cast<std::uint32_t>(shape[0])};
    const auto channels{static_cast<std::uint32_t>(shape[1])};
    const auto height{static_cast<std::uint32_t>(shape[2])};
    const auto width{static_cast<std::uint32_t>(shape[3])};
    const std::uint32_t group{native.dims[4]};
    const std::uint32_t groups{native.dims[1]};
    if (native.dims[0] != batch || native.dims[2] != height || native.dims[3] != width) {
        return reject(identity, tensor, "原生批次与空间尺寸等于普通属性 " + describeAttribute(logical),
                      describeAttribute(native), "原生与普通空间尺寸不一致");
    }
    if (groups != (static_cast<std::uint64_t>(channels) + group - 1) / group) {
        return reject(identity, tensor,
                      "通道分组覆盖 " + std::to_string(channels) + " 个逻辑通道，每组 " + std::to_string(group) +
                          " 通道",
                      describeAttribute(native), "原生通道分组与逻辑通道数不一致");
    }
    std::uint64_t elements{1};
    for (std::uint32_t i{0}; i < native.n_dims; ++i) {
        if (!multiplyCapacity(elements, native.dims[i])) {
            return reject(identity, tensor, "原生维度乘积可由 SDK uint32_t 元素数表示",
                          describeAttribute(native), "原生元素数溢出");
        }
    }
    if (elements != native.n_elems) {
        return reject(identity, tensor, "原生元素数等于各维乘积 " + std::to_string(elements),
                      describeAttribute(native), "原生元素数与形状不一致");
    }
    std::uint64_t bytes{elements};
    if (!multiplyCapacity(bytes, kFloat16Bytes) || bytes != native.size) {
        return reject(identity, tensor, "原生 size 等于 n_elems × 2 且不超出 SDK uint32_t 容量",
                      describeAttribute(native), "原生字节数不一致或溢出");
    }
    if (effectiveWidth(native) < width) {
        return reject(identity, tensor, "有效宽跨度不小于逻辑宽度",
                      describeAttribute(native), "原生宽跨度不足");
    }
    std::uint64_t capacity{1};
    for (const auto factor : {batch, groups, height, effectiveWidth(native), group, kFloat16Bytes}) {
        if (!multiplyCapacity(capacity, factor)) {
            return reject(identity, tensor, "带跨度容量可由 SDK uint32_t 字节数表示",
                          describeAttribute(native), "原生带跨度容量溢出");
        }
    }
    if (capacity < bytes || capacity != native.size_with_stride) {
        return reject(identity, tensor, "带跨度容量等于按有效宽度计算的 " + std::to_string(capacity) + " 字节",
                      describeAttribute(native), "原生容量与有效跨度不一致");
    }
    if (native.qnt_type != RKNN_TENSOR_QNT_NONE && native.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC) {
        return reject(identity, tensor, "未量化或仿射量化元数据", describeAttribute(native), "量化形式不支持");
    }
    return {};
}
} // namespace

std::string describeAttribute(const rknn_tensor_attr &a) {
    std::ostringstream out;
    out << "#" << a.index << " " << std::string(a.name, strnlen(a.name, RKNN_MAX_NAME_LEN)) << " [";
    for (std::uint32_t i{0}; i < a.n_dims && i < RKNN_MAX_DIMS; ++i) {
        out << a.dims[i] << ',';
    }
    out << "] rank=" << a.n_dims << " type=" << get_type_string(a.type) << " format=" << get_format_string(a.fmt)
        << " quant=" << get_qnt_type_string(a.qnt_type) << " zp=" << a.zp << " scale=" << a.scale
        << " elems=" << a.n_elems << " bytes=" << a.size << " w_stride=" << a.w_stride
        << " effective_w_stride=" << (a.n_dims == 5 ? effectiveWidth(a) : a.w_stride)
        << " size_with_stride=" << a.size_with_stride << " pass_through=" << static_cast<int>(a.pass_through);
    return out.str();
}

model::Result<rk3576::NanoTrackRoles> resolveNanoTrackRoles(const SessionAttributes &template_session,
                                                            const SessionAttributes &search_session,
                                                            const SessionAttributes &head_session) {
    using Result = model::Result<rk3576::NanoTrackRoles>;
    for (const auto *session : {&template_session, &search_session}) {
        const bool is_template{session == &template_session};
        const int image{is_template ? 127 : 255};
        const int feature{is_template ? 8 : 16};
        if (!session->inputs || !session->outputs || session->inputs->size() != 1 || session->outputs->size() != 1 ||
            rk3576::nchwShape((*session->inputs)[0]) != TensorShape{1, 3, image, image} ||
            rk3576::nchwShape((*session->outputs)[0]) != TensorShape{1, 96, feature, feature}) {
            return Result::failure(reject(session->identity, is_template ? "template_backbone" : "search_backbone",
                                          "one image [1,3," + std::to_string(image) + "," + std::to_string(image) +
                                              "] -> one feature [1,96," + std::to_string(feature) + "," +
                                              std::to_string(feature) + "]",
                                          session->description, "Backbone shape mismatch"));
        }
    }
    if (!head_session.inputs || !head_session.outputs || head_session.inputs->size() != 2 ||
        head_session.outputs->size() != 2) {
        return Result::failure(reject(head_session.identity, "input/output count", "2/2", head_session.description,
                                      "Head requires exactly two inputs and outputs"));
    }
    rk3576::NanoTrackRoles roles;
    for (bool input : {true, false}) {
        bool first{false};
        bool second{false};
        for (const auto &a : input ? *head_session.inputs : *head_session.outputs) {
            const auto shape{rk3576::nchwShape(a)};
            if (shape == (input ? TensorShape{1, 96, 8, 8} : TensorShape{1, 2, 15, 15}) && !first) {
                first = true;
                (input ? roles.template_input : roles.classification_output) = a.index;
            } else if (shape == (input ? TensorShape{1, 96, 16, 16} : TensorShape{1, 4, 15, 15}) && !second) {
                second = true;
                (input ? roles.search_input : roles.location_output) = a.index;
            } else {
                return Result::failure(reject(head_session.identity,
                                              std::string(input ? "input#" : "output#") + std::to_string(a.index),
                                              input ? "unique template [1,96,8,8] and search [1,96,16,16]"
                                                    : "unique classification [1,2,15,15] and location [1,4,15,15]",
                                              head_session.description, "Unknown or duplicate Head role"));
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
    if (const auto error{checkNativeSide(producer_native, producer_logical, identity, "search_backbone native output")}) {
        return Result::failure(error);
    }
    if (const auto error{checkNativeSide(consumer_native, consumer_logical, identity, "head native search input")}) {
        return Result::failure(error);
    }
    const auto mismatch{[&](const std::string &message) {
        return Result::failure(reject(identity, "shared search feature",
                                      "search 输出 " + describeAttribute(producer_native),
                                      "head 搜索输入 " + describeAttribute(consumer_native), message));
    }};
    for (std::uint32_t i{0}; i < producer_native.n_dims; ++i) {
        if (producer_native.dims[i] != consumer_native.dims[i]) {
            return mismatch("两端原生形状不同");
        }
    }
    if (effectiveWidth(producer_native) != effectiveWidth(consumer_native)) {
        return mismatch("两端有效行跨度不同");
    }
    if (producer_native.size_with_stride != consumer_native.size_with_stride) {
        return mismatch("两端带跨度容量不同");
    }
    if (producer_native.qnt_type != consumer_native.qnt_type || producer_native.zp != consumer_native.zp ||
        producer_native.scale != consumer_native.scale) {
        // 浮点特征不做反量化；两端量化元数据必须一致，否则共享会隐含一次未执行的缩放。
        return mismatch("两端量化元数据不同");
    }
    SharedFeatureContract contract;
    contract.producer = producer_native;
    contract.consumer = consumer_native;
    // pass_through=1 表示 head 按原样读取共享内存中的原生特征，不做任何格式或类型转换。
    contract.consumer.pass_through = 1;
    contract.bytes = producer_native.size_with_stride;
    std::ostringstream description;
    description << "search_handoff=native_fp16_shared bind_policy=rebind_after_each_inputs_set shared_bytes="
                << contract.bytes << "\nsearch native output " << describeAttribute(contract.producer)
                << "\nhead native search input " << describeAttribute(contract.consumer) << '\n';
    contract.description = description.str();
    return {contract};
}
} // namespace search_direct
