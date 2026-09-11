#ifndef RK3576_RKNN_YOLO_CONTRACT_H
#define RK3576_RKNN_YOLO_CONTRACT_H
#include "rknn_session.h"
namespace rk3576 {
/** @brief 在通用存储检查之上保留 YOLO 的输入和六输出限制。 */
model::Result<int> validateYoloSession(const RknnSession &session);
} // namespace rk3576
#endif // RK3576_RKNN_YOLO_CONTRACT_H
