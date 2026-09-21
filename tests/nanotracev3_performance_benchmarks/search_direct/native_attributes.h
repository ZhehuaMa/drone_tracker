#ifndef NANOTRACK_SEARCH_DIRECT_NATIVE_ATTRIBUTES_H
#define NANOTRACK_SEARCH_DIRECT_NATIVE_ATTRIBUTES_H

#include <string>
#include <vector>

#include "platform/rk3576/rknn_nanotrack_contract.h"

namespace search_direct {
/** @brief 一个模型会话对外可见的属性视图；指针借用会话内部数组，不转移所有权。 */
struct SessionAttributes {
    std::string identity{};
    std::string description{};
    const std::vector<rknn_tensor_attr> *inputs{};
    const std::vector<rknn_tensor_attr> *outputs{};
};

/**
 * @brief 搜索特征共享缓冲区的准入结论。
 *
 * producer 是搜索模型写出特征时使用的原生输出属性；consumer 是 head 读取该特征时
 * 使用的原生输入属性，且已把 `pass_through` 置为 1，表示按原样读取、不做格式转换。
 * bytes 是双方一致的带跨度字节容量，用于分配设备内存。
 */
struct SharedFeatureContract {
    rknn_tensor_attr producer{};
    rknn_tensor_attr consumer{};
    std::uint32_t bytes{};
    std::string description{};
};

/** @brief 把一个张量属性渲染成可比对的诊断文本，包含原始及按 SDK 约定解释后的跨度。 */
std::string describeAttribute(const rknn_tensor_attr &attribute);

/**
 * @brief 按唯一逻辑形状识别三个会话的角色，语义与生产契约一致。
 *
 * 逻辑形状指 SDK 以 NCHW/NHWC 报告的普通形状。识别不依赖张量名称或导出顺序，
 * 因此 head 的模板输入和搜索输入即使索引互换也能被正确识别。
 */
model::Result<rk3576::NanoTrackRoles> resolveNanoTrackRoles(const SessionAttributes &template_session,
                                                            const SessionAttributes &search_session,
                                                            const SessionAttributes &head_session);

/**
 * @brief 判断搜索输出与 head 搜索输入能否共享同一块设备内存。
 *
 * 判断只依据 SDK 实际返回的属性，不使用文件名或模型指纹白名单。
 * 原生排列 NC1HWC2 把逻辑通道拆成若干组，每组固定通道数；例如逻辑 96 通道可以
 * 表示为 12 组、每组 8 通道，形状写作 `[1,12,16,16,8]`。
 *
 * @param producer_native 搜索模型输出的原生属性。
 * @param producer_logical 搜索模型输出的普通属性，用于核对逻辑通道数与空间尺寸。
 * @param consumer_native head 搜索输入的原生属性。
 * @param consumer_logical head 搜索输入的普通属性。
 * @param identity 诊断中使用的模型身份文本。
 * @return 兼容时给出共享契约；不兼容时给出包含预期与实际属性的诊断，不做任何转换。
 */
model::Result<SharedFeatureContract> matchSharedFeature(const rknn_tensor_attr &producer_native,
                                                        const rknn_tensor_attr &producer_logical,
                                                        const rknn_tensor_attr &consumer_native,
                                                        const rknn_tensor_attr &consumer_logical,
                                                        const std::string &identity);
} // namespace search_direct
#endif // NANOTRACK_SEARCH_DIRECT_NATIVE_ATTRIBUTES_H
