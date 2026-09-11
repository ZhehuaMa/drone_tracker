#include "../rknn_session.h"

#include <cmath>
#include <iostream>
#include <limits>

#include "rknn_test_support.h"

namespace {
using namespace rknn_test;
Model twoInputs() {
    Model m;
    m.inputs = {attribute(0, {1, 2, 2, 2}), attribute(1, {1, 2, 2, 2}, RKNN_TENSOR_INT8)};
    m.outputs = {attribute(0, {1, 2, 2, 2}, RKNN_TENSOR_INT8),
                 attribute(1, {1, 2, 2, 2}, RKNN_TENSOR_FLOAT16, RKNN_TENSOR_NHWC)};
    m.value = [](std::size_t i, std::size_t) { return i == 0 ? 1.0f : 128.0f; };
    return m;
}
rk3576::SessionInput input(std::uint32_t index) {
    auto owner{std::make_shared<std::vector<float>>(8, 1.0f)};
    return {index, {{1, 2, 2, 2}, TensorElementType::Float32, TensorLayout::Nchw, 32, {}}, owner->data(), 32, owner};
}
} // namespace
int main() {
    Sdk fake{{twoInputs()}};
    sdk = &fake;
    auto loaded{rk3576::RknnSession::load("arbitrary.rknn", api())};
    require(loaded && loaded.value->inputs().size() == 2 && loaded.value->outputs().size() == 2,
            "双输入/输出属性查询失败");
    require(loaded.value->describe().find("runtime=2.3.0 driver=0.9.8") != std::string::npos, "缺少版本描述");
    for (int fault{0}; fault < 11; ++fault) {
        std::vector<rk3576::SessionInput> values{input(0), input(1)};
        auto &bad{values[1]};
        if (fault == 0) {
            --bad.bytes;
        }
        if (fault == 1) {
            bad.index = 0;
        }
        if (fault == 2) {
            bad.index = 2;
        }
        if (fault == 3) {
            bad.owner.reset();
        }
        if (fault == 4) {
            bad.data = nullptr;
        }
        if (fault == 5) {
            bad.descriptor.shape = {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
        }
        if (fault == 6) {
            bad.descriptor.type = TensorElementType::Int32;
        }
        if (fault == 7) {
            bad.descriptor.layout = TensorLayout::Nhwc;
        }
        if (fault == 8) {
            --bad.descriptor.bytes;
        }
        if (fault == 9) {
            bad.descriptor.shape[0] = 0;
        }
        if (fault == 10) {
            values.pop_back();
        }
        auto result{loaded.value->execute(values)};
        require(!result && result.value.empty() && fake.models[0].sets == 0, "非法第二输入触发了设备提交");
    }
    auto first{loaded.value->execute({input(1), input(0)})};
    auto second{loaded.value->execute({input(0), input(1)})};
    require(first && second && first.value[0].read().data()[0] == 1.0f && first.value[1].read().data()[0] == 128.0f,
            "输出被二次反量化、归一化或覆盖");
    require(first.value[1].descriptor.type == TensorElementType::Float32 &&
                first.value[1].descriptor.layout == TensorLayout::Nhwc && !first.value[0].descriptor.quantization,
            "描述未反映实际浮点布局");
    loaded.value.reset();
    require(first.value[0].read().data()[0] == 1 && fake.models[0].destroys == 1, "销毁破坏旧输出");
    for (int fault{0}; fault < 10; ++fault) {
        Sdk invalid{{twoInputs()}};
        sdk = &invalid;
        auto &a{invalid.models[0].outputs[0]};
        if (fault == 0) {
            a.dims[2] = 0;
        }
        if (fault == 1) {
            a.scale = 0;
        }
        if (fault == 2) {
            a.scale = std::numeric_limits<float>::infinity();
        }
        if (fault == 3) {
            a.zp = 128;
        }
        if (fault == 4) {
            a.qnt_type = RKNN_TENSOR_QNT_DFP;
        }
        if (fault == 5) {
            a.fmt = RKNN_TENSOR_UNDEFINED;
        }
        if (fault == 6) {
            a.dims[0] = UINT32_MAX;
        }
        if (fault == 7) {
            --a.n_elems;
        }
        if (fault == 8) {
            --a.size;
        }
        if (fault == 9) {
            a = attribute(0, {1, 2, 2, 2}, RKNN_TENSOR_FLOAT32);
            a.qnt_type = RKNN_TENSOR_QNT_DFP;
        }
        auto result{rk3576::RknnSession::load("invalid", api())};
        require(!result && !result.value && invalid.models[0].destroys == 1, "非法属性未拒绝/未回收");
    }
    for (int fault{0}; fault < 5; ++fault) {
        Sdk failing{{twoInputs()}};
        sdk = &failing;
        auto session{rk3576::RknnSession::load("failure", api())};
        auto &m{failing.models[0]};
        if (fault == 0) {
            m.set_status = -1;
        }
        if (fault == 1) {
            m.run_status = -1;
        }
        if (fault == 2) {
            m.get_status = -1;
        }
        if (fault == 3) {
            m.release_status = -1;
        }
        if (fault == 4) {
            m.bad_size = true;
        }
        auto result{session.value->execute({input(0), input(1)})};
        require(!result && result.value.empty() && !session.value->ready(), "失败返回部分输出或仍就绪");
        require(m.destroys == 1 && m.waits == (fault == 1 || fault == 2 ? 1 : 0) && m.releases == (fault >= 3 ? 1 : 0),
                "停止与释放边界错误");
    }
    std::cout << "通用会话多输入、溢出、浮点输出、所有权与失败测试通过\n";
}
