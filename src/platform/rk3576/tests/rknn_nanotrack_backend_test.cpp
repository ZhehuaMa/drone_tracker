#include "../rknn_nanotrack_backend.h"

#include <cmath>
#include <iostream>
#include <limits>

#include "../host_frame.h"
#include "../nanotrack_preprocessor.h"
#include "../yolo_preprocessor.h"
#include "nano_tracker.h"
#include "timing_observer.h"
#include <array>
#include "rknn_test_support.h"

namespace {
using namespace rknn_test;
// 与生产重排独立：由语义通道/像素计算 SDK 原生下标所对应的实数。
float featureValue(std::size_t offset, int side, bool nhwc) {
    const auto pixels{static_cast<std::size_t>(side * side)};
    const auto channel{nhwc ? offset % 96 : offset / pixels};
    const auto pixel{nhwc ? offset / 96 : offset % pixels};
    return static_cast<float>(channel) * 0.01f + static_cast<float>(pixel) * 0.0001f + 1.0f;
}
rknn_tensor_attr attr(std::uint32_t index, std::uint32_t channels, std::uint32_t side, rknn_tensor_type type,
                      bool nhwc) {
    return attribute(index,
                     nhwc ? std::vector<std::uint32_t>{1, side, side, channels}
                          : std::vector<std::uint32_t>{1, channels, side, side},
                     type, nhwc ? RKNN_TENSOR_NHWC : RKNN_TENSOR_NCHW);
}
Sdk models(bool int_backbone, bool int_head, bool nhwc, bool swapped) {
    const auto bb{int_backbone ? RKNN_TENSOR_INT8 : RKNN_TENSOR_FLOAT16};
    const auto ht{int_head ? RKNN_TENSOR_INT8 : RKNN_TENSOR_FLOAT16};
    Model z;
    z.inputs = {attr(0, 3, 127, bb, !nhwc)};
    z.outputs = {attr(0, 96, 8, bb, nhwc)};
    z.value = [nhwc](std::size_t, std::size_t j) { return featureValue(j, 8, nhwc); };
    Model x;
    x.inputs = {attr(0, 3, 255, bb, nhwc)};
    x.outputs = {attr(0, 96, 16, bb, !nhwc)};
    x.value = [nhwc](std::size_t, std::size_t j) { return featureValue(j, 16, !nhwc); };
    Model h;
    h.inputs = {attr(0, 96, swapped ? 16 : 8, ht, nhwc), attr(1, 96, swapped ? 8 : 16, ht, !nhwc)};
    h.inputs[0].scale = 0.2f;
    h.inputs[1].scale = 0.3f;
    h.outputs = {attr(0, swapped ? 4 : 2, 15, ht, !nhwc), attr(1, swapped ? 2 : 4, 15, ht, nhwc)};
    h.inspect = [nhwc, swapped](std::uint32_t, rknn_input *inputs) {
        for (std::size_t i{0}; i < 2; ++i) {
            const int side{(i == 0) == swapped ? 16 : 8};
            const bool layout{i == 0 ? nhwc : !nhwc};
            const auto *data{static_cast<const float *>(inputs[i].buf)};
            for (std::size_t j{0}; j < inputs[i].size / sizeof(float); ++j) {
                require(std::abs(data[j] - featureValue(j, side, layout)) < 1e-6f, "Head 特征索引/轴重排/量化实数错误");
            }
        }
    };
    h.value = [nhwc, swapped](std::size_t i, std::size_t j) {
        const bool classification{(i == 0) != swapped};
        const bool layout{i == 0 ? !nhwc : nhwc};
        const auto channels{classification ? 2U : 4U};
        const auto channel{layout ? j % channels : j / 225};
        const auto pixel{layout ? j / channels : j % 225};
        return static_cast<float>(channel * 10) + static_cast<float>(pixel) * 0.001f;
    };
    return {{z, x, h}};
}
PreparedInputRef image(int side) {
    auto data{std::make_shared<std::vector<float>>(3 * side * side, 128.25f)};
    return std::make_shared<rk3576::FloatInput>(data, ModelInputContract{{1, 3, side, side}});
}
auto load() { return rk3576::RknnNanoTrackBackend::load("new-template", "new-search", "new-head", api()); }
void checkCombinations() {
    for (bool ib : {false, true}) {
        for (bool ih : {false, true}) {
            for (bool layout : {false, true}) {
                for (bool swapped : {false, true}) {
                    auto fake{models(ib, ih, layout, swapped)};
                    sdk = &fake;
                    auto backend{load()};
                    require(static_cast<bool>(backend), "兼容模型装配失败");
                    require(!backend.value->search(image(255)) && fake.models[1].runs == 0, "无模板仍执行搜索");
                    require(static_cast<bool>(backend.value->setTemplate(image(127))), "模板失败");
                    auto result{backend.value->search(image(255))};
                    require(static_cast<bool>(result), "搜索失败");
                    auto cls{result.value.classification.read()};
                    auto loc{result.value.location.read()};
                    for (std::size_t c{0}; c < 4; ++c) {
                        for (std::size_t pixel{0}; pixel < 225; ++pixel) {
                            const float expected{static_cast<float>(c * 10) + static_cast<float>(pixel) * 0.001f};
                            require(std::abs(loc.data()[c * 225 + pixel] - expected) < 1e-6f, "位置输出重排错误");
                            if (c < 2) {
                                require(std::abs(cls.data()[c * 225 + pixel] - expected) < 1e-6f, "分类输出重排错误");
                            }
                        }
                    }
                    auto wrong{
                        std::make_shared<rk3576::ByteInput>(std::make_shared<const std::vector<std::uint8_t>>(10))};
                    require(backend.value->search(wrong).error.code == model::ModelError::PlatformTypeMismatch,
                            "外来存储未拒绝");
                    require(static_cast<bool>(backend.value->search(image(255))), "输入错误破坏有效模板");
                    require(fake.models[0].runs == 1, "搜索覆盖或重新建立了模板");
                    require(!backend.value->setTemplate(wrong) && !backend.value->search(image(255)),
                            "失败的新目标仍使用旧模板");
                    require(static_cast<bool>(backend.value->setTemplate(image(127))), "新模板无法恢复");
                    backend.value->reset();
                    backend.value->reset();
                    require(!backend.value->search(image(255)), "重复重置未使模板失效");
                    backend.value.reset();
                    for (const auto &m : fake.models) {
                        require(m.destroys == 1, "三个会话未全部回收");
                    }
                    require(loc.data()[899] > 30.0f, "后端销毁破坏返回输出");
                }
            }
        }
    }
}
void failures() {
    for (int fault{0}; fault < 10; ++fault) {
        auto fake{models(false, true, false, false)};
        sdk = &fake;
        if (fault < 3) {
            fake.models[fault].init_status = -1;
        }
        if (fault == 3) {
            fake.models[2].inputs[1] = attr(1, 96, 8, RKNN_TENSOR_INT8, false);
        }
        if (fault == 4) {
            fake.models[0].outputs[0] = attr(0, 95, 8, RKNN_TENSOR_FLOAT16, false);
        }
        if (fault == 5) {
            fake.models[2].outputs.push_back(attr(2, 4, 15, RKNN_TENSOR_INT8, false));
        }
        if (fault == 6) {
            fake.models[2].outputs[1] = attr(1, 2, 15, RKNN_TENSOR_INT8, false);
        }
        if (fault == 7) {
            fake.models[1].inputs[0] = attr(0, 3, 127, RKNN_TENSOR_FLOAT16, false);
        }
        if (fault == 8) {
            fake.models[2].outputs.pop_back();
        }
        if (fault == 9) {
            fake.models[0].inputs[0].qnt_type = RKNN_TENSOR_QNT_DFP;
        }
        auto result{load()};
        require(!result && !result.value && !result.error.model.empty(), "非法模型返回可用后端");
        for (std::size_t i{0}; i < fake.next; ++i) {
            require(fake.models[i].destroys == 1, "部分装配失败未回收已加载会话");
        }
    }
    for (int session : {0, 1, 2}) {
        auto fake{models(true, false, true, true)};
        sdk = &fake;
        auto backend{load()};
        require(static_cast<bool>(backend.value->setTemplate(image(127))), "建立初始模板失败");
        fake.models[session].get_status = -1;
        if (session == 0) {
            require(!backend.value->setTemplate(image(127)), "模板失败未传播");
        } else {
            auto result{backend.value->search(image(255))};
            require(!result && !result.value.classification.storage && !result.value.location.storage,
                    "搜索失败返回部分结果");
        }
        const auto runs{fake.models[1].runs};
        require(backend.value->search(image(255)).error.code == model::ModelError::NotReady &&
                    fake.models[1].runs == runs,
                "SDK 失效后仍运行");
    }
}
void floatingAffineMetadata() {
    for (const auto type : {RKNN_TENSOR_FLOAT16, RKNN_TENSOR_FLOAT32}) {
        for (const float scale : {1.0f, 0.2f, std::numeric_limits<float>::quiet_NaN()}) {
            auto fake{models(false, false, true, true)};
            sdk = &fake;
            for (auto &m : fake.models) {
                for (auto *attributes : {&m.inputs, &m.outputs}) {
                    for (auto &a : *attributes) {
                        a.type = type;
                        a.size = a.n_elems * (type == RKNN_TENSOR_FLOAT16 ? 2U : 4U);
                        a.qnt_type = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
                        a.scale = scale;
                        a.zp = scale == 1.0f ? 0 : 1000;
                    }
                }
            }
            auto backend{load()};
            require(static_cast<bool>(backend), "浮点 AFFINE 元数据导致误拒绝");
            require(static_cast<bool>(backend.value->setTemplate(image(127))), "浮点 AFFINE 模板失败");
            auto prediction{backend.value->search(image(255))};
            require(prediction && std::abs(prediction.value.location.read().data()[899] - 30.224f) < 1e-6f,
                    "浮点输出被 scale/zp 改写");
            // models() 中的独立 Head 输入检查也会逐元素验证特征没有被再次反量化。
        }
    }
}
void realTracker() {
    auto fake{models(true, true, true, true)};
    sdk = &fake;
    // 中心点有最高分。63.5 宽高在初始 20x20 目标的 scale=127/40 下仍为 20x20。
    fake.models[2].value = [](std::size_t i, std::size_t j) {
        if (i == 0) {
            return 31.75f;
        }
        const auto channel{j % 2};
        const auto pixel{j / 2};
        return channel == 1 && pixel == 112 ? std::log(9.0f) : 0.0f;
    };
    auto backend{load()};
    NanoTracker tracker{std::move(backend.value), std::make_unique<rk3576::NanoTrackPreprocessor>()};
    const auto frame{rk3576::copyGrayFrame(cv::Mat(200, 200, CV_8UC1, cv::Scalar{114}))};
    tracker.init(frame, {50, 60, 20, 20});
    require(tracker.isInitialized(), "真实 NanoTracker 初始化失败");
    cv::Rect2f box;
    const float confidence{tracker.update(frame, box)};
    require(std::abs(confidence - 0.9f) < 1e-5f && std::abs(box.x - 49.5f) < 1e-4f && std::abs(box.y - 59.5f) < 1e-4f &&
                std::abs(box.width - 20) < 1e-4f && std::abs(box.height - 20) < 1e-4f,
            "真实公共后处理偏离固定参考");
    const auto prior{box};
    require(tracker.update({}, box) == -1 && box == prior && tracker.isInitialized(), "输入失败改变公共状态");
    require(tracker.update(frame, box) > 0, "可恢复失败后无法继续");
    fake.models[2].get_status = -1;
    const auto previous{box};
    require(tracker.update(frame, box) == -1 && box == previous && tracker.isInitialized(), "设备失败改变公共语义");
    tracker.init(frame, {20, 20, 10, 10});
    require(!tracker.isInitialized(), "失效后端建立了虚假新目标");
    tracker.reset();
    tracker.reset();
}
class Counter final : public timing::Observer {
public:
    std::array<int, timing::stageCount> counts{};
    std::uint32_t begin(timing::Stage stage) noexcept override {
        ++counts[static_cast<std::size_t>(stage)];
        return 1;
    }
    void end(std::uint32_t, bool) noexcept override {}
};
void observationComparison() {
    std::array<cv::Rect2f, 10> reference_boxes{};
    std::array<float, 10> reference_scores{};
    for (bool observed : {false, true}) {
        auto fake{models(false, false, true, true)};
        sdk = &fake;
        auto backend{load()};
        require(backend.value->floatingInterfaces(), "纯浮点接口未识别");
        NanoTracker tracker{std::move(backend.value), std::make_unique<rk3576::NanoTrackPreprocessor>()};
        Counter counter;
        timing::Observation observation{observed ? &counter : nullptr};
        const auto frame{rk3576::copyGrayFrame(cv::Mat(200, 200, CV_8UC1, cv::Scalar{73}))};
        tracker.init(frame, {-3, 4, 20, 20});
        require(tracker.isInitialized(), "边缘模板初始化失败");
        for (std::size_t i{0}; i < reference_boxes.size(); ++i) {
            cv::Rect2f box;
            const float score{tracker.update(frame, box)};
            if (observed) {
                require(box == reference_boxes[i] && score == reference_scores[i], "观测改变框或分数");
            } else { reference_boxes[i] = box; reference_scores[i] = score; }
        }
        require(fake.models[0].runs == 1 && fake.models[1].runs == 10 && fake.models[2].runs == 10,
                "观测改变模型调用次数");
        if (observed) {
            require(counter.counts[static_cast<int>(timing::Stage::TemplateBackbone)] == 1
                && counter.counts[static_cast<int>(timing::Stage::SearchBackbone)] == 10
                && counter.counts[static_cast<int>(timing::Stage::Head)] == 10
                && counter.counts[static_cast<int>(timing::Stage::OutputsGet)] == 21,
                "模型角色或结果获取区间缺失");
        }
    }
}
} // namespace
int main() {
    checkCombinations();
    failures();
    floatingAffineMetadata();
    realTracker();
    observationComparison();
    std::cout << "NanoTrack 四精度组合、角色重排、失败状态与真实跟踪链通过\n";
}
