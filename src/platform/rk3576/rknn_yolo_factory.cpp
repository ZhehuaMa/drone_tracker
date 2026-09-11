#include "rknn_yolo_backend.h"

#include "rknn_runtime.h"

namespace rk3576 {
model::Result<std::unique_ptr<model::YoloBackend>> loadYoloBackend(const std::string &path) {
    auto runtime{RknnRuntime::load(path, nativeRknnApi())};
    if (!runtime) {
        return model::Result<std::unique_ptr<model::YoloBackend>>::failure(runtime.error);
    }
    return {std::make_unique<RknnYoloBackend>(std::move(runtime.value))};
}
} // namespace rk3576
