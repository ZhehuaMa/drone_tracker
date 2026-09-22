#include "production_factory.h"

#include <iostream>
#include "rknn_yolo_backend.h"

namespace rk3576 {
AssemblyDependencies productionDependencies(RknnApi api, std::function<void(const std::string &)> describe,
                                             RknnSharedMemoryApi shared_api) {
    AssemblyDependencies d{};
    d.yolo = [api](const std::string &path) {
        using Result = PlatformResult<YoloComponents>;
        std::string description{};
        auto result{loadYoloBackend(path, api, description)};
        if (!result) {
            return Result::failure({"rk3576", "yolo", path, model::describe(result.error)});
        }
        return Result{{std::move(result.value), std::move(description)}};
    };
    d.nano = [api, shared_api](const ModelPaths &paths) {
        using Result = PlatformResult<NanoTrackComponents>;
        auto result{loadNanoTrack(paths.template_backbone, paths.search_backbone, paths.head, api, shared_api)};
        if (!result) {
            const auto &error{result.error};
            std::string role{"head"};
            if (error.tensor.rfind("template_backbone", 0) == 0) {
                role = "template_backbone";
            }
            if (error.tensor.rfind("search_backbone", 0) == 0) {
                role = "search_backbone";
            }
            return Result::failure({"rk3576", role, error.model, model::describe(error)});
        }
        return Result{std::move(result.value)};
    };
    d.describe = std::move(describe);
    return d;
}
} // namespace rk3576
std::unique_ptr<PlatformFactory> CreatePlatformFactory() {
    rk3576::FactoryOptions options{};
    options.enable_camera = true;
    return rk3576::createFactory(rk3576::productionDependencies(
        rk3576::nativeRknnApi(), [](const std::string &description) { std::cout << description << std::flush; },
        rk3576::nativeRknnSharedMemoryApi()),
                                 options);
}
