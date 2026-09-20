#ifndef RK3576_RKNN_NANOTRACK_FACTORY_H
#define RK3576_RKNN_NANOTRACK_FACTORY_H
#include "model/model_backend.h"
#include "preprocess/image_preprocessor.h"
namespace rk3576 {
struct RknnApi;
/** @brief 完整独立组件；全部模型校验成功后才交出所有权。 */
struct NanoTrackComponents {
    std::unique_ptr<model::NanoTrackBackend> backend{};
    std::unique_ptr<::NanoTrackPreprocessor> preprocessor{};
    std::string description{};
    bool floating_interfaces{};
};
model::Result<NanoTrackComponents> loadNanoTrack(const std::string &template_path, const std::string &search_path,
                                                 const std::string &head_path);
model::Result<NanoTrackComponents> loadNanoTrack(const std::string &template_path, const std::string &search_path,
                                                 const std::string &head_path, RknnApi api);
} // namespace rk3576
#endif // RK3576_RKNN_NANOTRACK_FACTORY_H
