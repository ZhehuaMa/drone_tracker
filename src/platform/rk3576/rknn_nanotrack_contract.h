#ifndef RK3576_RKNN_NANOTRACK_CONTRACT_H
#define RK3576_RKNN_NANOTRACK_CONTRACT_H

#include "rknn_session.h"

namespace rk3576 {
/** @brief 通过唯一逻辑形状识别的 Head 索引，不依赖导出名称。 */
struct NanoTrackRoles {
    std::uint32_t template_input{};
    std::uint32_t search_input{};
    std::uint32_t classification_output{};
    std::uint32_t location_output{};
};
TensorShape nchwShape(const rknn_tensor_attr &attribute);
model::Result<NanoTrackRoles> validateNanoTrackSessions(const RknnSession &template_session,
                                                        const RknnSession &search_session,
                                                        const RknnSession &head_session);
} // namespace rk3576
#endif // RK3576_RKNN_NANOTRACK_CONTRACT_H
