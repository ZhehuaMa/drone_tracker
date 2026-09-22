#include "rknn_nanotrack_factory.h"

#include "nanotrack_preprocessor.h"
#include "rknn_nanotrack_backend.h"

namespace rk3576 {
model::Result<NanoTrackComponents> loadNanoTrack(const std::string &template_path, const std::string &search_path,
                                                 const std::string &head_path) {
    return loadNanoTrack(template_path, search_path, head_path, nativeRknnApi(), nativeRknnSharedMemoryApi());
}
model::Result<NanoTrackComponents> loadNanoTrack(const std::string &template_path, const std::string &search_path,
                                                 const std::string &head_path, RknnApi api) {
    using Result = model::Result<NanoTrackComponents>;
    try {
        auto backend{RknnNanoTrackBackend::load(template_path, search_path, head_path, api, {})};
        if (!backend) {
            return Result::failure(backend.error);
        }
        auto description{backend.value->describe()};
        const bool floating_interfaces{backend.value->floatingInterfaces()};
        return {{std::move(backend.value), std::make_unique<NanoTrackPreprocessor>(), std::move(description), floating_interfaces}};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, head_path, "assemble NanoTrack", {}, {}, error.what()});
    }
}
model::Result<NanoTrackComponents> loadNanoTrack(const std::string &template_path, const std::string &search_path,
                                                 const std::string &head_path, RknnApi api,
                                                 RknnSharedMemoryApi shared_api) {
    using Result = model::Result<NanoTrackComponents>;
    try {
        auto backend{RknnNanoTrackBackend::load(template_path, search_path, head_path, api, shared_api)};
        if (!backend) {
            return Result::failure(backend.error);
        }
        auto description{backend.value->describe()};
        const bool floating_interfaces{backend.value->floatingInterfaces()};
        return {{std::move(backend.value), std::make_unique<NanoTrackPreprocessor>(), std::move(description),
                 floating_interfaces}};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, head_path, "assemble NanoTrack", {}, {}, error.what()});
    }
}
model::Result<NanoTrackComponents> loadNanoTrackCpuReference(const std::string &template_path,
                                                             const std::string &search_path,
                                                             const std::string &head_path, RknnApi api) {
    using Result = model::Result<NanoTrackComponents>;
    try {
        auto backend{RknnNanoTrackBackend::loadCpuReference(template_path, search_path, head_path, api)};
        if (!backend) return Result::failure(backend.error);
        auto description{backend.value->describe()};
        const bool floating_interfaces{backend.value->floatingInterfaces()};
        return {{std::move(backend.value), std::make_unique<NanoTrackPreprocessor>(), std::move(description),
                 floating_interfaces}};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, head_path, "assemble NanoTrack", {}, {}, error.what()});
    }
}
} // namespace rk3576
