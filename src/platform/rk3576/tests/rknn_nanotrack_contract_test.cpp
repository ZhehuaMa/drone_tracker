#include "../rknn_nanotrack_contract.h"

#include <iostream>

#include "rknn_test_support.h"

namespace {
using namespace rknn_test;
rknn_tensor_attr logical() { return attribute(0, {1, 96, 16, 16}); }
rknn_tensor_attr native(std::uint32_t group = 8, rknn_tensor_type type = RKNN_TENSOR_FLOAT16) {
    auto result{attribute(0, {1, 12, 16, 16, group}, type, RKNN_TENSOR_NC1HWC2)};
    result.size_with_stride = result.n_elems * (type == RKNN_TENSOR_FLOAT16 ? 2U : 1U);
    return result;
}
} // namespace

int main() {
    const auto producer{native()};
    const auto consumer{native()};
    auto matched{rk3576::matchSharedFeature(producer, logical(), consumer, logical(), "contract")};
    require(matched && matched.value.bytes == producer.size_with_stride, "有效原生特征契约被拒绝");

    auto wrong_group{native(4)};
    require(!rk3576::matchSharedFeature(producer, logical(), wrong_group, logical(), "group"),
            "原生通道分组不匹配未拒绝");
    auto wrong_type{native(8, RKNN_TENSOR_FLOAT32)};
    require(!rk3576::matchSharedFeature(producer, logical(), wrong_type, logical(), "type"),
            "原生类型不匹配未拒绝");
    auto wrong_stride{consumer};
    wrong_stride.w_stride = 17;
    wrong_stride.size_with_stride = static_cast<std::uint32_t>(wrong_stride.n_elems * 2U + 512U);
    require(!rk3576::matchSharedFeature(producer, logical(), wrong_stride, logical(), "stride"),
            "原生跨度不匹配未拒绝");
    auto too_small{consumer};
    too_small.size_with_stride = 1;
    require(!rk3576::matchSharedFeature(producer, logical(), too_small, logical(), "capacity"),
            "原生容量不匹配未拒绝");
    std::cout << "NanoTrack 原生特征契约拒绝测试通过\n";
}
