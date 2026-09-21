#include "direct_session.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

#include "model/validation.h"
#include "timing_observer.h"

namespace search_direct {
namespace {
using Stage = timing::Stage;

class OutputStorage final : public model::TensorStorage {
public:
    explicit OutputStorage(std::size_t count) : values(count) {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};

std::string descriptorText(const rk3576::SessionInput &input) {
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

model::Diagnostic attributeMismatch(const std::string &path, const rknn_tensor_attr &a, bool input,
                                    const std::string &expected) {
    return {model::ModelError::ContractMismatch,
            path,
            std::string(input ? "input " : "output ") + std::to_string(a.index) + " " +
                std::string(a.name, strnlen(a.name, RKNN_MAX_NAME_LEN)),
            expected,
            describeAttribute(a),
            "RKNN contract mismatch"};
}
} // namespace

model::Diagnostic DirectSession::failure(const std::string &operation, int status) const {
    return {
        model::ModelError::ExecutionFailure, path_, operation, "RKNN_SUCC", std::to_string(status), "RKNN call failed"};
}

model::Result<std::unique_ptr<DirectSession>> DirectSession::load(const std::string &path, SharedMemoryApi api) {
    using Result = model::Result<std::unique_ptr<DirectSession>>;
    if (!api.complete()) {
        return Result::failure(
            {model::ModelError::LoadFailure, path, "API", "complete SDK and device memory boundary", {},
             "Missing API"});
    }
    auto session{std::make_unique<DirectSession>(path, api)};
    try {
        if (const auto error{session->initialize()}) {
            return Result::failure(error);
        }
        return {std::move(session)};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, path, {}, {}, {}, error.what()});
    }
}

model::Diagnostic DirectSession::checkAttribute(const rknn_tensor_attr &a, bool input) {
    if (a.n_dims == 0 || a.n_dims > RKNN_MAX_DIMS) {
        return attributeMismatch(path_, a, input, "resolved positive rank within RKNN_MAX_DIMS");
    }
    TensorShape shape;
    for (std::uint32_t i{0}; i < a.n_dims; ++i) {
        if (a.dims[i] == 0 || a.dims[i] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            return attributeMismatch(path_, a, input, "positive int32 dimensions");
        }
        shape.push_back(static_cast<std::int32_t>(a.dims[i]));
    }
    const auto bytes{model::tensorBytes(shape, TensorElementType::Float32, path_, describeAttribute(a))};
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

model::Diagnostic DirectSession::queryNative(std::vector<rknn_tensor_attr> &target, bool input) {
    for (std::size_t i{0}; i < target.size(); ++i) {
        auto &attribute{target[i]};
        attribute = {};
        attribute.index = static_cast<std::uint32_t>(i);
        const int status{api_.core.query(context_,
                                         input ? RKNN_QUERY_NATIVE_INPUT_ATTR : RKNN_QUERY_NATIVE_OUTPUT_ATTR,
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

model::Diagnostic DirectSession::initialize() {
    // size=0 让 SDK 按路径加载；flags=0 表示不启用异步运行，阻塞运行返回即代表推理结束。
    init_flags_ = 0;
    int status{api_.core.init(&context_, const_cast<char *>(path_.c_str()), 0, init_flags_, nullptr)};
    if (status != RKNN_SUCC) {
        auto error{failure("init", status)};
        error.code = model::ModelError::LoadFailure;
        return error;
    }
    if (context_ == 0) {
        return {model::ModelError::LoadFailure, path_, "context", "nonzero handle", "0", "Invalid initialized context"};
    }
    status = api_.core.query(context_, RKNN_QUERY_SDK_VERSION, &version_, sizeof(version_));
    if (status != RKNN_SUCC) {
        return failure("query version", status);
    }
    rknn_input_output_num count{};
    status = api_.core.query(context_, RKNN_QUERY_IN_OUT_NUM, &count, sizeof(count));
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
            status = api_.core.query(context_, input ? RKNN_QUERY_INPUT_ATTR : RKNN_QUERY_OUTPUT_ATTR, &attribute,
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
    native_inputs_.resize(count.n_input);
    native_outputs_.resize(count.n_output);
    for (bool input : {true, false}) {
        if (const auto error{queryNative(input ? native_inputs_ : native_outputs_, input)}) {
            return error;
        }
    }
    ready_ = true;
    return {};
}

void DirectSession::confirmStopped() {
    if (running_) {
        // 负返回值本身不证明停止。仅成功 wait 构成完成证据。
        if (api_.core.wait(context_, nullptr) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "RKNN wait failed; device access cannot be excluded");
        }
        running_ = false;
    }
}

void DirectSession::confirmCompleted() {
    if (init_flags_ != 0) {
        model::terminateDeviceFailure(path_.c_str(), "Synchronous run assumed but asynchronous flags were used");
    }
    running_ = false;
}

void DirectSession::invalidate() noexcept {
    ready_ = false;
    if (running_) {
        model::terminateDeviceFailure(path_.c_str(), "Destroy attempted before confirmed completion");
    }
    // 先释放本会话仍登记的共享内存，再销毁 context；顺序相反会让内存句柄失去归属。
    while (!memories_.empty()) {
        auto *memory{memories_.back()};
        memories_.pop_back();
        if (context_ == 0 || api_.destroy_mem(context_, memory) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "Shared device memory release failed during teardown");
        }
    }
    if (context_ != 0) {
        const auto context{context_};
        context_ = 0;
        if (api_.core.destroy(context) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "RKNN context destruction failed");
        }
    }
}

DirectSession::~DirectSession() noexcept { invalidate(); }

model::Status DirectSession::submit(const std::vector<rk3576::SessionInput> &inputs) {
    using Status = model::Status;
    if (!ready_) {
        return Status::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    if (inputs.empty() || inputs.size() > inputs_.size()) {
        return Status::failure({model::ModelError::ContractMismatch, path_, "inputs",
                                "1.." + std::to_string(inputs_.size()), std::to_string(inputs.size()),
                                "Input count out of range"});
    }
    timing::Span input_prepare{Stage::InputPrepare};
    std::vector<rknn_input> submissions(inputs.size());
    std::vector<bool> seen(inputs_.size(), false);
    for (std::size_t position{0}; position < inputs.size(); ++position) {
        const auto &input{inputs[position]};
        const auto name{"input#" + std::to_string(input.index)};
        if (input.index >= inputs_.size() || seen[input.index]) {
            return Status::failure({model::ModelError::ContractMismatch, path_, name, "unique valid index",
                                    std::to_string(input.index), "Duplicate or invalid input index"});
        }
        seen[input.index] = true;
        const auto &a{inputs_[input.index]};
        const auto &d{input.descriptor};
        TensorShape expected;
        for (std::uint32_t axis{0}; axis < a.n_dims; ++axis) {
            expected.push_back(static_cast<std::int32_t>(a.dims[axis]));
        }
        const auto layout{a.fmt == RKNN_TENSOR_NCHW ? TensorLayout::Nchw : TensorLayout::Nhwc};
        auto bytes{model::tensorBytes(d.shape, TensorElementType::Float32, path_, name)};
        if (!bytes) {
            return Status::failure(bytes.error);
        }
        if (d.shape != expected || d.layout != layout || d.type != TensorElementType::Float32 || d.quantization ||
            bytes.value > std::numeric_limits<std::uint32_t>::max() || d.bytes != bytes.value ||
            input.bytes != bytes.value) {
            return Status::failure({model::ModelError::ContractMismatch, path_, name,
                                    describeAttribute(a) + "; Float32 submission with exact logical bytes",
                                    descriptorText(input), "Input shape/type/layout/size mismatch"});
        }
        if (!input.owner || !input.data || reinterpret_cast<std::uintptr_t>(input.data) % alignof(float) != 0) {
            return Status::failure({model::ModelError::PlatformTypeMismatch, path_, name,
                                    "owned, aligned host storage", "missing or misaligned storage",
                                    "Invalid storage"});
        }
        auto &submission{submissions[position]};
        submission.index = input.index;
        submission.buf = const_cast<void *>(input.data);
        submission.size = static_cast<std::uint32_t>(input.bytes);
        submission.pass_through = 0;
        submission.type = RKNN_TENSOR_FLOAT32;
        submission.fmt = a.fmt;
    }
    input_prepare.finish();
    timing::Span inputs_set{Stage::InputsSet};
    const int status{api_.core.inputs_set(context_, static_cast<std::uint32_t>(submissions.size()),
                                          submissions.data())};
    inputs_set.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("inputs_set", status)};
        markFailed(); // 本次尚未提交 run；共享内存留给统一的所有权顺序释放。
        return Status::failure(error);
    }
    return {true};
}

model::Status DirectSession::bind(const rknn_tensor_attr &attribute, rknn_tensor_mem *memory) {
    using Status = model::Status;
    if (!ready_) {
        return Status::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    if (!memory || !memory->virt_addr || memory->offset < 0 ||
        static_cast<std::uint32_t>(memory->offset) > memory->size ||
        memory->size - static_cast<std::uint32_t>(memory->offset) < attribute.size_with_stride) {
        return Status::failure({model::ModelError::ContractMismatch, path_,
                                "io_mem#" + std::to_string(attribute.index),
                                "device memory covering " + std::to_string(attribute.size_with_stride) +
                                    " bytes after its offset",
                                memory ? std::to_string(memory->size) + " bytes at offset " +
                                             std::to_string(memory->offset)
                                       : "null memory",
                                "Shared memory too small for the bound tensor"});
    }
    timing::Span io_mem_bind{Stage::IoMemBind};
    rknn_tensor_attr request{attribute};
    const int status{api_.set_io_mem(context_, memory, &request)};
    io_mem_bind.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("set_io_mem#" + std::to_string(attribute.index), status)};
        markFailed(); // 绑定失败发生在 run 之前，本次没有设备执行需要等待。
        return Status::failure(error);
    }
    return {true};
}

model::Status DirectSession::run() {
    using Status = model::Status;
    if (!ready_) {
        return Status::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    running_ = true;
    timing::Span run_span{Stage::Run};
    const int status{api_.core.run(context_, nullptr)};
    run_span.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("run", status)};
        confirmStopped();
        markFailed();
        return Status::failure(error);
    }
    return {true};
}

model::ExecutionResult DirectSession::fetch() {
    using Result = model::ExecutionResult;
    if (!ready_) {
        return Result::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    std::vector<rknn_output> outputs(outputs_.size());
    for (std::size_t i{0}; i < outputs.size(); ++i) {
        outputs[i].index = static_cast<std::uint32_t>(i);
        outputs[i].want_float = 1;
    }
    timing::Span outputs_get{Stage::OutputsGet};
    int status{api_.core.outputs_get(context_, static_cast<std::uint32_t>(outputs.size()), outputs.data(), nullptr)};
    outputs_get.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        const auto error{failure("outputs_get", status)};
        confirmStopped();
        // get 失败未转移输出所有权；不 release 不确定/部分 SDK 指针，交由销毁流程回收。
        markFailed();
        return Result::failure(error);
    }
    running_ = false; // 成功 get 的 API 契约明确等待推理结束。
    Result result;
    timing::Span output_copy{Stage::OutputCopy};
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
    output_copy.finish(static_cast<bool>(result));
    timing::Span outputs_release{Stage::OutputsRelease};
    status = api_.core.outputs_release(context_, static_cast<std::uint32_t>(outputs.size()), outputs.data());
    outputs_release.finish(status == RKNN_SUCC);
    if (status != RKNN_SUCC) {
        result = Result::failure(failure("outputs_release", status));
    }
    if (!result) {
        markFailed();
    }
    return result;
}

model::Result<rknn_tensor_mem *> DirectSession::allocate(std::uint32_t bytes) {
    using Result = model::Result<rknn_tensor_mem *>;
    if (!ready_ || bytes == 0) {
        return Result::failure({model::ModelError::NotReady, path_, "create_mem", "ready session and positive size",
                                std::to_string(bytes), "Cannot allocate shared device memory"});
    }
    auto *memory{api_.create_mem(context_, bytes)};
    if (!memory || !memory->virt_addr || memory->size < bytes || memory->fd < 0 || memory->offset < 0) {
        if (memory) {
            releaseMemory(memory);
        }
        return Result::failure({model::ModelError::ExecutionFailure, path_, "create_mem",
                                std::to_string(bytes) + " bytes with a shareable descriptor",
                                memory ? "incomplete memory handle" : "null", "Shared allocation failed"});
    }
    memories_.push_back(memory);
    return {memory};
}

model::Result<rknn_tensor_mem *> DirectSession::importFrom(const rknn_tensor_mem &source) {
    using Result = model::Result<rknn_tensor_mem *>;
    if (!ready_ || source.fd < 0 || !source.virt_addr || source.size == 0) {
        return Result::failure({model::ModelError::NotReady, path_, "create_mem_from_fd",
                                "ready session and shareable source memory", "invalid source",
                                "Cannot import shared device memory"});
    }
    auto *memory{api_.create_mem_from_fd(context_, source.fd, source.virt_addr, source.size, source.offset)};
    if (!memory || memory->virt_addr != source.virt_addr || memory->fd != source.fd || memory->size < source.size) {
        if (memory) {
            releaseMemory(memory);
        }
        return Result::failure({model::ModelError::ExecutionFailure, path_, "create_mem_from_fd",
                                "imported handle referencing the same storage as fd " + std::to_string(source.fd),
                                memory ? "different storage or size" : "null", "Shared import failed"});
    }
    memories_.push_back(memory);
    return {memory};
}

model::Status DirectSession::releaseMemory(rknn_tensor_mem *memory) {
    if (!memory) {
        return {true};
    }
    const auto position{std::find(memories_.begin(), memories_.end(), memory)};
    if (position != memories_.end()) {
        memories_.erase(position);
    }
    if (context_ == 0) {
        model::terminateDeviceFailure(path_.c_str(), "Shared memory released after its session context was destroyed");
    }
    if (api_.destroy_mem(context_, memory) != RKNN_SUCC) {
        model::terminateDeviceFailure(path_.c_str(), "Shared device memory release failed");
    }
    return {true};
}

model::Status DirectSession::syncFromDevice(rknn_tensor_mem *memory) {
    using Status = model::Status;
    if (!memory || context_ == 0) {
        return Status::failure({model::ModelError::NotReady, path_, "mem_sync", "registered shared memory",
                                "missing memory or destroyed context", "Cannot synchronise shared memory"});
    }
    const int status{api_.mem_sync(context_, memory, RKNN_MEMORY_SYNC_FROM_DEVICE)};
    if (status != RKNN_SUCC) {
        return Status::failure(failure("mem_sync", status));
    }
    return {true};
}

SessionAttributes DirectSession::attributes() const { return {path_, describe(), &inputs_, &outputs_}; }
std::string DirectSession::describe() const {
    std::ostringstream out;
    out.precision(10);
    out << "model=" << path_ << " runtime=" << version_.api_version << " driver=" << version_.drv_version << "\n";
    for (const auto &a : inputs_) {
        out << "input " << describeAttribute(a) << '\n';
    }
    for (const auto &a : outputs_) {
        out << "output " << describeAttribute(a) << '\n';
    }
    for (const auto &a : native_inputs_) {
        out << "native input " << describeAttribute(a) << '\n';
    }
    for (const auto &a : native_outputs_) {
        out << "native output " << describeAttribute(a) << '\n';
    }
    return out.str();
}
} // namespace search_direct
