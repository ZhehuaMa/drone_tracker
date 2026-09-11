#include "rknn_session.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

#include "model/validation.h"

namespace rk3576 {
namespace {
class OutputStorage final : public model::TensorStorage {
public:
    explicit OutputStorage(std::size_t count) : values(count) {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};
std::string descriptorText(const SessionInput &input) {
    std::ostringstream out;
    out << "shape=[";
    for (const auto extent : input.descriptor.shape) {
        out << extent << ',';
    }
    out << "] type=" << static_cast<int>(input.descriptor.type)
        << " layout=" << static_cast<int>(input.descriptor.layout) << " bytes=" << input.bytes
        << " descriptor_bytes=" << input.descriptor.bytes;
    return out.str();
}
std::string attributeText(const rknn_tensor_attr &a) {
    std::ostringstream out;
    out << "#" << a.index << " " << std::string(a.name, strnlen(a.name, RKNN_MAX_NAME_LEN)) << " [";
    for (std::size_t i{0}; i < a.n_dims && i < RKNN_MAX_DIMS; ++i) {
        out << a.dims[i] << ',';
    }
    out << "] rank=" << a.n_dims << " type=" << get_type_string(a.type) << " format=" << get_format_string(a.fmt)
        << " quant=" << get_qnt_type_string(a.qnt_type) << " zp=" << a.zp << " scale=" << a.scale
        << " elems=" << a.n_elems << " bytes=" << a.size << " w_stride=" << a.w_stride << " h_stride=" << a.h_stride
        << " size_with_stride=" << a.size_with_stride << " pass_through=" << static_cast<int>(a.pass_through)
        << " fl=" << static_cast<int>(a.fl);
    return out.str();
}
model::Diagnostic attributeMismatch(const std::string &path, const rknn_tensor_attr &a, bool input,
                                    const std::string &expected) {
    return {model::ModelError::ContractMismatch,
            path,
            std::string(input ? "input " : "output ") + std::to_string(a.index) + " " +
                std::string(a.name, strnlen(a.name, RKNN_MAX_NAME_LEN)),
            expected,
            attributeText(a),
            "RKNN contract mismatch"};
}
} // namespace
model::Diagnostic RknnSession::failure(const std::string &operation, int status) const {
    return {
        model::ModelError::ExecutionFailure, path_, operation, "RKNN_SUCC", std::to_string(status), "RKNN call failed"};
}
model::Result<std::unique_ptr<RknnSession>> RknnSession::load(const std::string &path, RknnApi api) {
    using Result = model::Result<std::unique_ptr<RknnSession>>;
    if (!api.init || !api.query || !api.inputs_set || !api.run || !api.wait || !api.outputs_get ||
        !api.outputs_release || !api.destroy) {
        return Result::failure(
            {model::ModelError::LoadFailure, path, "API", "complete SDK boundary", {}, "Missing API"});
    }
    auto runtime{std::make_unique<RknnSession>(path, api)};
    try {
        if (const auto error{runtime->initialize()}) {
            return Result::failure(error);
        }
        return {std::move(runtime)};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, path, {}, {}, {}, error.what()});
    }
}
model::Diagnostic RknnSession::checkAttribute(const rknn_tensor_attr &a, bool input) {
    if (a.n_dims == 0 || a.n_dims > RKNN_MAX_DIMS) {
        return attributeMismatch(path_, a, input, "resolved positive rank within RKNN_MAX_DIMS");
    }
    TensorShape shape;
    for (std::size_t i{0}; i < a.n_dims; ++i) {
        if (a.dims[i] == 0 || a.dims[i] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            return attributeMismatch(path_, a, input, "positive int32 dimensions");
        }
        shape.push_back(static_cast<std::int32_t>(a.dims[i]));
    }
    const auto bytes{model::tensorBytes(shape, TensorElementType::Float32, path_, attributeText(a))};
    if (!bytes || bytes.value > std::numeric_limits<std::uint32_t>::max() || bytes.value / sizeof(float) != a.n_elems) {
        return attributeMismatch(path_, a, input, "logical element count and Float32 byte count without overflow");
    }
    const std::size_t element_size{a.type == RKNN_TENSOR_INT8 ? 1U : a.type == RKNN_TENSOR_FLOAT16 ? 2U : 4U};
    if ((a.type != RKNN_TENSOR_INT8 && a.type != RKNN_TENSOR_FLOAT16 && a.type != RKNN_TENSOR_FLOAT32) ||
        a.size != a.n_elems * element_size) {
        return attributeMismatch(path_, a, input, "INT8/FP16/Float32 logical bytes");
    }
    if (a.qnt_type != RKNN_TENSOR_QNT_NONE && a.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC) {
        return attributeMismatch(path_, a, input, "unquantized or affine logical tensor");
    }
    if (a.type == RKNN_TENSOR_INT8 && (a.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC || !std::isfinite(a.scale) ||
                                       a.scale <= 0 || a.zp < -128 || a.zp > 127)) {
        return attributeMismatch(path_, a, input, "INT8 affine quantization, positive finite scale, int8 zero point");
    }
    if (a.fmt != RKNN_TENSOR_NCHW && a.fmt != RKNN_TENSOR_NHWC) {
        return attributeMismatch(path_, a, input, "NCHW or NHWC logical layout");
    }
    if (!input) {
        descriptors_.push_back({shape,
                                TensorElementType::Float32,
                                a.fmt == RKNN_TENSOR_NCHW ? TensorLayout::Nchw : TensorLayout::Nhwc,
                                bytes.value,
                                {}});
    }
    return {};
}
model::Diagnostic RknnSession::initialize() {
    // size=0 让 SDK 按路径加载；不启用异步或零拷贝标志。
    int status{api_.init(&context_, const_cast<char *>(path_.c_str()), 0, 0, nullptr)};
    if (status != RKNN_SUCC) {
        auto error{failure("init", status)};
        error.code = model::ModelError::LoadFailure;
        return error;
    }
    if (context_ == 0) {
        return {model::ModelError::LoadFailure, path_, "context", "nonzero handle", "0", "Invalid initialized context"};
    }
    status = api_.query(context_, RKNN_QUERY_SDK_VERSION, &version_, sizeof(version_));
    if (status != RKNN_SUCC) {
        return failure("query version", status);
    }
    rknn_input_output_num count{};
    status = api_.query(context_, RKNN_QUERY_IN_OUT_NUM, &count, sizeof(count));
    if (status != RKNN_SUCC) {
        return failure("query counts", status);
    }
    if (count.n_input == 0 || count.n_output == 0) {
        return {model::ModelError::ContractMismatch,
                path_,
                "input/output count",
                "positive counts",
                std::to_string(count.n_input) + "/" + std::to_string(count.n_output),
                "Empty model"};
    }
    inputs_.resize(count.n_input);
    outputs_.resize(count.n_output);
    for (bool input : {true, false}) {
        auto &attributes{input ? inputs_ : outputs_};
        for (std::size_t i{0}; i < attributes.size(); ++i) {
            auto &attribute{attributes[i]};
            attribute.index = static_cast<std::uint32_t>(i);
            status = api_.query(context_, input ? RKNN_QUERY_INPUT_ATTR : RKNN_QUERY_OUTPUT_ATTR, &attribute,
                                sizeof(attribute));
            if (status != RKNN_SUCC) {
                return failure(std::string("query ") + (input ? "input#" : "output#") + std::to_string(i), status);
            }
            if (attribute.index != i) {
                return attributeMismatch(path_, attribute, input, "queried tensor index " + std::to_string(i));
            }
            if (const auto error{checkAttribute(attribute, input)}) {
                return error;
            }
        }
    }
    ready_ = true;
    return {};
}
void RknnSession::confirmStopped() {
    if (running_) {
        // 负返回值本身不证明停止。仅成功 wait 构成完成证据。
        if (api_.wait(context_, nullptr) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "RKNN wait failed; device access cannot be excluded");
        }
        running_ = false;
    }
}
void RknnSession::invalidate() noexcept {
    ready_ = false;
    if (running_) {
        model::terminateDeviceFailure(path_.c_str(), "Destroy attempted before confirmed completion");
    }
    if (context_ != 0) {
        const auto context{context_};
        context_ = 0;
        if (api_.destroy(context) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "RKNN context destruction failed");
        }
    }
}
RknnSession::~RknnSession() noexcept { invalidate(); }
model::ExecutionResult RknnSession::execute(const std::vector<SessionInput> &inputs) {
    using Result = model::ExecutionResult;
    if (!ready_) {
        return Result::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    if (inputs.size() != inputs_.size()) {
        return Result::failure({model::ModelError::ContractMismatch, path_, "inputs", std::to_string(inputs_.size()),
                                std::to_string(inputs.size()), "Input count mismatch"});
    }
    std::vector<rknn_input> submissions(inputs.size());
    std::vector<bool> seen(inputs.size(), false);
    for (const auto &input : inputs) {
        const auto name{"input#" + std::to_string(input.index)};
        if (input.index >= inputs_.size() || seen[input.index]) {
            return Result::failure({model::ModelError::ContractMismatch, path_, name, "unique valid index",
                                    std::to_string(input.index), "Duplicate or invalid input index"});
        }
        seen[input.index] = true;
        const auto &a{inputs_[input.index]};
        const auto &d{input.descriptor};
        TensorShape expected;
        for (std::size_t axis{0}; axis < a.n_dims; ++axis) {
            expected.push_back(static_cast<std::int32_t>(a.dims[axis]));
        }
        const auto layout{a.fmt == RKNN_TENSOR_NCHW ? TensorLayout::Nchw : TensorLayout::Nhwc};
        auto bytes{model::tensorBytes(d.shape, TensorElementType::Float32, path_, name)};
        if (bytes && d.type == TensorElementType::Uint8) {
            bytes.value /= sizeof(float);
        }
        if (!bytes) {
            return Result::failure(bytes.error);
        }
        if (d.shape != expected || d.layout != layout ||
            (d.type != TensorElementType::Float32 && d.type != TensorElementType::Uint8) || d.quantization ||
            bytes.value > std::numeric_limits<std::uint32_t>::max() || d.bytes != bytes.value ||
            input.bytes != bytes.value) {
            return Result::failure({model::ModelError::ContractMismatch, path_, name,
                                    attributeText(a) + "; Float32/Uint8 submission with exact logical bytes",
                                    descriptorText(input), "Input shape/type/layout/size mismatch"});
        }
        if (!input.owner || !input.data ||
            (d.type == TensorElementType::Float32 &&
             reinterpret_cast<std::uintptr_t>(input.data) % alignof(float) != 0)) {
            return Result::failure({model::ModelError::PlatformTypeMismatch, path_, name, "owned, aligned host storage",
                                    "missing or misaligned storage", "Invalid storage"});
        }
        auto &submission{submissions[input.index]};
        submission.index = input.index;
        submission.buf = const_cast<void *>(input.data);
        submission.size = static_cast<std::uint32_t>(input.bytes);
        submission.type = d.type == TensorElementType::Float32 ? RKNN_TENSOR_FLOAT32 : RKNN_TENSOR_UINT8;
        submission.fmt = a.fmt;
    }
    // 在设备提交前完成可能分配内存的请求数组准备。
    std::vector<rknn_output> outputs(outputs_.size());
    int status{api_.inputs_set(context_, static_cast<std::uint32_t>(submissions.size()), submissions.data())};
    if (status != RKNN_SUCC) {
        const auto error{failure("inputs_set", status)};
        invalidate(); // 本次尚未提交 run。
        return Result::failure(error);
    }
    running_ = true;
    status = api_.run(context_, nullptr);
    if (status != RKNN_SUCC) {
        const auto error{failure("run", status)};
        confirmStopped();
        invalidate();
        return Result::failure(error);
    }
    for (std::size_t i{0}; i < outputs.size(); ++i) {
        outputs[i].index = static_cast<std::uint32_t>(i);
        outputs[i].want_float = 1;
    }
    status = api_.outputs_get(context_, static_cast<std::uint32_t>(outputs.size()), outputs.data(), nullptr);
    if (status != RKNN_SUCC) {
        const auto error{failure("outputs_get", status)};
        confirmStopped();
        // get 失败未转移输出所有权；不 release 不确定/部分 SDK 指针，销毁 context 回收。
        invalidate();
        return Result::failure(error);
    }
    running_ = false; // 成功 get 的 API 契约明确等待推理结束。
    Result result;
    try {
        for (std::size_t i{0}; i < outputs.size(); ++i) {
            if (!outputs[i].buf || outputs[i].size != descriptors_[i].bytes) {
                result = Result::failure({model::ModelError::ContractMismatch, path_, "output#" + std::to_string(i),
                                          std::to_string(descriptors_[i].bytes) + " Float32 bytes",
                                          std::to_string(outputs[i].size), "Invalid converted output buffer"});
                break;
            }
            auto storage{std::make_shared<OutputStorage>(descriptors_[i].bytes / sizeof(float))};
            std::memcpy(storage->values.data(), outputs[i].buf, descriptors_[i].bytes);
            result.value.push_back({descriptors_[i], std::move(storage)});
        }
    } catch (const std::exception &error) {
        result = Result::failure({model::ModelError::ExecutionFailure, path_, "copy outputs", {}, {}, error.what()});
    }
    status = api_.outputs_release(context_, static_cast<std::uint32_t>(outputs.size()), outputs.data());
    if (status != RKNN_SUCC) {
        result = Result::failure(failure("outputs_release", status));
    }
    if (!result) {
        invalidate();
    }
    return result;
}
std::string RknnSession::describe() const {
    std::ostringstream out;
    out.precision(10);
    out << "model=" << path_ << " runtime=" << version_.api_version << " driver=" << version_.drv_version << "\n";
    for (const auto &a : inputs_) {
        out << "input " << attributeText(a) << '\n';
    }
    for (const auto &a : outputs_) {
        out << "output " << attributeText(a) << '\n';
    }
    return out.str();
}
} // namespace rk3576
