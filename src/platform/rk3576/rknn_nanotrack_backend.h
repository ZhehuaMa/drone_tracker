#ifndef RK3576_RKNN_NANOTRACK_BACKEND_H
#define RK3576_RKNN_NANOTRACK_BACKEND_H

#include "model/model_backend.h"
#include "rknn_nanotrack_contract.h"

namespace rk3576 {
/** @brief 三会话串联；模板特征自有存储，SDK 失效后必须重建。 */
class RknnNanoTrackBackend final : public model::NanoTrackBackend {
public:
    static model::Result<std::unique_ptr<RknnNanoTrackBackend>>
    load(const std::string &template_path, const std::string &search_path, const std::string &head_path, RknnApi api,
         RknnSharedMemoryApi shared_api);
    static model::Result<std::unique_ptr<RknnNanoTrackBackend>>
    loadCpuReference(const std::string &template_path, const std::string &search_path, const std::string &head_path,
                     RknnApi api);
    ~RknnNanoTrackBackend() noexcept override;
    RknnNanoTrackBackend(std::unique_ptr<RknnSession> z, std::unique_ptr<RknnSession> x,
                         std::unique_ptr<RknnSession> head, NanoTrackRoles roles,
                         SharedFeatureContract contract = {});
    model::Status setTemplate(const PreparedInputRef &input) override;
    model::Result<model::NanoPrediction> search(const PreparedInputRef &input) override;
    void reset() noexcept override;
    std::string describe() const;
    bool floatingInterfaces() const noexcept;

private:
    bool ready() const noexcept;
    void releaseShared() noexcept;
    model::Result<model::Tensor> backbone(RknnSession &session, const PreparedInputRef &input, int size);
    std::unique_ptr<RknnSession> template_session_;
    std::unique_ptr<RknnSession> search_session_;
    std::unique_ptr<RknnSession> head_session_;
    NanoTrackRoles roles_{};
    SharedFeatureContract contract_{};
    bool direct_{true};
    rknn_tensor_mem *shared_{};
    rknn_tensor_mem *imported_{};
    model::Tensor template_feature_{};
};
} // namespace rk3576
#endif // RK3576_RKNN_NANOTRACK_BACKEND_H
