#ifndef NANOTRACK_SEARCH_DIRECT_TEST_SUPPORT_H
#define NANOTRACK_SEARCH_DIRECT_TEST_SUPPORT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../shared_memory_api.h"

namespace search_direct_test {
inline void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline std::uint16_t floatToHalf(float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign{(bits >> 16) & 0x8000U};
    const std::int32_t exponent{static_cast<std::int32_t>((bits >> 23) & 0xFFU) - 127 + 15};
    const std::uint32_t mantissa{bits & 0x7FFFFFU};
    if (exponent <= 0 || exponent >= 0x1F) {
        // 受控测试只使用 FP16 正规数范围内可精确表示的数值。
        require(mantissa == 0 && exponent <= 0, "测试数值超出 FP16 正规数范围");
        return static_cast<std::uint16_t>(sign);
    }
    require((mantissa & 0x1FFFU) == 0, "测试数值无法在 FP16 中精确表示");
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
}

inline float halfToFloat(std::uint16_t half) {
    const std::uint32_t sign{static_cast<std::uint32_t>(half & 0x8000U) << 16};
    const std::uint32_t exponent{static_cast<std::uint32_t>(half >> 10) & 0x1FU};
    const std::uint32_t mantissa{static_cast<std::uint32_t>(half) & 0x3FFU};
    if (exponent == 0 && mantissa == 0) {
        float zero{};
        std::uint32_t bits{sign};
        std::memcpy(&zero, &bits, sizeof(zero));
        return zero;
    }
    require(exponent != 0 && exponent != 0x1F, "测试替身只产生 FP16 正规数");
    const std::uint32_t bits{sign | ((exponent + 127 - 15) << 23) | (mantissa << 13)};
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline rknn_tensor_attr logicalAttribute(std::uint32_t index, const std::vector<std::uint32_t> &shape,
                                         const char *name, rknn_tensor_type type = RKNN_TENSOR_FLOAT16,
                                         rknn_tensor_format format = RKNN_TENSOR_NCHW) {
    rknn_tensor_attr a{};
    a.index = index;
    a.n_dims = static_cast<std::uint32_t>(shape.size());
    a.n_elems = 1;
    std::strncpy(a.name, name, RKNN_MAX_NAME_LEN - 1);
    for (std::size_t i{0}; i < shape.size(); ++i) {
        a.dims[i] = shape[i];
        a.n_elems *= shape[i];
    }
    a.type = type;
    a.fmt = format;
    a.qnt_type = RKNN_TENSOR_QNT_NONE;
    a.size = a.n_elems * (type == RKNN_TENSOR_INT8 ? 1U : type == RKNN_TENSOR_FLOAT16 ? 2U : 4U);
    a.size_with_stride = a.size;
    return a;
}

/** @brief 构造本程序支持的原生 NC1HWC2 FP16 属性；w_stride 为 0 表示等于宽度。 */
inline rknn_tensor_attr nativeAttribute(std::uint32_t index, std::uint32_t batch, std::uint32_t groups,
                                        std::uint32_t height, std::uint32_t width, std::uint32_t group,
                                        const char *name) {
    rknn_tensor_attr a{};
    a.index = index;
    a.n_dims = 5;
    a.dims[0] = batch;
    a.dims[1] = groups;
    a.dims[2] = height;
    a.dims[3] = width;
    a.dims[4] = group;
    std::strncpy(a.name, name, RKNN_MAX_NAME_LEN - 1);
    a.n_elems = batch * groups * height * width * group;
    a.type = RKNN_TENSOR_FLOAT16;
    a.fmt = RKNN_TENSOR_NC1HWC2;
    a.qnt_type = RKNN_TENSOR_QNT_NONE;
    a.size = a.n_elems * 2;
    a.size_with_stride = a.n_elems * 2;
    return a;
}

/** @brief 一次绑定记录；valid 表示该绑定在最近一次普通提交之后仍然有效。 */
struct Binding {
    rknn_tensor_mem *memory{};
    bool valid{};
    bool pass_through{};
};

struct Buffer {
    std::vector<std::uint8_t> bytes{};
    std::int32_t fd{};
};

struct FakeModel {
    std::vector<rknn_tensor_attr> inputs{};
    std::vector<rknn_tensor_attr> outputs{};
    std::vector<rknn_tensor_attr> native_inputs{};
    std::vector<rknn_tensor_attr> native_outputs{};
    /** @brief 每个输入最近一次普通提交的逻辑值；空表示未提交。 */
    std::vector<std::vector<float>> submitted{};
    std::map<std::uint32_t, Binding> input_bindings{};
    std::map<std::uint32_t, Binding> output_bindings{};
    std::vector<std::vector<float>> produced{};
    std::vector<std::vector<float>> host_outputs{};
    int runs{};
    int sets{};
    int binds{};
    int destroys{};
    int init_status{};
    int query_status{};
    int native_query_status{};
    int set_status{};
    int bind_status{};
    int run_status{};
    int get_status{};
    int release_status{};
    int wait_status{};
    int destroy_mem_status{};
    bool create_mem_fails{false};
    bool import_fails{false};
    /** @brief 由逻辑输入计算逻辑输出；缺省时输出全部等于当前运行序号。 */
    std::function<void(FakeModel &)> compute{};
};

/**
 * @brief 受控 RKNN 设备替身。
 *
 * 它模拟本变更依赖的两条实机事实：绑定只指定内存、不复制数据；任何普通输入提交
 * 都会让该上下文上已建立的绑定失效。失效后执行模型时，被绑定的内存里只会留下零值，
 * 因此"只在初始化时绑定"或"先绑定再提交"的错误顺序都能被检测出来。
 *
 * 替身比真实设备更严格：真实设备在错误顺序下偶尔仍可能得到正确结果，替身则总是失败。
 */
struct FakeDevice {
    std::vector<FakeModel> models{};
    std::vector<std::string> paths{};
    std::map<rknn_context, std::size_t> contexts{};
    std::size_t next{};
    std::map<std::int32_t, std::shared_ptr<Buffer>> buffers{};
    std::map<rknn_tensor_mem *, std::shared_ptr<Buffer>> handles{};
    std::vector<std::unique_ptr<rknn_tensor_mem>> allocations{};
    std::int32_t next_fd{10};
    int live_handles{};
};

inline FakeDevice *device{};

inline FakeModel &fakeModel(rknn_context context) { return device->models.at(device->contexts.at(context)); }

inline std::size_t logicalChannels(const rknn_tensor_attr &a) {
    return a.fmt == RKNN_TENSOR_NCHW ? a.dims[1] : a.dims[3];
}

/** @brief 把逻辑 NCHW 值写成原生 NC1HWC2 FP16 字节。 */
inline void packNative(const std::vector<float> &values, const rknn_tensor_attr &native, std::uint8_t *target) {
    const std::uint32_t height{native.dims[2]};
    const std::uint32_t width{native.dims[3]};
    const std::uint32_t group{native.dims[4]};
    std::vector<std::uint16_t> raw(native.n_elems, 0);
    const std::size_t channels{values.size() / (static_cast<std::size_t>(height) * width)};
    for (std::size_t c{0}; c < channels; ++c) {
        for (std::uint32_t y{0}; y < height; ++y) {
            for (std::uint32_t x{0}; x < width; ++x) {
                const std::size_t offset{((c / group * height + y) * width + x) * group + (c % group)};
                raw[offset] = floatToHalf(values[(c * height + y) * width + x]);
            }
        }
    }
    std::memcpy(target, raw.data(), raw.size() * sizeof(std::uint16_t));
}

/** @brief 从原生 NC1HWC2 FP16 字节还原逻辑 NCHW 值。 */
inline std::vector<float> unpackNative(const std::uint8_t *source, const rknn_tensor_attr &native,
                                       std::size_t channels) {
    const std::uint32_t height{native.dims[2]};
    const std::uint32_t width{native.dims[3]};
    const std::uint32_t group{native.dims[4]};
    std::vector<std::uint16_t> raw(native.n_elems);
    std::memcpy(raw.data(), source, raw.size() * sizeof(std::uint16_t));
    std::vector<float> values(channels * height * width);
    for (std::size_t c{0}; c < channels; ++c) {
        for (std::uint32_t y{0}; y < height; ++y) {
            for (std::uint32_t x{0}; x < width; ++x) {
                const std::size_t offset{((c / group * height + y) * width + x) * group + (c % group)};
                values[(c * height + y) * width + x] = halfToFloat(raw[offset]);
            }
        }
    }
    return values;
}

inline int fakeInit(rknn_context *context, void *path, std::uint32_t size, std::uint32_t flags, rknn_init_extend *) {
    require(size == 0 && flags == 0, "应按路径同步加载且不启用异步标志");
    device->paths.emplace_back(static_cast<const char *>(path));
    *context = static_cast<rknn_context>(device->next + 1);
    device->contexts[*context] = device->next++;
    auto &m{fakeModel(*context)};
    m.submitted.assign(m.inputs.size(), {});
    m.produced.assign(m.outputs.size(), {});
    return m.init_status;
}

inline int fakeQuery(rknn_context context, rknn_query_cmd command, void *ptr, std::uint32_t) {
    auto &m{fakeModel(context)};
    if (command == RKNN_QUERY_NATIVE_INPUT_ATTR || command == RKNN_QUERY_NATIVE_OUTPUT_ATTR) {
        if (m.native_query_status) {
            return m.native_query_status;
        }
        auto &a{*static_cast<rknn_tensor_attr *>(ptr)};
        a = (command == RKNN_QUERY_NATIVE_INPUT_ATTR ? m.native_inputs : m.native_outputs).at(a.index);
        return 0;
    }
    if (m.query_status) {
        return m.query_status;
    }
    if (command == RKNN_QUERY_IN_OUT_NUM) {
        auto &n{*static_cast<rknn_input_output_num *>(ptr)};
        n.n_input = static_cast<std::uint32_t>(m.inputs.size());
        n.n_output = static_cast<std::uint32_t>(m.outputs.size());
    } else if (command == RKNN_QUERY_SDK_VERSION) {
        auto &v{*static_cast<rknn_sdk_version *>(ptr)};
        std::strcpy(v.api_version, "2.3.2");
        std::strcpy(v.drv_version, "0.9.8");
    } else {
        auto &a{*static_cast<rknn_tensor_attr *>(ptr)};
        a = (command == RKNN_QUERY_INPUT_ATTR ? m.inputs : m.outputs).at(a.index);
    }
    return 0;
}

inline int fakeInputsSet(rknn_context context, std::uint32_t count, rknn_input *values) {
    auto &m{fakeModel(context)};
    ++m.sets;
    require(count >= 1 && count <= m.inputs.size(), "输入数量超出范围");
    for (std::uint32_t i{0}; i < count; ++i) {
        const auto index{values[i].index};
        require(index < m.inputs.size() && values[i].buf && values[i].pass_through == 0, "索引或提交方式错误");
        require(values[i].type == RKNN_TENSOR_FLOAT32 && values[i].size == m.inputs[index].n_elems * sizeof(float) &&
                    values[i].fmt == m.inputs[index].fmt,
                "浮点转换提交契约错误");
        const auto *source{static_cast<const float *>(values[i].buf)};
        std::vector<float> logical(m.inputs[index].n_elems);
        if (m.inputs[index].fmt == RKNN_TENSOR_NHWC) {
            const std::uint32_t height{m.inputs[index].dims[1]};
            const std::uint32_t width{m.inputs[index].dims[2]};
            const std::uint32_t channels{m.inputs[index].dims[3]};
            for (std::uint32_t c{0}; c < channels; ++c) {
                for (std::uint32_t y{0}; y < height; ++y) {
                    for (std::uint32_t x{0}; x < width; ++x) {
                        logical[(c * height + y) * width + x] = source[(y * width + x) * channels + c];
                    }
                }
            }
        } else {
            std::copy(source, source + logical.size(), logical.begin());
        }
        m.submitted[index] = std::move(logical);
    }
    // 实机事实：普通提交会让本上下文上已建立的共享内存绑定失效。
    for (auto &binding : m.input_bindings) {
        binding.second.valid = false;
    }
    for (auto &binding : m.output_bindings) {
        binding.second.valid = false;
    }
    return m.set_status;
}

inline int fakeSetIoMem(rknn_context context, rknn_tensor_mem *memory, rknn_tensor_attr *attribute) {
    auto &m{fakeModel(context)};
    ++m.binds;
    require(memory && attribute, "绑定需要内存与属性");
    require(device->handles.count(memory) == 1, "绑定了未登记的内存句柄");
    const auto index{attribute->index};
    const bool is_input{index < m.native_inputs.size() &&
                        std::strcmp(attribute->name, m.native_inputs[index].name) == 0};
    const bool is_output{index < m.native_outputs.size() &&
                         std::strcmp(attribute->name, m.native_outputs[index].name) == 0};
    require(is_input != is_output, "绑定属性既不是已知输入也不是已知输出");
    if (m.bind_status) {
        return m.bind_status;
    }
    Binding binding{memory, true, attribute->pass_through != 0};
    if (is_input) {
        require(binding.pass_through, "共享输入必须按原样读取");
        m.input_bindings[index] = binding;
    } else {
        m.output_bindings[index] = binding;
    }
    return 0;
}

inline int fakeRun(rknn_context context, rknn_run_extend *) {
    auto &m{fakeModel(context)};
    ++m.runs;
    if (m.run_status) {
        return m.run_status;
    }
    // 读取输入：优先使用有效绑定；绑定失效时该输入只能看到零值。
    for (std::size_t i{0}; i < m.inputs.size(); ++i) {
        const auto binding{m.input_bindings.find(static_cast<std::uint32_t>(i))};
        if (binding == m.input_bindings.end()) {
            continue;
        }
        const auto channels{logicalChannels(m.inputs[i])};
        if (binding->second.valid) {
            const auto &buffer{device->handles.at(binding->second.memory)};
            m.submitted[i] = unpackNative(buffer->bytes.data() + binding->second.memory->offset, m.native_inputs[i],
                                          channels);
        } else {
            m.submitted[i].assign(m.inputs[i].n_elems, 0.0f);
        }
    }
    if (m.compute) {
        m.compute(m);
    } else {
        for (std::size_t i{0}; i < m.outputs.size(); ++i) {
            m.produced[i].assign(m.outputs[i].n_elems, static_cast<float>(m.runs));
        }
    }
    // 写出输出：有效绑定写入共享内存；失效绑定只会留下零值。
    for (std::size_t i{0}; i < m.outputs.size(); ++i) {
        const auto binding{m.output_bindings.find(static_cast<std::uint32_t>(i))};
        if (binding == m.output_bindings.end()) {
            continue;
        }
        auto &buffer{device->handles.at(binding->second.memory)};
        auto *target{buffer->bytes.data() + binding->second.memory->offset};
        if (binding->second.valid) {
            packNative(m.produced[i], m.native_outputs[i], target);
        } else {
            std::fill(target, target + m.native_outputs[i].size_with_stride, std::uint8_t{0});
        }
    }
    return 0;
}

inline int fakeWait(rknn_context context, rknn_run_extend *) { return fakeModel(context).wait_status; }

inline int fakeOutputsGet(rknn_context context, std::uint32_t count, rknn_output *outputs, rknn_output_extend *) {
    auto &m{fakeModel(context)};
    if (m.get_status) {
        return m.get_status;
    }
    require(count == m.outputs.size(), "输出数量错误");
    m.host_outputs.resize(count);
    for (std::size_t i{0}; i < count; ++i) {
        require(outputs[i].want_float == 1 && outputs[i].index == i, "输出未要求 SDK 转为 Float32");
        m.host_outputs[i] = m.produced[i];
        m.host_outputs[i].resize(m.outputs[i].n_elems);
        outputs[i].buf = m.host_outputs[i].data();
        outputs[i].size = static_cast<std::uint32_t>(m.host_outputs[i].size() * sizeof(float));
    }
    return 0;
}

inline int fakeOutputsRelease(rknn_context context, std::uint32_t, rknn_output *) {
    auto &m{fakeModel(context)};
    for (auto &values : m.host_outputs) {
        std::fill(values.begin(), values.end(), -999.0f);
    }
    return m.release_status;
}

inline int fakeDestroy(rknn_context context) {
    ++fakeModel(context).destroys;
    return 0;
}

inline rknn_tensor_mem *fakeCreateMem(rknn_context context, std::uint32_t size) {
    auto &m{fakeModel(context)};
    if (m.create_mem_fails) {
        return nullptr;
    }
    auto buffer{std::make_shared<Buffer>()};
    buffer->bytes.assign(size, 0);
    buffer->fd = device->next_fd++;
    device->buffers[buffer->fd] = buffer;
    auto handle{std::make_unique<rknn_tensor_mem>()};
    handle->virt_addr = buffer->bytes.data();
    handle->phys_addr = 0;
    handle->fd = buffer->fd;
    handle->offset = 0;
    handle->size = size;
    handle->flags = RKNN_TENSOR_MEMORY_FLAGS_ALLOC_INSIDE;
    auto *raw{handle.get()};
    device->allocations.push_back(std::move(handle));
    device->handles[raw] = buffer;
    ++device->live_handles;
    return raw;
}

inline rknn_tensor_mem *fakeCreateMemFromFd(rknn_context context, std::int32_t fd, void *virt_addr, std::uint32_t size,
                                            std::int32_t offset) {
    auto &m{fakeModel(context)};
    if (m.import_fails || !device->buffers.count(fd)) {
        return nullptr;
    }
    auto buffer{device->buffers.at(fd)};
    require(virt_addr == buffer->bytes.data(), "导入地址与原始内存不一致");
    auto handle{std::make_unique<rknn_tensor_mem>()};
    handle->virt_addr = virt_addr;
    handle->phys_addr = 0;
    handle->fd = fd;
    handle->offset = offset;
    handle->size = size;
    handle->flags = RKNN_TENSOR_MEMORY_FLAGS_FROM_FD;
    auto *raw{handle.get()};
    device->allocations.push_back(std::move(handle));
    device->handles[raw] = buffer;
    ++device->live_handles;
    return raw;
}

inline int fakeDestroyMem(rknn_context context, rknn_tensor_mem *memory) {
    auto &m{fakeModel(context)};
    require(device->handles.count(memory) == 1, "重复释放或释放未登记的内存句柄");
    if (m.destroy_mem_status) {
        return m.destroy_mem_status;
    }
    device->handles.erase(memory);
    --device->live_handles;
    return 0;
}

inline int fakeMemSync(rknn_context, rknn_tensor_mem *memory, rknn_mem_sync_mode) {
    require(device->handles.count(memory) == 1, "同步了未登记的内存句柄");
    return 0;
}

inline search_direct::SharedMemoryApi fakeApi() {
    search_direct::SharedMemoryApi api;
    api.core = {fakeInit,           fakeQuery, fakeInputsSet,       fakeRun,
                fakeWait,           fakeOutputsGet, fakeOutputsRelease, fakeDestroy};
    api.create_mem = fakeCreateMem;
    api.create_mem_from_fd = fakeCreateMemFromFd;
    api.destroy_mem = fakeDestroyMem;
    api.set_io_mem = fakeSetIoMem;
    api.mem_sync = fakeMemSync;
    return api;
}

inline rk3576::RknnSharedMemoryApi productionSharedMemoryApi(const search_direct::SharedMemoryApi &api) {
    return {api.create_mem, api.create_mem_from_fd, api.destroy_mem, api.set_io_mem, api.mem_sync};
}

/** @brief 构造与 baseline 相同形状的三模型 NanoTrackV3 组合，搜索边为原生 FP16 共享。 */
inline FakeDevice nanoTrackModels() {
    FakeDevice fake;
    FakeModel z, x, head;
    z.inputs = {logicalAttribute(0, {1, 3, 127, 127}, "z_image")};
    z.outputs = {logicalAttribute(0, {1, 96, 8, 8}, "z_feature")};
    z.native_inputs = {nativeAttribute(0, 1, 1, 127, 127, 3, "z_image_native")};
    z.native_outputs = {nativeAttribute(0, 1, 12, 8, 8, 8, "z_feature_native")};
    x.inputs = {logicalAttribute(0, {1, 3, 255, 255}, "x_image")};
    x.outputs = {logicalAttribute(0, {1, 96, 16, 16}, "x_feature")};
    x.native_inputs = {nativeAttribute(0, 1, 1, 255, 255, 3, "x_image_native")};
    x.native_outputs = {nativeAttribute(0, 1, 12, 16, 16, 8, "x_feature_native")};
    head.inputs = {logicalAttribute(0, {1, 96, 8, 8}, "head_template"),
                   logicalAttribute(1, {1, 96, 16, 16}, "head_search")};
    head.outputs = {logicalAttribute(0, {1, 2, 15, 15}, "head_cls"),
                    logicalAttribute(1, {1, 4, 15, 15}, "head_loc")};
    head.native_inputs = {nativeAttribute(0, 1, 12, 8, 8, 8, "head_template_native"),
                          nativeAttribute(1, 1, 12, 16, 16, 8, "head_search_native")};
    head.native_outputs = {nativeAttribute(0, 1, 1, 15, 15, 2, "head_cls_native"),
                           nativeAttribute(1, 1, 1, 15, 15, 4, "head_loc_native")};
    // 特征值取 0.5 的整数倍，保证在 FP16 中可以精确表示，便于逐元素比较。
    z.compute = [](FakeModel &m) {
        const float seed{static_cast<float>(static_cast<int>(m.submitted[0].at(0)) % 8) * 0.5f};
        m.produced[0].assign(m.outputs[0].n_elems, 0.0f);
        for (std::size_t i{0}; i < m.produced[0].size(); ++i) {
            m.produced[0][i] = seed + static_cast<float>(i % 4) * 0.5f;
        }
    };
    x.compute = [](FakeModel &m) {
        const float seed{static_cast<float>(static_cast<int>(m.submitted[0].at(0)) % 8) * 0.5f};
        m.produced[0].assign(m.outputs[0].n_elems, 0.0f);
        for (std::size_t i{0}; i < m.produced[0].size(); ++i) {
            m.produced[0][i] = seed + static_cast<float>(i % 6) * 0.5f;
        }
    };
    head.compute = [](FakeModel &m) {
        double templates{};
        double searches{};
        for (const auto value : m.submitted[0]) {
            templates += value;
        }
        for (const auto value : m.submitted[1]) {
            searches += value;
        }
        m.produced[0].assign(m.outputs[0].n_elems, 0.0f);
        m.produced[1].assign(m.outputs[1].n_elems, 0.0f);
        for (std::size_t i{0}; i < m.produced[0].size(); ++i) {
            // 分类输出必须让第二通道在某个位置明显更大，公共算法才会得到有限分数。
            m.produced[0][i] = i == 300 ? 6.0f : static_cast<float>(templates + searches) * 1e-6f;
        }
        for (std::size_t i{0}; i < m.produced[1].size(); ++i) {
            m.produced[1][i] = 10.0f + static_cast<float>(searches) * 1e-6f;
        }
    };
    fake.models = {z, x, head};
    return fake;
}
} // namespace search_direct_test
#endif // NANOTRACK_SEARCH_DIRECT_TEST_SUPPORT_H
