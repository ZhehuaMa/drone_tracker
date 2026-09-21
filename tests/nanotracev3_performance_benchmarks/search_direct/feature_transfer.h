#ifndef NANOTRACK_SEARCH_DIRECT_FEATURE_TRANSFER_H
#define NANOTRACK_SEARCH_DIRECT_FEATURE_TRANSFER_H

#include "model/model_backend.h"
#include "platform/rk3576/rknn_session.h"

namespace search_direct {
/**
 * @brief 与生产后端相同的输出规范化：把 NHWC 排列的 Float32 输出整理成 NCHW。
 *
 * 已是 NCHW 时原样返回，不复制。
 */
model::Tensor toNchw(const model::Tensor &source);

/**
 * @brief 与生产后端相同的普通提交准备：按模型报告的逻辑排列重排 Float32 数据。
 *
 * 模型报告 NCHW 时直接借用原存储；报告 NHWC 时生成一份重排后的独立存储。
 */
rk3576::SessionInput submission(const model::Tensor &source, const rknn_tensor_attr &attribute);

/**
 * @brief 按公共前处理的契约把准备结果复制成一份独立的 Float32 NCHW 张量。
 *
 * @param size 期望的方形边长，模板为 127，搜索为 255。
 * @return 契约不符时返回失败，不产生部分数据。
 */
model::Result<model::Tensor> imageTensor(const PreparedInputRef &input, const std::string &identity, int size);

/** @brief 把一个 16 位浮点值转换为 32 位浮点，用于诊断读取共享特征。 */
float halfToFloat(std::uint16_t bits);
} // namespace search_direct
#endif // NANOTRACK_SEARCH_DIRECT_FEATURE_TRANSFER_H
