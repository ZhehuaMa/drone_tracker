#include "model/yolo_output_adapter.h"
#include "model_test_support.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using model_test::require;
std::vector<model::Tensor> fixture(int classes) {
    std::vector<model::Tensor> outputs;
    for (int size : {80, 40, 20}) {
        for (int channels : {64, classes}) {
            auto tensor{model_test::tensor({1, channels, size, size})};
            tensor.descriptor.layout = TensorLayout::Nchw;
            outputs.push_back(std::move(tensor));
        }
    }
    return outputs;
}
void close(float actual, double expected) {
    require(std::abs(actual - expected) <= 1e-4 + 1e-5 * std::abs(expected), "数学转换超过容差");
}
void check(int classes, bool random) {
    auto input{fixture(classes)};
    if (random) {
        for (std::size_t index{0}; index < input.size(); ++index) {
            auto view{input[index].read()};
            auto *data{const_cast<float *>(view.data())};
            for (std::size_t i{0}; i < view.size(); ++i) {
                data[i] = static_cast<float>(std::sin(static_cast<double>(i + 137 * index)) * 4.0);
            }
        }
    }
    const auto adapted{model::adaptYoloSixOutputs(input, "test")};
    require(adapted && adapted.value.predictions.descriptor.shape == TensorShape{1, classes + 4, 8400}, "适配形状错误");
    const auto output{adapted.value.predictions.read()};
    std::size_t offset{0};
    // 独立参考直接计算 exp/logits、左右边坐标；逐个候选核对顺序。
    for (int scale{0}; scale < 3; ++scale) {
        const int size{80 >> scale};
        const int cells{size * size};
        const double stride{640.0 / size};
        const auto box{input[2 * scale].read()};
        const auto score{input[2 * scale + 1].read()};
        for (int y{0}; y < size; ++y) {
            for (int x{0}; x < size; ++x) {
                const int cell{y * size + x};
                std::array<double, 4> distances{};
                for (int side{0}; side < 4; ++side) {
                    double denominator{0};
                    for (int bin{0}; bin < 16; ++bin) {
                        const double p{std::exp(static_cast<double>(box.data()[(16 * side + bin) * cells + cell]))};
                        denominator += p;
                        distances[side] += bin * p;
                    }
                    distances[side] /= denominator;
                }
                const double left{(x + 0.5 - distances[0]) * stride};
                const double right{(x + 0.5 + distances[2]) * stride};
                const double top{(y + 0.5 - distances[1]) * stride};
                const double bottom{(y + 0.5 + distances[3]) * stride};
                close(output.data()[offset + cell], (left + right) / 2);
                close(output.data()[8400 + offset + cell], (top + bottom) / 2);
                close(output.data()[16800 + offset + cell], right - left);
                close(output.data()[25200 + offset + cell], bottom - top);
                for (int c{0}; c < classes; ++c) {
                    close(output.data()[(4 + static_cast<std::size_t>(c)) * 8400 + offset + cell],
                          1 / (1 + std::exp(-static_cast<double>(score.data()[c * cells + cell]))));
                }
            }
        }
        offset += cells;
    }
}
} // namespace
int main() {
    for (int classes : {1, 4, 6, 64}) {
        check(classes, false);
        check(classes, true);
    }
    check(8401, false); // 属性轴大于 8400 候选轴，仍按固定轴转换完整数据。
    auto large{model_test::tensor({1, 100, 2})};
    require(static_cast<bool>(model::yoloPrediction({large}, "large")), "大属性轴仍被误认为转置");
    auto descriptors{fixture(1)};
    for (auto &t : descriptors) {
        t.storage.reset();
    }
    std::vector<model::TensorDescriptor> shapes;
    for (std::size_t i{0}; i < descriptors.size(); ++i) {
        auto d{descriptors[i].descriptor};
        if (i % 2 == 1) {
            d.shape[1] = 9000;
            d.bytes = model::tensorBytes(d.shape, d.type, "large", "C").value;
        }
        shapes.push_back(d);
    }
    require(model::yoloSixOutputClasses(shapes, "large").value == 9000, "合法 C>8400 被拒绝");
    for (int mode{0}; mode < 8; ++mode) {
        auto input{fixture(6)};
        if (mode == 0) {
            input.pop_back();
        }
        if (mode == 1) {
            std::swap(input[0], input[1]);
        }
        if (mode == 2) {
            input[3].descriptor.shape[1] = 7;
        }
        if (mode == 3) {
            input[4].descriptor.shape[2] = 40;
        }
        if (mode == 4) {
            const_cast<float *>(input[1].read().data())[7] = std::numeric_limits<float>::infinity();
        }
        if (mode == 5) {
            const_cast<float *>(input[0].read().data())[0] = std::numeric_limits<float>::quiet_NaN();
        }
        if (mode == 6) {
            input[0].descriptor.shape = {1, 64, 0, 80};
        }
        if (mode == 7) {
            input[0].descriptor.shape = {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX};
        }
        const auto result{model::adaptYoloSixOutputs(input, "bad")};
        require(!result && !result.value.predictions.storage, "非法输出产生了部分结果");
    }
    auto extreme{fixture(1)};
    const_cast<float *>(extreme[0].read().data())[0] = std::numeric_limits<float>::max();
    const_cast<float *>(extreme[1].read().data())[0] = -std::numeric_limits<float>::max();
    require(static_cast<bool>(model::adaptYoloSixOutputs(extreme, "extreme")), "稳定数学计算溢出");
    std::cout << "六输出公式、参考容差、类别数和非法输入测试通过\n";
}
