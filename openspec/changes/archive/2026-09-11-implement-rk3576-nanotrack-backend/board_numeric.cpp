#include "rknn_nanotrack_backend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

#include "host_frame.h"
#include "nanotrack_preprocessor.h"

#include <opencv2/imgcodecs.hpp>

namespace {
std::filesystem::path g_output;
std::map<rknn_context, std::string> g_roles;
int g_round{0};
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void dump(const std::string &name, const void *data, std::size_t bytes) {
    std::ofstream out{g_output / name, std::ios::binary};
    out.write(static_cast<const char *>(data), static_cast<std::streamsize>(bytes));
    require(static_cast<bool>(out), "dump failed");
}
int observeInit(rknn_context *context, void *model, std::uint32_t size, std::uint32_t flags,
                rknn_init_extend *extend) {
    const int status{rk3576::nativeRknnApi().init(context, model, size, flags, extend)};
    if (status == RKNN_SUCC) {
        const std::string names[]{"template", "search", "head"};
        require(g_roles.size() < 3, "unexpected model count");
        const auto role{names[g_roles.size()]};
        g_roles.emplace(*context, role);
    }
    return status;
}
std::string prefix(rknn_context context) {
    return std::to_string(g_round) + "-" + g_roles.at(context);
}
int observeInputs(rknn_context context, std::uint32_t count, rknn_input *inputs) {
    for (std::uint32_t i{0}; i < count; ++i) {
        require(inputs[i].type == RKNN_TENSOR_FLOAT32 && inputs[i].pass_through == 0,
                "unexpected submission conversion");
        dump(prefix(context) + "-input" + std::to_string(inputs[i].index) + ".f32",
             inputs[i].buf, inputs[i].size);
    }
    return rk3576::nativeRknnApi().inputs_set(context, count, inputs);
}
int observeOutputs(rknn_context context, std::uint32_t count, rknn_output *outputs,
                   rknn_output_extend *extend) {
    const int status{rk3576::nativeRknnApi().outputs_get(context, count, outputs, extend)};
    if (status == RKNN_SUCC) {
        for (std::uint32_t i{0}; i < count; ++i) {
            require(outputs[i].want_float == 1, "expected floating output");
            dump(prefix(context) + "-output" + std::to_string(outputs[i].index) + ".f32",
                 outputs[i].buf, outputs[i].size);
        }
    }
    return status;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 6, "usage: board_numeric TEMPLATE SEARCH HEAD IMAGE OUT");
        cv::setNumThreads(1);
        g_output = argv[5];
        std::filesystem::create_directories(g_output);
        const auto frame{rk3576::copyGrayFrame(cv::imread(argv[4], cv::IMREAD_GRAYSCALE))};
        rk3576::NanoTrackPreprocessor preprocessor;
        auto z{preprocessor.prepareTemplate(frame, {472.5f, 285.0f}, 43, ModelInputContract{{1, 3, 127, 127}})};
        auto x{preprocessor.prepareSearch(frame, {470.5f, 286.0f}, 87, ModelInputContract{{1, 3, 255, 255}})};
        require(z && x, "crop failed");
        for (const auto &entry : {std::make_pair("template.f32", z.value), std::make_pair("search.f32", x.value)}) {
            const auto input{std::dynamic_pointer_cast<const rk3576::FloatInput>(entry.second)};
            require(static_cast<bool>(input), "missing Float32 crop");
            dump(entry.first, input->values()->data(), input->values()->size() * sizeof(float));
        }
        auto api{rk3576::nativeRknnApi()};
        api.init = observeInit;
        api.inputs_set = observeInputs;
        api.outputs_get = observeOutputs;
        auto backend{rk3576::RknnNanoTrackBackend::load(argv[1], argv[2], argv[3], api)};
        require(static_cast<bool>(backend), "backend load failed");
        std::cout << backend.value->describe();
        for (g_round = 0; g_round < 5; ++g_round) {
            require(static_cast<bool>(backend.value->setTemplate(z.value)), "template failed");
            auto result{backend.value->search(x.value)};
            require(static_cast<bool>(result), "search failed");
            dump(std::to_string(g_round) + "-classification.f32", result.value.classification.read().data(),
                 result.value.classification.descriptor.bytes);
            dump(std::to_string(g_round) + "-location.f32", result.value.location.read().data(),
                 result.value.location.descriptor.bytes);
        }
        std::cout << "PASS repeated_backend_runs=5\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
