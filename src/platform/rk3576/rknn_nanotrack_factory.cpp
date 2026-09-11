#include "rknn_nanotrack_factory.h"

#include "nanotrack_preprocessor.h"
#include "rknn_nanotrack_backend.h"

namespace rk3576 {
model::Result<NanoTrackComponents> loadNanoTrack(const std::string &template_path, const std::string &search_path,
                                                 const std::string &head_path) {
    using Result = model::Result<NanoTrackComponents>;
    try {
        auto backend{RknnNanoTrackBackend::load(template_path, search_path, head_path, nativeRknnApi())};
        if (!backend) {
            return Result::failure(backend.error);
        }
        auto description{backend.value->describe()};
        return {{std::move(backend.value), std::make_unique<NanoTrackPreprocessor>(), std::move(description)}};
    } catch (const std::exception &error) {
        return Result::failure({model::ModelError::LoadFailure, head_path, "assemble NanoTrack", {}, {}, error.what()});
    }
}
} // namespace rk3576
