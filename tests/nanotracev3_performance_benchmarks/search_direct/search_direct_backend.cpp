#include "search_direct_backend.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "feature_transfer.h"
#include "model/validation.h"
#include "platform/rk3576/nanotrack_preprocessor.h"
#include "timing_observer.h"

namespace search_direct {
namespace {
using Stage = timing::Stage;

model::Diagnostic notReady(const std::string &path) {
    return {model::ModelError::NotReady,
            path,
            "NanoTrack",
            "ready sessions, shared search buffer and valid template",
            {},
            "Template missing, shared buffer released or session invalid; rebuild backend"};
}
} // namespace

SearchDirectBackend::SearchDirectBackend(std::unique_ptr<DirectSession> template_session,
                                         std::unique_ptr<DirectSession> search_session,
                                         std::unique_ptr<DirectSession> head_session, rk3576::NanoTrackRoles roles,
                                         SharedFeatureContract contract)
    : template_session_{std::move(template_session)}, search_session_{std::move(search_session)},
      head_session_{std::move(head_session)}, roles_{roles}, contract_{std::move(contract)} {
    if (!template_session_ || !search_session_ || !head_session_) {
        throw std::invalid_argument("Three RKNN sessions are required");
    }
}

SearchDirectBackend::~SearchDirectBackend() noexcept { releaseShared(); }

void SearchDirectBackend::releaseShared() noexcept {
    // 先释放消费者的导入句柄，再释放生产者的原始内存；两个会话都保持有效直到释放完成。
    if (imported_) {
        head_session_->releaseMemory(imported_);
        imported_ = nullptr;
    }
    if (shared_) {
        search_session_->releaseMemory(shared_);
        shared_ = nullptr;
    }
}

model::Result<std::unique_ptr<SearchDirectBackend>> SearchDirectBackend::load(const std::string &template_path,
                                                                              const std::string &search_path,
                                                                              const std::string &head_path,
                                                                              SharedMemoryApi api) {
    using Result = model::Result<std::unique_ptr<SearchDirectBackend>>;
    auto z{DirectSession::load(template_path, api)};
    if (!z) {
        z.error.tensor = "template_backbone: " + z.error.tensor;
        return Result::failure(z.error);
    }
    auto x{DirectSession::load(search_path, api)};
    if (!x) {
        x.error.tensor = "search_backbone: " + x.error.tensor;
        return Result::failure(x.error);
    }
    auto head{DirectSession::load(head_path, api)};
    if (!head) {
        head.error.tensor = "head: " + head.error.tensor;
        return Result::failure(head.error);
    }
    const auto roles{resolveNanoTrackRoles(z.value->attributes(), x.value->attributes(), head.value->attributes())};
    if (!roles) {
        return Result::failure(roles.error);
    }
    auto contract{matchSharedFeature(x.value->nativeOutputs()[0], x.value->outputs()[0],
                                     head.value->nativeInputs()[roles.value.search_input],
                                     head.value->inputs()[roles.value.search_input],
                                     x.value->identity() + " -> " + head.value->identity())};
    if (!contract) {
        return Result::failure(contract.error);
    }
    auto backend{std::make_unique<SearchDirectBackend>(std::move(z.value), std::move(x.value), std::move(head.value),
                                                       roles.value, contract.value)};
    auto allocated{backend->search_session_->allocate(backend->contract_.bytes)};
    if (!allocated) {
        return Result::failure(allocated.error);
    }
    backend->shared_ = allocated.value;
    auto imported{backend->head_session_->importFrom(*backend->shared_)};
    if (!imported) {
        backend->releaseShared();
        return Result::failure(imported.error);
    }
    backend->imported_ = imported.value;
    auto &identity{backend->identity_};
    identity["search_handoff"] = "native_fp16_shared";
    identity["bind_policy"] = "rebind_after_each_inputs_set";
    identity["search_completion_evidence"] = "blocking_rknn_run_without_async_flags";
    identity["shared_feature_bytes"] = std::to_string(backend->contract_.bytes);
    identity["shared_feature_fd"] = std::to_string(backend->shared_->fd) + "/" + std::to_string(imported.value->fd);
    identity["search_native_output"] = describeAttribute(backend->contract_.producer);
    identity["head_native_search_input"] = describeAttribute(backend->contract_.consumer);
    identity["head_role_indices"] = "template_input=" + std::to_string(roles.value.template_input) +
                                    " search_input=" + std::to_string(roles.value.search_input) +
                                    " classification_output=" + std::to_string(roles.value.classification_output) +
                                    " location_output=" + std::to_string(roles.value.location_output);
    return {std::move(backend)};
}

bool SearchDirectBackend::ready() const noexcept {
    return template_session_->ready() && search_session_->ready() && head_session_->ready() && shared_ && imported_;
}

void SearchDirectBackend::reset() noexcept { template_feature_ = {}; }

model::Status SearchDirectBackend::runTemplateBackbone(const PreparedInputRef &input, model::Tensor &feature) {
    timing::Span model_span{Stage::TemplateBackbone};
    timing::Span input_prepare{Stage::InputPrepare};
    auto tensor{imageTensor(input, template_session_->identity(), 127)};
    if (!tensor) {
        input_prepare.finish(false);
        model_span.finish(false);
        return model::Status::failure(tensor.error);
    }
    auto request{submission(tensor.value, template_session_->inputs()[0])};
    input_prepare.finish();
    if (const auto submitted{template_session_->submit({request})}; !submitted) {
        model_span.finish(false);
        return submitted;
    }
    if (const auto executed{template_session_->run()}; !executed) {
        model_span.finish(false);
        return executed;
    }
    auto outputs{template_session_->fetch()};
    if (!outputs) {
        model_span.finish(false);
        return model::Status::failure(outputs.error);
    }
    timing::Span normalize{Stage::Normalize};
    feature = toNchw(outputs.value[0]);
    return {true};
}

model::Status SearchDirectBackend::setTemplate(const PreparedInputRef &input) {
    // 建立新模板开始时旧模板立即失效；重建失败不会留下可继续使用的旧模板。
    reset();
    if (!ready()) {
        return model::Status::failure(notReady(template_session_->identity()));
    }
    try {
        model::Tensor feature;
        if (const auto status{runTemplateBackbone(input, feature)}; !status) {
            return status;
        }
        template_feature_ = std::move(feature);
        return {true};
    } catch (const std::exception &error) {
        return model::Status::failure(
            {model::ModelError::ExecutionFailure, template_session_->identity(), "setTemplate", {}, {}, error.what()});
    }
}

model::Result<model::NanoPrediction> SearchDirectBackend::search(const PreparedInputRef &input) {
    using Result = model::Result<model::NanoPrediction>;
    if (!ready() || !template_feature_.storage) {
        return Result::failure(notReady(head_session_->identity()));
    }
    try {
        {
            timing::Span model_span{Stage::SearchBackbone};
            timing::Span input_prepare{Stage::InputPrepare};
            auto tensor{imageTensor(input, search_session_->identity(), 255)};
            if (!tensor) {
                input_prepare.finish(false);
                model_span.finish(false);
                return Result::failure(tensor.error);
            }
            auto request{submission(tensor.value, search_session_->inputs()[0])};
            input_prepare.finish();
            // 先普通提交搜索图像，再重新绑定输出：旧绑定不会跨普通提交保持有效。
            if (const auto submitted{search_session_->submit({request})}; !submitted) {
                model_span.finish(false);
                return Result::failure(submitted.error);
            }
            if (const auto bound{search_session_->bind(contract_.producer, shared_)}; !bound) {
                model_span.finish(false);
                return Result::failure(bound.error);
            }
            if (const auto executed{search_session_->run()}; !executed) {
                model_span.finish(false);
                return Result::failure(executed.error);
            }
            // 阻塞运行成功即代表本帧特征已经写入共享内存，此处不读取、复制或转换该特征。
            search_session_->confirmCompleted();
        }
        timing::Span head_span{Stage::Head};
        timing::Span input_prepare{Stage::InputPrepare};
        auto request{submission(template_feature_, head_session_->inputs()[roles_.template_input])};
        input_prepare.finish();
        if (const auto submitted{head_session_->submit({request})}; !submitted) {
            head_span.finish(false);
            return Result::failure(submitted.error);
        }
        // 模板普通提交之后才重新绑定搜索输入，这是实机验证通过的唯一顺序。
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
        timing::Span normalize{Stage::Normalize};
        return model::nanoPrediction(
            {toNchw(outputs.value[roles_.classification_output]), toNchw(outputs.value[roles_.location_output])},
            head_session_->identity());
    } catch (const std::exception &error) {
        return Result::failure(
            {model::ModelError::ExecutionFailure, head_session_->identity(), "search", {}, {}, error.what()});
    }
}

model::Result<std::vector<float>> SearchDirectBackend::readSharedFeatureForDiagnostics() {
    using Result = model::Result<std::vector<float>>;
    if (!shared_ || !shared_->virt_addr) {
        return Result::failure(notReady(search_session_->identity()));
    }
    if (const auto synced{search_session_->syncFromDevice(shared_)}; !synced) {
        return Result::failure(synced.error);
    }
    const auto &a{contract_.producer};
    const std::size_t count{a.n_elems};
    if (contract_.bytes != count * sizeof(std::uint16_t)) {
        // 带跨度容量大于紧密排列时，诊断读取需要按行跨度展开；当前未支持该情形。
        return Result::failure({model::ModelError::ContractMismatch, search_session_->identity(),
                                "shared search feature", "紧密排列的原生特征",
                                describeAttribute(a), "诊断读取不支持带填充的共享特征"});
    }
    std::vector<std::uint16_t> raw(count);
    std::memcpy(raw.data(), static_cast<const std::uint8_t *>(shared_->virt_addr) + shared_->offset,
                count * sizeof(std::uint16_t));
    // 原生 NC1HWC2 把逻辑通道拆成若干组、每组 dims[4] 个；这里还原为逻辑 NCHW 顺序。
    const std::uint32_t height{a.dims[2]};
    const std::uint32_t width{a.dims[3]};
    const std::uint32_t group{a.dims[4]};
    const auto logical{rk3576::nchwShape(search_session_->outputs()[0])};
    const auto channels{static_cast<std::uint32_t>(logical[1])};
    std::vector<float> values(static_cast<std::size_t>(channels) * height * width);
    for (std::uint32_t c{0}; c < channels; ++c) {
        for (std::uint32_t y{0}; y < height; ++y) {
            for (std::uint32_t x{0}; x < width; ++x) {
                const std::size_t offset{(((static_cast<std::size_t>(c / group) * height + y) * width + x) * group) +
                                         (c % group)};
                values[(static_cast<std::size_t>(c) * height + y) * width + x] = halfToFloat(raw[offset]);
            }
        }
    }
    return {std::move(values)};
}

bool SearchDirectBackend::floatingInterfaces() const noexcept {
    for (const auto *session : {template_session_.get(), search_session_.get(), head_session_.get()}) {
        for (const auto *attributes : {&session->inputs(), &session->outputs()}) {
            for (const auto &attribute : *attributes) {
                if (attribute.type != RKNN_TENSOR_FLOAT16 && attribute.type != RKNN_TENSOR_FLOAT32) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::string SearchDirectBackend::describe() const {
    std::ostringstream out;
    out << "template Backbone\n"
        << template_session_->describe() << "search Backbone\n"
        << search_session_->describe() << "Head\n"
        << head_session_->describe() << "roles: template_input=" << roles_.template_input
        << " search_input=" << roles_.search_input << " classification_output=" << roles_.classification_output
        << " location_output=" << roles_.location_output << '\n'
        << "benchmark_variant: search_direct\n"
        << contract_.description
        << "submission: template Float32 pass_through=0; search feature shared natively, head outputs want_float=1\n";
    return out.str();
}

model::Result<rk3576::NanoTrackComponents> loadSearchDirect(const std::string &template_path,
                                                            const std::string &search_path,
                                                            const std::string &head_path, SharedMemoryApi api,
                                                            std::map<std::string, std::string> *identity) {
    using Result = model::Result<rk3576::NanoTrackComponents>;
    try {
        auto backend{SearchDirectBackend::load(template_path, search_path, head_path, api)};
        if (!backend) {
            return Result::failure(backend.error);
        }
        auto description{backend.value->describe()};
        const bool floating{backend.value->floatingInterfaces()};
        if (identity) {
            *identity = backend.value->identity();
        }
        return {{std::move(backend.value), std::make_unique<rk3576::NanoTrackPreprocessor>(), std::move(description),
                 floating}};
    } catch (const std::exception &error) {
        return Result::failure(
            {model::ModelError::LoadFailure, head_path, "assemble search-direct NanoTrack", {}, {}, error.what()});
    }
}

model::Result<rk3576::NanoTrackComponents> loadSearchDirect(const std::string &template_path,
                                                            const std::string &search_path,
                                                            const std::string &head_path,
                                                            std::map<std::string, std::string> *identity) {
    return loadSearchDirect(template_path, search_path, head_path, nativeSharedMemoryApi(), identity);
}
} // namespace search_direct
