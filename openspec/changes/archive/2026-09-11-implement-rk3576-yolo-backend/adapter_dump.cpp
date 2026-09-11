#include "model/yolo_output_adapter.h"

#include <fstream>
#include <iostream>

#include "model_test_support.h"

// 把同源 ONNX 导出的六份 Float32 张量交给实际 C++ 适配器。
int main(int argc, char **argv) {
    try {
        model_test::require(argc == 2, "usage: adapter_dump PREFIX");
        const std::string prefix{argv[1]};
        std::vector<model::Tensor> outputs;
        for (int size : {80, 40, 20}) {
            for (int channels : {64, 4}) {
                auto tensor{model_test::tensor({1, channels, size, size})};
                tensor.descriptor.layout = TensorLayout::Nchw;
                std::ifstream input{prefix + ".output" + std::to_string(outputs.size()) + ".f32", std::ios::binary};
                // 测试存储由本程序独占；填充后只读提交给实际适配器。
                input.read(reinterpret_cast<char *>(const_cast<float *>(tensor.read().data())),
                           static_cast<std::streamsize>(tensor.descriptor.bytes));
                model_test::require(static_cast<bool>(input), "cannot read full tensor");
                outputs.push_back(std::move(tensor));
            }
        }
        auto adapted{model::adaptYoloSixOutputs(outputs, "same-source-onnx")};
        model_test::require(static_cast<bool>(adapted), "adapter failed");
        std::ofstream output{prefix + ".cpp.f32", std::ios::binary};
        output.write(reinterpret_cast<const char *>(adapted.value.predictions.read().data()),
                     static_cast<std::streamsize>(adapted.value.predictions.descriptor.bytes));
        model_test::require(static_cast<bool>(output), "cannot write prediction");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
