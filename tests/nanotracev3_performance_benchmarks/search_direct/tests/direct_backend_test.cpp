#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "direct_test_support.h"
#include "platform/rk3576/nanotrack_preprocessor.h"
#include "platform/rk3576/rknn_nanotrack_backend.h"
#include "search_direct_backend.h"

namespace {
using namespace search_direct;
using namespace search_direct_test;

/** @brief 受控测试用的简单主机存储，供直接驱动会话时提供输入。 */
class PlainStorage final : public model::TensorStorage {
public:
    explicit PlainStorage(std::vector<float> initial) : values{std::move(initial)} {}
    const float *hostData() const noexcept override { return values.data(); }
    std::size_t byteSize() const noexcept override { return values.size() * sizeof(float); }
    std::vector<float> values;
};

PreparedInputRef image(int size, float seed) {
    auto values{std::make_shared<std::vector<float>>(static_cast<std::size_t>(3 * size * size))};
    for (std::size_t i{0}; i < values->size(); ++i) {
        // 保留小数，确认直连路径没有偷偷把输入取整或改变前处理。
        (*values)[i] = seed + static_cast<float>(i % 17) * 0.25f;
    }
    ModelInputContract contract;
    contract.shape = {1, 3, size, size};
    contract.element_type = TensorElementType::Float32;
    contract.layout = TensorLayout::Nchw;
    contract.scale = 1.0f;
    contract.offset = 0.0f;
    return std::make_shared<const rk3576::FloatInput>(values, contract);
}

std::vector<float> flatten(const model::NanoPrediction &prediction) {
    std::vector<float> values;
    for (const auto *tensor : {&prediction.classification, &prediction.location}) {
        const auto view{tensor->read()};
        values.insert(values.end(), view.data(), view.data() + view.size());
    }
    return values;
}

void checkAdmission() {
    auto fake{nanoTrackModels()};
    const auto producer{fake.models[1].native_outputs[0]};
    const auto producer_logical{fake.models[1].outputs[0]};
    auto consumer{fake.models[2].native_inputs[1]};
    const auto consumer_logical{fake.models[2].inputs[1]};
    // 输出报告零跨度、输入报告显式宽度，按 SDK 约定有效跨度相同，应当接受。
    consumer.w_stride = consumer.dims[3];
    const auto accepted{matchSharedFeature(producer, producer_logical, consumer, consumer_logical, "controlled")};
    require(static_cast<bool>(accepted), "零跨度与显式宽度应判定为兼容");
    require(accepted.value.bytes == 49152, "共享容量应为按有效跨度计算的字节数");
    require(accepted.value.consumer.pass_through == 1, "共享输入必须按原样读取");
    auto padded{consumer};
    padded.w_stride = 32;
    padded.size_with_stride = 98304;
    const auto padded_result{matchSharedFeature(padded, producer_logical, padded, consumer_logical, "controlled")};
    require(static_cast<bool>(padded_result) && padded_result.value.bytes == 98304,
            "两端相同的合法行填充必须接受");

    const auto unsafe{[&](const rknn_tensor_attr &broken, const char *message) {
        // 两端同时畸形也必须拒绝，不能依赖两端属性不相等来发现错误。
        const auto result{matchSharedFeature(broken, producer_logical, broken, consumer_logical, "controlled")};
        require(!result, message);
        require(!result.error.actual.empty() && !result.error.expected.empty(), "拒绝必须包含实际与预期属性");
        auto invalid_fake{nanoTrackModels()};
        invalid_fake.models[1].native_outputs[0] = broken;
        device = &invalid_fake;
        const auto backend{SearchDirectBackend::load("z", "x", "head", fakeApi())};
        require(!backend && invalid_fake.allocations.empty(), "畸形属性必须在设备内存分配之前失败");
        for (const auto &model : invalid_fake.models) {
            require(model.runs == 0, "畸形属性不得进入设备执行");
        }
        device = nullptr;
    }};
    auto short_stride{consumer};
    short_stride.w_stride = 8;
    short_stride.size_with_stride = 24576;
    unsafe(short_stride, "即使两端容量按不足跨度自洽也必须拒绝");
    auto short_capacity{consumer};
    short_capacity.size_with_stride = 24576;
    unsafe(short_capacity, "不足以容纳完整原生特征的容量必须拒绝");
    auto wrong_size{consumer};
    wrong_size.size /= 2;
    unsafe(wrong_size, "size 不等于 n_elems × 2 必须拒绝");
    auto overflowing_stride{consumer};
    overflowing_stride.w_stride = std::numeric_limits<std::uint32_t>::max();
    unsafe(overflowing_stride, "带跨度容量溢出必须拒绝");
    auto overflowing_group{consumer};
    overflowing_group.dims[1] = 1;
    overflowing_group.dims[4] = std::numeric_limits<std::uint32_t>::max();
    unsafe(overflowing_group, "极端通道分组不能使上取整或维度乘积回绕");
    auto overflowing_bytes{consumer};
    overflowing_bytes.dims[1] = 1;
    overflowing_bytes.dims[4] = 1U << 23;
    overflowing_bytes.n_elems = 1U << 31;
    overflowing_bytes.size = 0;
    unsafe(overflowing_bytes, "原生元素数可表示但 FP16 字节数溢出必须拒绝");
    auto huge_native{consumer};
    auto huge_logical{producer_logical};
    huge_native.dims[0] = huge_native.dims[2] = huge_native.dims[3] = 1U << 30;
    huge_logical.dims[0] = huge_logical.dims[2] = huge_logical.dims[3] = 1U << 30;
    const auto huge_result{
        matchSharedFeature(huge_native, huge_logical, huge_native, huge_logical, "controlled")};
    require(!huge_result && huge_result.error.message.find("溢出") != std::string::npos,
            "超出 uint64_t 的维度乘积也必须在乘法前拒绝，不能回绕");

    const auto rejected{[&](rknn_tensor_attr broken, const char *message) {
        const auto result{matchSharedFeature(producer, producer_logical, broken, consumer_logical, "controlled")};
        require(!result, message);
        require(!result.error.actual.empty() && !result.error.expected.empty(), "拒绝必须包含实际与预期属性");
    }};
    auto wrong_type{consumer};
    wrong_type.type = RKNN_TENSOR_FLOAT32;
    rejected(wrong_type, "非 FP16 原生类型必须拒绝");
    auto wrong_format{consumer};
    wrong_format.fmt = RKNN_TENSOR_NHWC;
    rejected(wrong_format, "非 NC1HWC2 原生排列必须拒绝");
    auto wrong_group{nativeAttribute(1, 1, 24, 16, 16, 4, "head_search_native")};
    rejected(wrong_group, "通道分组不同必须拒绝");
    auto wrong_capacity{consumer};
    wrong_capacity.size_with_stride = consumer.size_with_stride * 2;
    rejected(wrong_capacity, "带跨度容量不一致必须拒绝");
    auto wrong_quant{consumer};
    wrong_quant.qnt_type = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
    wrong_quant.scale = 0.5f;
    rejected(wrong_quant, "量化元数据不同必须拒绝");
    auto mismatched_channels{nativeAttribute(1, 1, 8, 16, 16, 8, "head_search_native")};
    const auto channel_result{
        matchSharedFeature(producer, producer_logical, mismatched_channels, consumer_logical, "controlled")};
    require(!channel_result, "原生分组覆盖不了逻辑通道必须拒绝");
    std::cout << "原生属性准入检查通过\n";
}

void checkRoles() {
    auto fake{nanoTrackModels()};
    const auto attributes{[](const FakeModel &m) {
        return SessionAttributes{"controlled", "controlled", &m.inputs, &m.outputs};
    }};
    const auto roles{attributes(fake.models[2])};
    const auto resolved{resolveNanoTrackRoles(attributes(fake.models[0]), attributes(fake.models[1]), roles)};
    require(static_cast<bool>(resolved) && resolved.value.template_input == 0 && resolved.value.search_input == 1,
            "常规顺序的角色识别错误");
    FakeModel swapped{fake.models[2]};
    swapped.inputs = {logicalAttribute(0, {1, 96, 16, 16}, "head_search"),
                      logicalAttribute(1, {1, 96, 8, 8}, "head_template")};
    swapped.outputs = {logicalAttribute(0, {1, 4, 15, 15}, "head_loc"),
                       logicalAttribute(1, {1, 2, 15, 15}, "head_cls")};
    const auto reordered{
        resolveNanoTrackRoles(attributes(fake.models[0]), attributes(fake.models[1]), attributes(swapped))};
    require(static_cast<bool>(reordered) && reordered.value.search_input == 0 &&
                reordered.value.template_input == 1 && reordered.value.location_output == 0 &&
                reordered.value.classification_output == 1,
            "角色索引重排后必须按形状识别");
    FakeModel duplicated{fake.models[2]};
    duplicated.inputs = {logicalAttribute(0, {1, 96, 8, 8}, "head_template"),
                         logicalAttribute(1, {1, 96, 8, 8}, "head_template_again")};
    require(!resolveNanoTrackRoles(attributes(fake.models[0]), attributes(fake.models[1]), attributes(duplicated)),
            "重复角色必须拒绝");
    std::cout << "角色识别检查通过\n";
}

/** @brief 用生产普通路径在独立设备替身上生成参照输出，避免与被测路径共用缓冲区。 */
std::vector<std::vector<float>> conventionalReference(const std::vector<std::pair<int, float>> &sequence) {
    auto fake{nanoTrackModels()};
    device = &fake;
    std::vector<std::vector<float>> results;
    {
        const auto api{fakeApi()};
        auto backend{rk3576::RknnNanoTrackBackend::load(
            "z", "x", "head", api.core, productionSharedMemoryApi(api))};
        require(static_cast<bool>(backend), "参照后端加载失败");
        float current_template{std::numeric_limits<float>::quiet_NaN()};
        for (const auto &step : sequence) {
            if (step.first == 0) {
                require(static_cast<bool>(backend.value->setTemplate(image(127, step.second))), "参照模板失败");
                current_template = step.second;
                continue;
            }
            require(std::isfinite(current_template), "参照顺序错误");
            auto prediction{backend.value->search(image(255, step.second))};
            require(static_cast<bool>(prediction), "参照搜索失败");
            results.push_back(flatten(prediction.value));
        }
    }
    device = nullptr;
    return results;
}

void checkDirectNumerics() {
    // 模板在两个种子之间切换，搜索按 A/B/A/C 交替，覆盖复用同一缓冲区的情况。
    const std::vector<std::pair<int, float>> sequence{{0, 3.0f}, {1, 11.0f}, {1, 22.0f}, {1, 11.0f}, {1, 33.0f},
                                                      {0, 5.0f}, {1, 22.0f}, {1, 11.0f}, {1, 33.0f}};
    const auto reference{conventionalReference(sequence)};
    auto fake{nanoTrackModels()};
    device = &fake;
    std::vector<std::vector<float>> actual;
    {
        auto loaded{SearchDirectBackend::load("z", "x", "head", fakeApi())};
        require(static_cast<bool>(loaded), "搜索直连后端加载失败");
        auto &backend{*loaded.value};
        require(fake.live_handles == 2, "共享内存应当有一个原始句柄和一个导入句柄");
        for (const auto &step : sequence) {
            if (step.first == 0) {
                require(static_cast<bool>(backend.setTemplate(image(127, step.second))), "直连模板失败");
                continue;
            }
            auto prediction{backend.search(image(255, step.second))};
            require(static_cast<bool>(prediction), "直连搜索失败");
            actual.push_back(flatten(prediction.value));
            // 消费者执行完成之后才读取共享特征，仅用于诊断，不属于直连链。
            const auto feature{backend.readSharedFeatureForDiagnostics()};
            require(static_cast<bool>(feature), "诊断读取共享特征失败");
            require(feature.value == fake.models[1].produced[0], "共享特征与搜索模型当帧输出不一致");
        }
        require(fake.models[0].runs == 2 && fake.models[1].runs == 7 && fake.models[2].runs == 7,
                "模型执行次数与基线不一致");
        require(fake.models[2].sets == 7, "head 每次更新只应普通提交模板输入");
        require(fake.models[1].binds == 7 && fake.models[2].binds == 7, "每次更新两端都必须重新绑定");
        require(fake.models[0].binds == 0, "模板路径不应使用共享内存");
    }
    require(fake.live_handles == 0, "共享内存句柄未按所有权顺序释放");
    device = nullptr;
    require(actual.size() == reference.size(), "样本数不同");
    for (std::size_t i{0}; i < actual.size(); ++i) {
        require(actual[i] == reference[i], "直连结果与独立普通参照不是逐元素相同");
    }
    // 输入敏感性：不同搜索输入必须得到不同参照输出，否则相等本身没有意义。
    std::vector<std::vector<float>> distinct{reference.begin(), reference.end()};
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    require(distinct.size() >= 4, "参照输出缺少输入敏感性，相等结论不成立");
    std::cout << "直连数值与独立参照一致，覆盖模板切换和搜索交替\n";
}

/** @brief 证明测试替身能区分错误的绑定顺序，否则顺序检查没有意义。 */
void checkWrongOrdersDetected() {
    for (const std::string mode : {"bind_only_at_init", "bind_before_inputs_set"}) {
        auto fake{nanoTrackModels()};
        device = &fake;
        {
            // 会话按模板、搜索、head 的顺序创建，与正式后端的加载顺序一致。
            auto z{DirectSession::load("z", fakeApi())};
            auto x{DirectSession::load("x", fakeApi())};
            auto head{DirectSession::load("head", fakeApi())};
            require(static_cast<bool>(z) && static_cast<bool>(x) && static_cast<bool>(head), "会话加载失败");
            auto shared{x.value->allocate(x.value->nativeOutputs()[0].size_with_stride)};
            require(static_cast<bool>(shared), "共享内存分配失败");
            auto imported{head.value->importFrom(*shared.value)};
            require(static_cast<bool>(imported), "共享内存导入失败");
            auto consumer{head.value->nativeInputs()[1]};
            consumer.pass_through = 1;
            if (mode == "bind_only_at_init") {
                require(static_cast<bool>(x.value->bind(x.value->nativeOutputs()[0], shared.value)), "初始绑定失败");
                require(static_cast<bool>(head.value->bind(consumer, imported.value)), "初始绑定失败");
            }
            auto storage{std::make_shared<PlainStorage>(std::vector<float>(3 * 255 * 255, 7.0f))};
            const model::TensorDescriptor descriptor{{1, 3, 255, 255},
                                                     TensorElementType::Float32,
                                                     TensorLayout::Nchw,
                                                     storage->byteSize(),
                                                     {}};
            rk3576::SessionInput request{0, descriptor, storage->hostData(), descriptor.bytes, storage};
            if (mode == "bind_before_inputs_set") {
                require(static_cast<bool>(x.value->bind(x.value->nativeOutputs()[0], shared.value)), "提前绑定失败");
            }
            require(static_cast<bool>(x.value->submit({request})), "搜索提交失败");
            require(static_cast<bool>(x.value->run()), "搜索执行失败");
            x.value->confirmCompleted();
            const auto *bytes{static_cast<const std::uint8_t *>(shared.value->virt_addr)};
            const bool all_zero{
                std::all_of(bytes, bytes + shared.value->size, [](std::uint8_t value) { return value == 0; })};
            require(all_zero, "错误绑定顺序未被替身检测为空数据: " + mode);
            head.value->releaseMemory(imported.value);
            x.value->releaseMemory(shared.value);
        }
        require(fake.live_handles == 0, "错误顺序场景仍然泄漏内存句柄");
        device = nullptr;
    }
    std::cout << "错误绑定顺序被受控替身检测\n";
}

void checkResetAndTemplateInvalidation() {
    auto fake{nanoTrackModels()};
    device = &fake;
    {
        auto loaded{SearchDirectBackend::load("z", "x", "head", fakeApi())};
        require(static_cast<bool>(loaded), "后端加载失败");
        auto &backend{*loaded.value};
        require(!backend.search(image(255, 11.0f)), "无模板时必须拒绝搜索");
        require(static_cast<bool>(backend.setTemplate(image(127, 3.0f))), "模板建立失败");
        require(static_cast<bool>(backend.search(image(255, 11.0f))), "模板建立后搜索应当成功");
        backend.reset();
        require(!backend.search(image(255, 11.0f)), "reset 之后必须拒绝搜索");
        // 模板重建失败时旧模板不得继续可用。
        require(static_cast<bool>(backend.setTemplate(image(127, 3.0f))), "模板重建失败");
        fake.models[0].run_status = -1;
        require(!backend.setTemplate(image(127, 5.0f)), "模板执行失败必须返回失败");
        require(!backend.search(image(255, 11.0f)), "模板重建失败后不能使用旧模板");
    }
    require(fake.live_handles == 0, "失败路径仍然泄漏内存句柄");
    device = nullptr;
    std::cout << "reset 与模板失效行为通过\n";
}
} // namespace

int main() {
    try {
        checkAdmission();
        checkRoles();
        checkDirectNumerics();
        checkWrongOrdersDetected();
        checkResetAndTemplateInvalidation();
        std::cout << "search_direct 受控后端测试通过\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
