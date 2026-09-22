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
    require(loaded.value->nativeInputs().size() == loaded.value->inputs().size() &&
                loaded.value->nativeOutputs().size() == loaded.value->outputs().size() &&
                loaded.value->nativeOutputs()[0].n_elems == loaded.value->outputs()[0].n_elems,
            "原生属性查询未保留张量数量与元素契约");
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

    Sdk shared_fake{{twoInputs()}};
    shared_fake.models[0].allow_partial = true;
    shared_fake.models[0].native_outputs = shared_fake.models[0].outputs;
    sdk = &shared_fake;
    auto shared_session{rk3576::RknnSession::load("shared", api(), sharedApi())};
    require(static_cast<bool>(shared_session), "共享内存会话加载失败");
    auto memory{shared_session.value->allocateShared(64)};
    require(memory && shared_fake.models[0].mem_creates == 1, "共享内存分配失败");
    auto imported{shared_session.value->importShared(*memory.value)};
    require(imported && imported.value->virt_addr == memory.value->virt_addr, "共享内存导入未复用存储");
    auto undersized{shared_session.value->allocateShared(8)};
    require(static_cast<bool>(undersized), "容量边界测试内存分配失败");
    auto bounded_attribute{shared_session.value->nativeOutputs()[0]};
    bounded_attribute.size_with_stride = bounded_attribute.size;
    undersized.value->size = bounded_attribute.size_with_stride - 1;
    require(!shared_session.value->bind(bounded_attribute, undersized.value) &&
                shared_fake.models[0].binds == 0,
            "容量不足的绑定未被拒绝");
    require(static_cast<bool>(shared_session.value->submit({input(0)})), "部分输入提交失败");
    auto attr{shared_session.value->nativeOutputs()[0]};
    require(static_cast<bool>(shared_session.value->bind(attr, memory.value)), "显式输出绑定失败");
    require(static_cast<bool>(shared_session.value->run()), "绑定执行失败");
    shared_session.value->confirmCompleted();
    require(shared_fake.models[0].releases == 0, "确认完成错误触发普通输出取回");
    require(static_cast<bool>(shared_session.value->submit({input(0)})), "第二次部分输入提交失败");
    require(shared_fake.models[0].binds == 1, "新提交错误复用了旧绑定");
    require(static_cast<bool>(shared_session.value->bind(attr, memory.value)) &&
                static_cast<bool>(shared_session.value->run()),
            "重新绑定后执行失败");
    shared_session.value->confirmCompleted();
    require(shared_session.value->syncFromDevice(memory.value) && shared_fake.models[0].mem_syncs == 1,
            "共享内存同步失败");
    require(static_cast<bool>(shared_session.value->releaseShared(imported.value)), "导入句柄释放失败");
    require(static_cast<bool>(shared_session.value->releaseShared(memory.value)), "原始句柄释放失败");
    require(shared_fake.models[0].mem_destroys == 2, "共享内存句柄未完整释放");

    Sdk no_extension{{twoInputs()}};
    sdk = &no_extension;
    auto ordinary{rk3576::RknnSession::load("ordinary", api())};
    require(ordinary && ordinary.value->execute({input(0), input(1)}), "无扩展时普通执行路径回归");
    require(!ordinary.value->allocateShared(64) &&
                ordinary.value->allocateShared(64).error.code == model::ModelError::NotReady,
            "缺少共享扩展未返回能力缺失诊断");
    std::cout << "原子提交、共享内存生命周期与扩展缺失兼容测试通过\n";
}
