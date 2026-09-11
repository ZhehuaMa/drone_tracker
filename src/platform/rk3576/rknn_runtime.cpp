#include "rknn_runtime.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

#include "model/validation.h"
#include "model/yolo_output_adapter.h"

namespace rk3576 {
namespace {
class OutputStorage final : public model::TensorStorage {
public:
    explicit OutputStorage(std::size_t count) : values(count) {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};
std::string attributeText(const rknn_tensor_attr &a) {
    std::ostringstream out;
    out << "#" << a.index << " " << std::string(a.name, strnlen(a.name, RKNN_MAX_NAME_LEN)) << " [";
    for (std::size_t i{0}; i < a.n_dims && i < RKNN_MAX_DIMS; ++i) {
        out << a.dims[i] << ',';
    }
    out << "] type=" << a.type << " format=" << a.fmt << " quant=" << a.qnt_type << " zp=" << a.zp
        << " scale=" << a.scale << " elems=" << a.n_elems << " bytes=" << a.size;
    return out.str();
}
model::Diagnostic attributeMismatch(const std::string &path, const rknn_tensor_attr &a, bool input,
                                    const std::string &expected) {
    return {model::ModelError::ContractMismatch, path,
            std::string(input ? "input " : "output ") + std::to_string(a.index) + " " +
                std::string(a.name, strnlen(a.name, RKNN_MAX_NAME_LEN)),
            expected, attributeText(a), "RKNN contract mismatch"};
}
} // namespace
model::Diagnostic RknnRuntime::failure(const std::string &operation, int status) const {
    return {
        model::ModelError::ExecutionFailure, path_, operation, "RKNN_SUCC", std::to_string(status), "RKNN call failed"};
}
model::Result<std::unique_ptr<RknnRuntime>> RknnRuntime::load(const std::string &path, RknnApi api) {
    using Result = model::Result<std::unique_ptr<RknnRuntime>>;
    if (!api.init || !api.query || !api.inputs_set || !api.run || !api.wait || !api.outputs_get ||
        !api.outputs_release || !api.destroy) {
        return Result::failure(
            {model::ModelError::LoadFailure, path, "API", "complete SDK boundary", {}, "Missing API"});
    }
    auto runtime{std::make_unique<RknnRuntime>(path, api)};
    try {
        if (const auto error{runtime->initialize()}) {
            return Result::failure(error);
        }
        return {std::move(runtime)};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, path, {}, {}, {}, error.what()});
    }
}
model::Diagnostic RknnRuntime::checkAttribute(const rknn_tensor_attr &a, bool input) {
    if (a.n_dims != 4) {
        return attributeMismatch(path_, a, input, "rank 4");
    }
    TensorShape shape;
    for (std::size_t i{0}; i < 4; ++i) {
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
    if (a.type == RKNN_TENSOR_INT8 && (a.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC || !std::isfinite(a.scale) ||
                                       a.scale <= 0 || a.zp < -128 || a.zp > 127)) {
        return attributeMismatch(path_, a, input, "INT8 affine quantization, positive finite scale, int8 zero point");
    }
    if (input) {
        if (shape != TensorShape{1, 640, 640, 3} || a.type != RKNN_TENSOR_INT8 || a.fmt != RKNN_TENSOR_NHWC ||
            a.zp != -128 || a.scale != 1.0f / 255.0f) {
            return attributeMismatch(path_, a, input, "INT8 NHWC [1,640,640,3], zp=-128 scale=1/255; mean=0/std=255");
        }
    } else {
        if (a.fmt != RKNN_TENSOR_NCHW) {
            return attributeMismatch(path_, a, input, "NCHW logical output");
        }
        descriptors_.push_back({shape, TensorElementType::Float32, TensorLayout::Nchw, bytes.value, {}});
    }
    return {};
}
model::Diagnostic RknnRuntime::initialize() {
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
    if (count.n_input != 1 || count.n_output != 6) {
        return {model::ModelError::ContractMismatch,
                path_,
                "input/output count",
                "1/6",
                std::to_string(count.n_input) + "/" + std::to_string(count.n_output),
                "Unsupported YOLO structure"};
    }
    status = api_.query(context_, RKNN_QUERY_INPUT_ATTR, &input_, sizeof(input_));
    if (status != RKNN_SUCC) {
        return failure("query input#0", status);
    }
    if (const auto error{checkAttribute(input_, true)}) {
        return error;
    }
    for (std::size_t i{0}; i < outputs_.size(); ++i) {
        outputs_[i].index = static_cast<std::uint32_t>(i);
        status = api_.query(context_, RKNN_QUERY_OUTPUT_ATTR, &outputs_[i], sizeof(outputs_[i]));
        if (status != RKNN_SUCC) {
            return failure("query output#" + std::to_string(i), status);
        }
        if (const auto error{checkAttribute(outputs_[i], false)}) {
            return error;
        }
    }
    const auto classes{model::yoloSixOutputClasses(descriptors_, path_)};
    if (!classes) {
        auto error{classes.error};
        for (const auto &a : outputs_) {
            error.actual += "\n" + attributeText(a);
        }
        return error;
    }
    classes_ = classes.value;
    ready_ = true;
    return {};
}
void RknnRuntime::confirmStopped() {
    if (running_) {
        // 负返回值本身不证明停止。仅成功 wait 构成完成证据。
        if (api_.wait(context_, nullptr) != RKNN_SUCC) {
            model::terminateDeviceFailure(path_.c_str(), "RKNN wait failed; device access cannot be excluded");
        }
        running_ = false;
    }
}
void RknnRuntime::invalidate() noexcept {
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
RknnRuntime::~RknnRuntime() noexcept { invalidate(); }
model::ExecutionResult RknnRuntime::execute(const std::vector<std::uint8_t> &input) {
    using Result = model::ExecutionResult;
    if (!ready_) {
        return Result::failure({model::ModelError::NotReady, path_, {}, {}, {}, "Runtime is invalid"});
    }
    if (input.size() != 640 * 640 * 3) {
        return Result::failure({model::ModelError::ContractMismatch, path_, "input#0", "1228800 bytes",
                                std::to_string(input.size()), "Invalid input storage"});
    }
    rknn_input submission{};
    submission.buf = const_cast<std::uint8_t *>(input.data());
    submission.size = static_cast<std::uint32_t>(input.size());
    submission.type = RKNN_TENSOR_UINT8;
    submission.fmt = RKNN_TENSOR_NHWC;
    int status{api_.inputs_set(context_, 1, &submission)};
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
    std::array<rknn_output, 6> outputs{};
    for (std::size_t i{0}; i < outputs.size(); ++i) {
        outputs[i].index = static_cast<std::uint32_t>(i);
        outputs[i].want_float = 1;
    }
    status = api_.outputs_get(context_, 6, outputs.data(), nullptr);
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
    status = api_.outputs_release(context_, 6, outputs.data());
    if (status != RKNN_SUCC) {
        result = Result::failure(failure("outputs_release", status));
    }
    if (!result) {
        invalidate();
    }
    return result;
}
std::string RknnRuntime::describe() const {
    std::ostringstream out;
    out.precision(10);
    out << "model=" << path_ << " runtime=" << version_.api_version << " driver=" << version_.drv_version
        << " C=" << classes_ << "\n"
        << attributeText(input_) << '\n';
    for (const auto &a : outputs_) {
        out << attributeText(a) << '\n';
    }
    return out.str();
}
} // namespace rk3576
