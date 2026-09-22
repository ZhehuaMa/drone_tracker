#include "rknn_nanotrack_backend.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "model/validation.h"
#include "nanotrack_preprocessor.h"
#include "preprocess/input_validation.h"
#include "timing_observer.h"

namespace rk3576 {
namespace {
class FloatStorage final : public model::TensorStorage {
public:
    explicit FloatStorage(std::size_t size) : values(size) {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};
// 所有 SDK 输出先规范化，模型之间只传递实数，不复用原始量化字节。
model::Tensor toNchw(const model::Tensor &source) {
    if (source.descriptor.layout == TensorLayout::Nchw) {
        return source;
    }
    const auto &s{source.descriptor.shape};
    const int height{s[1]};
    const int width{s[2]};
    const int channels{s[3]};
    auto storage{std::make_shared<FloatStorage>(source.descriptor.bytes / sizeof(float))};
    const auto view{source.read()};
    for (int n{0}; n < s[0]; ++n) {
        for (int c{0}; c < channels; ++c) {
            for (int y{0}; y < height; ++y) {
                for (int x{0}; x < width; ++x) {
                    storage->values[((n * channels + c) * height + y) * width + x] =
                        view.data()[((n * height + y) * width + x) * channels + c];
                }
            }
        }
    }
    return {
        {{s[0], channels, height, width}, TensorElementType::Float32, TensorLayout::Nchw, source.descriptor.bytes, {}},
        std::move(storage)};
}
SessionInput submission(const model::Tensor &source, const rknn_tensor_attr &attribute) {
    const auto &s{source.descriptor.shape};
    if (attribute.fmt == RKNN_TENSOR_NCHW) {
        return {attribute.index, source.descriptor, source.storage->hostData(), source.descriptor.bytes,
                source.storage};
    }
    auto storage{std::make_shared<FloatStorage>(source.descriptor.bytes / sizeof(float))};
    const int channels{s[1]};
    const int height{s[2]};
    const int width{s[3]};
    const auto view{source.read()};
    for (int n{0}; n < s[0]; ++n) {
        for (int y{0}; y < height; ++y) {
            for (int x{0}; x < width; ++x) {
                for (int c{0}; c < channels; ++c) {
                    storage->values[((n * height + y) * width + x) * channels + c] =
                        view.data()[((n * channels + c) * height + y) * width + x];
                }
            }
        }
    }
    return {
        attribute.index,
        {{s[0], height, width, channels}, TensorElementType::Float32, TensorLayout::Nhwc, source.descriptor.bytes, {}},
        storage->hostData(),
        source.descriptor.bytes,
        storage};
}
model::Diagnostic notReady(const std::string &path) {
    return {model::ModelError::NotReady,
            path,
            "NanoTrack",
            "ready sessions and valid template",
            {},
            "Template missing or session invalid; reset template or rebuild backend"};
}
} // namespace
RknnNanoTrackBackend::RknnNanoTrackBackend(std::unique_ptr<RknnSession> z, std::unique_ptr<RknnSession> x,
                                           std::unique_ptr<RknnSession> head, NanoTrackRoles roles,
                                           SharedFeatureContract contract)
    : template_session_{std::move(z)}, search_session_{std::move(x)}, head_session_{std::move(head)}, roles_{roles},
      contract_{contract} {
    if (!template_session_ || !search_session_ || !head_session_) {
        throw std::invalid_argument("Three RKNN sessions are required");
    }
}
RknnNanoTrackBackend::~RknnNanoTrackBackend() noexcept { releaseShared(); }
model::Result<std::unique_ptr<RknnNanoTrackBackend>> RknnNanoTrackBackend::loadCpuReference(
    const std::string &template_path, const std::string &search_path, const std::string &head_path, RknnApi api) {
    using Result = model::Result<std::unique_ptr<RknnNanoTrackBackend>>;
    auto z{RknnSession::load(template_path, api)};
    if (!z) return Result::failure(z.error);
    auto x{RknnSession::load(search_path, api)};
    if (!x) return Result::failure(x.error);
    auto head{RknnSession::load(head_path, api)};
    if (!head) return Result::failure(head.error);
    const auto roles{validateNanoTrackSessions(*z.value, *x.value, *head.value)};
    if (!roles) return Result::failure(roles.error);
    auto backend{std::make_unique<RknnNanoTrackBackend>(std::move(z.value), std::move(x.value),
                                                        std::move(head.value), roles.value)};
    backend->direct_ = false;
    return {std::move(backend)};
}
model::Result<std::unique_ptr<RknnNanoTrackBackend>> RknnNanoTrackBackend::load(const std::string &template_path,
                                                                                const std::string &search_path,
                                                                                const std::string &head_path,
                                                                                RknnApi api,
                                                                                RknnSharedMemoryApi shared_api) {
    using Result = model::Result<std::unique_ptr<RknnNanoTrackBackend>>;
    auto z{RknnSession::load(template_path, api, shared_api)};
    if (!z) {
        z.error.tensor = "template_backbone: " + z.error.tensor;
        return Result::failure(z.error);
    }
    auto x{RknnSession::load(search_path, api, shared_api)};
    if (!x) {
        x.error.tensor = "search_backbone: " + x.error.tensor;
        return Result::failure(x.error);
    }
    auto head{RknnSession::load(head_path, api, shared_api)};
    if (!head) {
        head.error.tensor = "head: " + head.error.tensor;
        return Result::failure(head.error);
    }
    const auto roles{validateNanoTrackSessions(*z.value, *x.value, *head.value)};
    if (!roles) {
        return Result::failure(roles.error);
    }
    if (!shared_api.complete()) {
        return Result::failure({model::ModelError::LoadFailure, search_path, "shared memory API",
                                "complete RKNN shared-memory extension", "missing", "Direct handoff unavailable"});
    }
    auto contract{matchSharedFeature(x.value->nativeOutputs().at(0), x.value->outputs().at(0),
                                     head.value->nativeInputs().at(roles.value.search_input),
                                     head.value->inputs().at(roles.value.search_input),
                                     search_path + " -> " + head_path)};
    if (!contract) {
        return Result::failure(contract.error);
    }
    auto backend{std::make_unique<RknnNanoTrackBackend>(std::move(z.value), std::move(x.value), std::move(head.value),
                                                        roles.value, contract.value)};
    auto allocated{backend->search_session_->allocateShared(contract.value.bytes)};
    if (!allocated) {
        return Result::failure(allocated.error);
    }
    backend->shared_ = allocated.value;
    auto imported{backend->head_session_->importShared(*backend->shared_)};
    if (!imported) {
        backend->releaseShared();
        return Result::failure(imported.error);
    }
    backend->imported_ = imported.value;
    return {std::move(backend)};
}
bool RknnNanoTrackBackend::ready() const noexcept {
    return template_session_->ready() && search_session_->ready() && head_session_->ready() &&
           (!direct_ || (shared_ && imported_));
}
void RknnNanoTrackBackend::reset() noexcept { template_feature_ = {}; }
void RknnNanoTrackBackend::releaseShared() noexcept {
    if (imported_) {
        head_session_->releaseShared(imported_);
        imported_ = nullptr;
    }
    if (shared_) {
        search_session_->releaseShared(shared_);
        shared_ = nullptr;
    }
}
model::Result<model::Tensor> RknnNanoTrackBackend::backbone(RknnSession &session, const PreparedInputRef &input,
                                                            int size) {
    timing::Span model_span{size == 127 ? timing::Stage::TemplateBackbone : timing::Stage::SearchBackbone};
    timing::Span input_prepare{timing::Stage::InputPrepare};
    using Result = model::Result<model::Tensor>;
    const auto floating{std::dynamic_pointer_cast<const FloatInput>(input)};
    if (!floating) {
        return Result::failure({model::ModelError::PlatformTypeMismatch, session.identity(), "input#0", "FloatInput",
                                "foreign storage", "Incompatible prepared input"});
    }
    if (!matchesImageContract(floating->contract(), 1.0f, size) ||
        floating->values()->size() != static_cast<std::size_t>(3 * size * size)) {
        return Result::failure({model::ModelError::ContractMismatch, session.identity(), "input#0",
                                "Float32 NCHW [1,3," + std::to_string(size) + "," + std::to_string(size) + "]",
                                "elements=" + std::to_string(floating->values()->size()), "Invalid image input"});
    }
    auto storage{std::make_shared<FloatStorage>(floating->values()->size())};
    std::copy(floating->values()->begin(), floating->values()->end(), storage->values.begin());
    model::Tensor tensor{
        {floating->contract().shape, TensorElementType::Float32, TensorLayout::Nchw, storage->byteSize(), {}}, storage};
    auto input_submission{submission(tensor, session.inputs()[0])};
    input_prepare.finish();
    auto outputs{session.execute({input_submission})};
    if (!outputs) {
        model_span.finish(false);
        return Result::failure(outputs.error);
    }
    timing::Span normalize{timing::Stage::Normalize};
    return {toNchw(outputs.value[0])};
}
model::Status RknnNanoTrackBackend::setTemplate(const PreparedInputRef &input) {
    reset();
    if (!ready()) {
        return model::Status::failure(notReady(template_session_->identity()));
    }
    try {
        auto feature{backbone(*template_session_, input, 127)};
        if (!feature) {
            return model::Status::failure(feature.error);
        }
        template_feature_ = std::move(feature.value);
        return {true};
    } catch (const std::exception &error) {
        return model::Status::failure(
            {model::ModelError::ExecutionFailure, template_session_->identity(), "setTemplate", {}, {}, error.what()});
    }
}
model::Result<model::NanoPrediction> RknnNanoTrackBackend::search(const PreparedInputRef &input) {
    using Result = model::Result<model::NanoPrediction>;
    if (!ready() || !template_feature_.storage) {
        return Result::failure(notReady(head_session_->identity()));
    }
    try {
        if (!direct_) {
            auto feature{backbone(*search_session_, input, 255)};
            if (!feature) return Result::failure(feature.error);
            timing::Span head_span{timing::Stage::Head};
            timing::Span input_prepare{timing::Stage::InputPrepare};
            std::vector<SessionInput> inputs{
                submission(template_feature_, head_session_->inputs()[roles_.template_input]),
                submission(feature.value, head_session_->inputs()[roles_.search_input])};
            input_prepare.finish();
            auto outputs{head_session_->execute(inputs)};
            if (!outputs) {
                head_span.finish(false);
                return Result::failure(outputs.error);
            }
            timing::Span normalize{timing::Stage::Normalize};
            return model::nanoPrediction(
                {toNchw(outputs.value[roles_.classification_output]), toNchw(outputs.value[roles_.location_output])},
                head_session_->identity());
        }
        const auto floating{std::dynamic_pointer_cast<const FloatInput>(input)};
        if (!floating) {
            return Result::failure({model::ModelError::PlatformTypeMismatch, search_session_->identity(), "input#0",
                                    "FloatInput", "foreign storage", "Incompatible prepared input"});
        }
        if (!matchesImageContract(floating->contract(), 1.0f, 255) ||
            floating->values()->size() != static_cast<std::size_t>(3 * 255 * 255)) {
            return Result::failure({model::ModelError::ContractMismatch, search_session_->identity(), "input#0",
                                    "Float32 NCHW [1,3,255,255]", "invalid image input", "Invalid image input"});
        }
        auto storage{std::make_shared<FloatStorage>(floating->values()->size())};
        std::copy(floating->values()->begin(), floating->values()->end(), storage->values.begin());
        model::Tensor tensor{{floating->contract().shape, TensorElementType::Float32, TensorLayout::Nchw,
                              storage->byteSize(), {}},
                             storage};
        auto search_input{submission(tensor, search_session_->inputs()[0])};
        if (const auto submitted{search_session_->submit({search_input})}; !submitted) {
            return Result::failure(submitted.error);
        }
        if (const auto bound{search_session_->bind(contract_.producer, shared_)}; !bound) {
            return Result::failure(bound.error);
        }
        if (const auto executed{search_session_->run()}; !executed) {
            return Result::failure(executed.error);
        }
        search_session_->confirmCompleted();
        timing::Span head_span{timing::Stage::Head};
        timing::Span input_prepare{timing::Stage::InputPrepare};
        auto template_input{submission(template_feature_, head_session_->inputs()[roles_.template_input])};
        input_prepare.finish();
        if (const auto submitted{head_session_->submit({template_input})}; !submitted) {
            head_span.finish(false);
            return Result::failure(submitted.error);
        }
        if (const auto bound{head_session_->bind(contract_.consumer, imported_)}; !bound) {
            head_span.finish(false);
            return Result::failure(bound.error);
        }
        if (const auto executed{head_session_->run()}; !executed) {
            head_span.finish(false);
            return Result::failure(executed.error);
        }
        auto outputs{head_session_->fetch()};
        if (!outputs) {
            head_span.finish(false);
            return Result::failure(outputs.error);
        }
        timing::Span normalize{timing::Stage::Normalize};
        return model::nanoPrediction(
            {toNchw(outputs.value[roles_.classification_output]), toNchw(outputs.value[roles_.location_output])},
            head_session_->identity());
    } catch (const std::exception &error) {
        return Result::failure(
            {model::ModelError::ExecutionFailure, head_session_->identity(), "search", {}, {}, error.what()});
    }
}
bool RknnNanoTrackBackend::floatingInterfaces() const noexcept {
    for (const auto *session : {template_session_.get(), search_session_.get(), head_session_.get()}) {
        for (const auto *attributes : {&session->inputs(), &session->outputs()}) {
            for (const auto &attribute : *attributes) {
                if (attribute.type != RKNN_TENSOR_FLOAT16 && attribute.type != RKNN_TENSOR_FLOAT32) { return false; }
            }
        }
    }
    return true;
}
std::string RknnNanoTrackBackend::describe() const {
    std::ostringstream out;
    out << "template Backbone\n"
        << template_session_->describe() << "search Backbone\n"
        << search_session_->describe() << "Head\n"
        << head_session_->describe() << "roles: template_input=" << roles_.template_input
        << " search_input=" << roles_.search_input << " classification_output=" << roles_.classification_output
        << " location_output=" << roles_.location_output << '\n'
        << "submission: Float32, pass_through=0, native logical layout; outputs: want_float=1\n";
    return out.str();
}
} // namespace rk3576
