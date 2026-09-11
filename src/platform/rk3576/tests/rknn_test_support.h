#ifndef RK3576_RKNN_TEST_SUPPORT_H
#define RK3576_RKNN_TEST_SUPPORT_H

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../rknn_api.h"

namespace rknn_test {
inline void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
inline rknn_tensor_attr attribute(std::uint32_t index, std::vector<std::uint32_t> shape,
                                  rknn_tensor_type type = RKNN_TENSOR_FLOAT16,
                                  rknn_tensor_format format = RKNN_TENSOR_NCHW) {
    rknn_tensor_attr a{};
    a.index = index;
    a.n_dims = static_cast<std::uint32_t>(shape.size());
    a.n_elems = 1;
    std::strcpy(a.name, "replaceable_weight_node");
    for (std::size_t i{0}; i < shape.size(); ++i) {
        a.dims[i] = shape[i];
        a.n_elems *= shape[i];
    }
    a.type = type;
    a.fmt = format;
    a.qnt_type = type == RKNN_TENSOR_INT8 ? RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC : RKNN_TENSOR_QNT_NONE;
    a.scale = type == RKNN_TENSOR_INT8 ? 0.1f : 0;
    a.zp = 0;
    a.size = a.n_elems * (type == RKNN_TENSOR_INT8 ? 1U : type == RKNN_TENSOR_FLOAT16 ? 2U : 4U);
    return a;
}
struct Model {
    std::vector<rknn_tensor_attr> inputs{};
    std::vector<rknn_tensor_attr> outputs{};
    std::vector<std::vector<float>> data{};
    int runs{};
    int sets{};
    int destroys{};
    int releases{};
    int waits{};
    int init_status{};
    int query_status{};
    int set_status{};
    int run_status{};
    int get_status{};
    int release_status{};
    bool bad_size{false};
    std::function<void(std::uint32_t, rknn_input *)> inspect{};
    std::function<float(std::size_t, std::size_t)> value{};
};
struct Sdk {
    std::vector<Model> models{};
    std::vector<std::string> paths{};
    std::map<rknn_context, std::size_t> contexts{};
    std::size_t next{};
};
inline Sdk *sdk{};
inline Model &model(rknn_context context) { return sdk->models.at(sdk->contexts.at(context)); }
inline int init(rknn_context *context, void *path, std::uint32_t size, std::uint32_t flags, rknn_init_extend *) {
    require(size == 0 && flags == 0, "应按路径同步加载");
    sdk->paths.emplace_back(static_cast<const char *>(path));
    *context = static_cast<rknn_context>(sdk->next + 1);
    sdk->contexts[*context] = sdk->next++;
    return model(*context).init_status;
}
inline int query(rknn_context context, rknn_query_cmd command, void *ptr, std::uint32_t) {
    auto &m{model(context)};
    if (m.query_status) {
        return m.query_status;
    }
    if (command == RKNN_QUERY_IN_OUT_NUM) {
        auto &n{*static_cast<rknn_input_output_num *>(ptr)};
        n.n_input = static_cast<std::uint32_t>(m.inputs.size());
        n.n_output = static_cast<std::uint32_t>(m.outputs.size());
    } else if (command == RKNN_QUERY_SDK_VERSION) {
        auto &v{*static_cast<rknn_sdk_version *>(ptr)};
        std::strcpy(v.api_version, "2.3.0");
        std::strcpy(v.drv_version, "0.9.8");
    } else {
        auto &a{*static_cast<rknn_tensor_attr *>(ptr)};
        a = (command == RKNN_QUERY_INPUT_ATTR ? m.inputs : m.outputs).at(a.index);
    }
    return 0;
}
inline int inputs(rknn_context context, std::uint32_t count, rknn_input *values) {
    auto &m{model(context)};
    ++m.sets;
    require(count == m.inputs.size(), "输入数量错误");
    for (std::uint32_t i{0}; i < count; ++i) {
        require(values[i].index == i && values[i].pass_through == 0 && values[i].buf, "索引或提交方式错误");
        require(values[i].type == RKNN_TENSOR_FLOAT32 && values[i].size == m.inputs[i].n_elems * sizeof(float) &&
                    values[i].fmt == m.inputs[i].fmt,
                "浮点转换提交契约错误");
    }
    if (m.inspect) {
        m.inspect(count, values);
    }
    return m.set_status;
}
inline int run(rknn_context context, rknn_run_extend *) {
    ++model(context).runs;
    return model(context).run_status;
}
inline int wait(rknn_context context, rknn_run_extend *) {
    ++model(context).waits;
    return 0;
}
inline int get(rknn_context context, std::uint32_t count, rknn_output *outputs, rknn_output_extend *) {
    auto &m{model(context)};
    if (m.get_status) {
        return m.get_status;
    }
    require(count == m.outputs.size(), "输出数量错误");
    m.data.resize(count);
    for (std::size_t i{0}; i < count; ++i) {
        require(outputs[i].want_float == 1 && outputs[i].index == i && outputs[i].is_prealloc == 0,
                "输出未要求 SDK 转为 Float32");
        m.data[i].resize(m.outputs[i].n_elems);
        for (std::size_t j{0}; j < m.data[i].size(); ++j) {
            m.data[i][j] = m.value ? m.value(i, j) : static_cast<float>(m.runs);
        }
        outputs[i].buf = m.data[i].data();
        outputs[i].size = static_cast<std::uint32_t>(m.data[i].size() * sizeof(float)) - (m.bad_size ? 1 : 0);
    }
    return 0;
}
inline int release(rknn_context context, std::uint32_t, rknn_output *) {
    auto &m{model(context)};
    ++m.releases;
    for (auto &data : m.data) {
        std::fill(data.begin(), data.end(), -999.0f);
    }
    return m.release_status;
}
inline int destroy(rknn_context context) {
    ++model(context).destroys;
    return 0;
}
inline rk3576::RknnApi api() { return {init, query, inputs, run, wait, get, release, destroy}; }
} // namespace rknn_test
#endif // RK3576_RKNN_TEST_SUPPORT_H
