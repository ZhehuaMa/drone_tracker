#include "rknn_yolo_backend.h"

#include "rknn_runtime.h"

namespace rk3576 {
model::Result<std::unique_ptr<model::YoloBackend>> loadYoloBackend(const std::string &path) {
    std::string description{};
    return loadYoloBackend(path, nativeRknnApi(), description);
}
model::Result<std::unique_ptr<model::YoloBackend>> loadYoloBackend(
    const std::string &path, RknnApi api, std::string &description) {
    auto runtime{RknnRuntime::load(path, api)};
    if (!runtime) {
        return model::Result<std::unique_ptr<model::YoloBackend>>::failure(runtime.error);
    }
    description = runtime.value->describe();
    return {std::make_unique<RknnYoloBackend>(std::move(runtime.value))};
}
} // namespace rk3576
