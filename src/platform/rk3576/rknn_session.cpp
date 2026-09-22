#include "rknn_session.h"
#include "timing_observer.h"

#include <algorithm>
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
    return load(path, api, {});
}
model::Result<std::unique_ptr<RknnSession>> RknnSession::load(const std::string &path, RknnApi api,
                                                              RknnSharedMemoryApi shared_api) {
    using Result = model::Result<std::unique_ptr<RknnSession>>;
    if (!api.init || !api.query || !api.inputs_set || !api.run || !api.wait || !api.outputs_get ||
        !api.outputs_release || !api.destroy) {
        return Result::failure(
            {model::ModelError::LoadFailure, path, "API", "complete SDK boundary", {}, "Missing API"});
    }
    auto runtime{std::make_unique<RknnSession>(path, api, shared_api)};
    try {
        if (const auto error{runtime->initialize()}) {
            return Result::failure(error);
        }
        return {std::move(runtime)};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, path, {}, {}, {}, error.what()});
    }
}
model::Diagnostic RknnSession::queryNative(std::vector<rknn_tensor_attr> &target, bool input) {
    for (std::size_t i{}; i < target.size(); ++i) {
        auto &attribute{target[i]};
        attribute = {};
        attribute.index = static_cast<std::uint32_t>(i);
        const int status{api_.query(context_, input ? RKNN_QUERY_NATIVE_INPUT_ATTR : RKNN_QUERY_NATIVE_OUTPUT_ATTR,
                                    &attribute, sizeof(attribute))};
        if (status != RKNN_SUCC) {
            return failure(std::string("query native ") + (input ? "input#" : "output#") + std::to_string(i), status);
        }
        if (attribute.index != i) {
            return attributeMismatch(path_, attribute, input, "queried native tensor index " + std::to_string(i));
        }
    }
    return {};
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
    native_inputs_.resize(inputs_.size());
    native_outputs_.resize(outputs_.size());
    if (const auto error{queryNative(native_inputs_, true)}) {
        return error;
    }
    if (const auto error{queryNative(native_outputs_, false)}) {
        return error;
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
    while (!memories_.empty()) {
        auto *memory{memories_.back()};
        memories_.pop_back();
        if (context_ == 0 || !shared_api_.destroy_mem || shared_api_.destroy_mem(context_, memory) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "Shared device memory release failed during teardown");
        }
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
model::Status RknnSession::submit(const std::vector<SessionInput> &inputs, bool allow_partial) {
    using Result = model::Status;
    if (!ready_) {
        return Result::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    if (inputs.empty() || inputs.size() > inputs_.size() || (!allow_partial && inputs.size() != inputs_.size())) {
        return Result::failure({model::ModelError::ContractMismatch, path_, "inputs",
                                allow_partial ? "1.." + std::to_string(inputs_.size())
                                              : std::to_string(inputs_.size()),
                                std::to_string(inputs.size()), "Input count mismatch"});
    }
    timing::Span input_prepare{timing::Stage::InputPrepare};
    std::vector<rknn_input> submissions(inputs.size());
    std::vector<bool> seen(inputs_.size(), false);
    for (std::size_t position{}; position < inputs.size(); ++position) {
        const auto &input{inputs[position]};
        const auto name{"input#" + std::to_string(input.index)};
        if (input.index >= inputs_.size() || seen[input.index]) {
            return Result::failure({model::ModelError::ContractMismatch, path_, name, "unique valid index",
                                    std::to_string(input.index), "Duplicate or invalid input index"});
        }
        seen[input.index] = true;
        const auto &a{inputs_[input.index]};
        const auto &d{input.descriptor};
        TensorShape expected;
        for (std::size_t axis{}; axis < a.n_dims; ++axis) {
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
        auto &submission{submissions[inputs.size() == inputs_.size() ? input.index : position]};
        submission.index = input.index;
        submission.buf = const_cast<void *>(input.data);
        submission.size = static_cast<std::uint32_t>(input.bytes);
        submission.pass_through = 0;
        submission.type = d.type == TensorElementType::Float32 ? RKNN_TENSOR_FLOAT32 : RKNN_TENSOR_UINT8;
        submission.fmt = a.fmt;
    }
    input_prepare.finish();
    timing::Span inputs_set{timing::Stage::InputsSet};
    const int status{api_.inputs_set(context_, static_cast<std::uint32_t>(submissions.size()), submissions.data())};
    inputs_set.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("inputs_set", status)};
        invalidate();
        return Result::failure(error);
    }
    return {true};
}
model::Status RknnSession::bind(const rknn_tensor_attr &attribute, rknn_tensor_mem *memory) {
    using Result = model::Status;
    if (!ready_ || !shared_api_.complete()) {
        return Result::failure({model::ModelError::NotReady, path_, "set_io_mem", "shared memory extension",
                                "not provided", "Shared memory capability unavailable"});
    }
    if (!memory || !memory->virt_addr || memory->offset < 0 ||
        static_cast<std::uint32_t>(memory->offset) > memory->size ||
        memory->size - static_cast<std::uint32_t>(memory->offset) < attribute.size_with_stride) {
        return Result::failure({model::ModelError::ContractMismatch, path_,
                                "io_mem#" + std::to_string(attribute.index),
                                "device memory covering " + std::to_string(attribute.size_with_stride) +
                                    " bytes after its offset",
                                memory ? std::to_string(memory->size) : "null", "Shared memory too small"});
    }
    timing::Span io_mem_bind{timing::Stage::IoMemBind};
    rknn_tensor_attr request{attribute};
    const int status{shared_api_.set_io_mem(context_, memory, &request)};
    io_mem_bind.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("set_io_mem#" + std::to_string(attribute.index), status)};
        ready_ = false;
        return Result::failure(error);
    }
    return {true};
}
model::Status RknnSession::run() {
    using Result = model::Status;
    if (!ready_) {
        return Result::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    running_ = true;
    timing::Span run_span{timing::Stage::Run};
    const int status{api_.run(context_, nullptr)};
    run_span.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("run", status)};
        confirmStopped();
        invalidate();
        return Result::failure(error);
    }
    return {true};
}
void RknnSession::confirmCompleted() {
    confirmStopped();
}
model::ExecutionResult RknnSession::fetch() {
    using Result = model::ExecutionResult;
    if (!ready_) {
        return Result::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    std::vector<rknn_output> outputs(outputs_.size());
    for (std::size_t i{}; i < outputs.size(); ++i) {
        outputs[i].index = static_cast<std::uint32_t>(i);
        outputs[i].want_float = 1;
    }
    timing::Span outputs_get{timing::Stage::OutputsGet};
    int status{api_.outputs_get(context_, static_cast<std::uint32_t>(outputs.size()), outputs.data(), nullptr)};
    outputs_get.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("outputs_get", status)};
        confirmStopped();
        invalidate();
        return Result::failure(error);
    }
    running_ = false;
    Result result;
    timing::Span output_copy{timing::Stage::OutputCopy};
    try {
        for (std::size_t i{}; i < outputs.size(); ++i) {
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
    output_copy.finish(static_cast<bool>(result));
    timing::Span outputs_release{timing::Stage::OutputsRelease};
    status = api_.outputs_release(context_, static_cast<std::uint32_t>(outputs.size()), outputs.data());
    outputs_release.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        result = Result::failure(failure("outputs_release", status));
    }
    if (!result) {
        invalidate();
    }
    return result;
}
model::ExecutionResult RknnSession::execute(const std::vector<SessionInput> &inputs) {
    if (inputs.size() != inputs_.size()) {
        return model::ExecutionResult::failure({model::ModelError::ContractMismatch, path_, "inputs",
                                                std::to_string(inputs_.size()), std::to_string(inputs.size()),
                                                "Input count mismatch"});
    }
    if (const auto submitted{submit(inputs, false)}; !submitted) {
        return model::ExecutionResult::failure(submitted.error);
    }
    if (const auto executed{run()}; !executed) {
        return model::ExecutionResult::failure(executed.error);
    }
    return fetch();
}
model::Result<rknn_tensor_mem *> RknnSession::allocateShared(std::uint32_t bytes) {
    using Result = model::Result<rknn_tensor_mem *>;
    if (!ready_ || !shared_api_.complete() || bytes == 0) {
        return Result::failure({model::ModelError::NotReady, path_, "create_mem", "shared memory capability and size",
                                "unavailable", "Shared memory capability unavailable"});
    }
    auto *memory{shared_api_.create_mem(context_, bytes)};
    if (!memory || !memory->virt_addr || memory->size < bytes || memory->fd < 0 || memory->offset < 0) {
        if (memory) {
            shared_api_.destroy_mem(context_, memory);
        }
        return Result::failure({model::ModelError::ExecutionFailure, path_, "create_mem",
                                std::to_string(bytes), "incomplete memory handle", "Shared allocation failed"});
    }
    memories_.push_back(memory);
    return {memory};
}
model::Result<rknn_tensor_mem *> RknnSession::importShared(const rknn_tensor_mem &source) {
    using Result = model::Result<rknn_tensor_mem *>;
    if (!ready_ || !shared_api_.complete() || source.fd < 0 || !source.virt_addr || source.size == 0) {
        return Result::failure({model::ModelError::NotReady, path_, "create_mem_from_fd", "valid shared memory",
                                "invalid source", "Cannot import shared device memory"});
    }
    auto *memory{shared_api_.create_mem_from_fd(context_, source.fd, source.virt_addr, source.size, source.offset)};
    if (!memory || memory->virt_addr != source.virt_addr || memory->fd != source.fd || memory->size < source.size) {
        if (memory) {
            shared_api_.destroy_mem(context_, memory);
        }
        return Result::failure({model::ModelError::ExecutionFailure, path_, "create_mem_from_fd",
                                "same shared storage", "different storage", "Shared import failed"});
    }
    memories_.push_back(memory);
    return {memory};
}
model::Status RknnSession::releaseShared(rknn_tensor_mem *memory) {
    if (!memory) {
        return {true};
    }
    const auto position{std::find(memories_.begin(), memories_.end(), memory)};
    if (position == memories_.end()) {
        return {true};
    }
    memories_.erase(position);
    if (!shared_api_.complete() || context_ == 0 ||
        shared_api_.destroy_mem(context_, memory) != RKNN_SUCC) {
        model::terminateDeviceFailure(path_.c_str(), "Shared device memory release failed");
    }
    return {true};
}
model::Status RknnSession::syncFromDevice(rknn_tensor_mem *memory) {
    using Result = model::Status;
    if (!memory || !shared_api_.complete() || context_ == 0) {
        return Result::failure({model::ModelError::NotReady, path_, "mem_sync", "shared memory capability",
                                "unavailable", "Cannot synchronise shared memory"});
    }
    const int status{shared_api_.mem_sync(context_, memory, RKNN_MEMORY_SYNC_FROM_DEVICE)};
    if (status != RKNN_SUCC) {
        return Result::failure(failure("mem_sync", status));
    }
    return {true};
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
