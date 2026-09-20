#include "../host_frame.h"
#include "../rknn_runtime.h"
#include "../rknn_yolo_backend.h"
#include "../yolo_preprocessor.h"
#include "model/validation.h"
#include "yolo_detector.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool ok, const char *message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}
struct Fake {
    int classes{4};
    rknn_tensor_type input_type{RKNN_TENSOR_INT8};
    bool input_none{false};
    bool inspect_pixels{false};
    int init_status{0};
    int query_failure{-1};
    int set_status{0};
    int run_status{0};
    int wait_status{0};
    int get_status{0};
    int release_status{0};
    int destroy_status{0};
    int bad_attr{0};
    bool bad_size{false};
    bool float_output{false};
    int destroys{0};
    int releases{0};
    int runs{0};
    int waits{0};
    std::atomic<bool> block{false};
    std::atomic<bool> entered{false};
    std::array<std::vector<float>, 6> data;
};
Fake *state{};
int init(rknn_context *ctx, void *, std::uint32_t, std::uint32_t flags, rknn_init_extend *) {
    require(flags == 0, "启用了异步 flags");
    *ctx = 123;
    return state->init_status;
}
int query(rknn_context, rknn_query_cmd cmd, void *ptr, std::uint32_t) {
    if (static_cast<int>(cmd) == state->query_failure) {
        return -1;
    }
    if (cmd == RKNN_QUERY_SDK_VERSION) {
        auto &v{*static_cast<rknn_sdk_version *>(ptr)};
        std::strcpy(v.api_version, "2.3.0");
        std::strcpy(v.drv_version, "0.9.8");
    } else if (cmd == RKNN_QUERY_IN_OUT_NUM) {
        auto &n{*static_cast<rknn_input_output_num *>(ptr)};
        n.n_input = state->bad_attr == 7 ? 2 : 1;
        n.n_output = state->bad_attr == 1 ? 9 : 6;
    } else {
        auto &a{*static_cast<rknn_tensor_attr *>(ptr)};
        const bool input{cmd == RKNN_QUERY_INPUT_ATTR};
        const int size{input ? 640 : 80 >> (a.index / 2)};
        a.n_dims = 4;
        a.dims[0] = 1;
        a.dims[1] = input ? 640 : (a.index % 2 == 0 ? 64 : state->classes);
        a.dims[2] = size;
        a.dims[3] = input ? 3 : size;
        a.fmt = input ? RKNN_TENSOR_NHWC : RKNN_TENSOR_NCHW;
        a.type = input ? state->input_type : (a.index % 2 == 0 ? RKNN_TENSOR_INT8 : RKNN_TENSOR_FLOAT16);
        if (!input && state->float_output) { a.type = RKNN_TENSOR_FLOAT32; }
        a.qnt_type = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
        a.zp = input ? -128 : 43;
        a.scale = input ? 1.0f / 255.0f : (0.1f + 0.1f * a.index);
        if (input && state->input_type == RKNN_TENSOR_FLOAT16) {
            a.zp = 0;
            a.scale = 1;
        }
        if (input && state->input_none) {
            a.qnt_type = RKNN_TENSOR_QNT_NONE;
        }
        std::strcpy(a.name, "arbitrary_node");
        if (state->bad_attr == 2 && input) {
            a.scale = 1;
        }
        if (state->bad_attr == 3 && !input) {
            a.scale = 0;
        }
        if (state->bad_attr == 4 && !input && a.index == 3) {
            ++a.dims[1];
        }
        if (state->bad_attr == 5 && !input) {
            a.type = RKNN_TENSOR_UINT8;
        }
        if (state->bad_attr == 6 && !input) {
            a.dims[1] = UINT32_MAX;
        }
        if (input && state->bad_attr == 8) { a.zp = 0; }
        if (input && state->bad_attr == 9) { a.dims[1] = 320; }
        if (input && state->bad_attr == 10) { a.fmt = RKNN_TENSOR_NCHW; }
        if (!input && state->bad_attr == 11) { a.fmt = RKNN_TENSOR_NHWC; }
        if (!input && state->bad_attr == 12 && a.index == 0) { a.dims[1] = 63; }
        a.n_elems = a.dims[0] * a.dims[1] * a.dims[2] * a.dims[3];
        a.size = a.n_elems * ((a.type == RKNN_TENSOR_INT8 || a.type == RKNN_TENSOR_UINT8) ? 1 : a.type == RKNN_TENSOR_FLOAT16 ? 2 : 4);
    }
    return 0;
}
int inputs(rknn_context, std::uint32_t count, rknn_input *input) {
    require(count == 1 && input->type == RKNN_TENSOR_UINT8 && input->fmt == RKNN_TENSOR_NHWC &&
                input->pass_through == 0 && input->size == 1228800,
            "提交格式错误");
    if (state->inspect_pixels) {
        const auto *pixels{static_cast<const std::uint8_t *>(input->buf)};
        const std::array<std::uint8_t, 4> values{0, 114, 150, 255};
        for (std::size_t i{0}; i < input->size; ++i) {
            require(pixels[i] == values[(i / 3) % values.size()], "输入像素被归一化或重新编码");
        }
    }
    return state->set_status;
}
int run(rknn_context, rknn_run_extend *) {
    ++state->runs;
    state->entered = true;
    while (state->block) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return state->run_status;
}
int wait(rknn_context, rknn_run_extend *) {
    ++state->waits;
    return state->wait_status;
}
int get(rknn_context, std::uint32_t count, rknn_output *outputs, rknn_output_extend *) {
    if (state->get_status) {
        state->data[0].assign(4, -999);
        outputs[0].buf = state->data[0].data();
        return state->get_status;
    }
    require(count == 6, "取回数量错误");
    for (std::size_t i{0}; i < 6; ++i) {
        require(outputs[i].want_float == 1 && outputs[i].is_prealloc == 0 && outputs[i].index == i, "输出请求错误");
        const std::size_t size{80U >> (i / 2)};
        state->data[i].assign(size * size * (i % 2 == 0 ? 64 : state->classes), state->runs * 0.125f);
        outputs[i].buf = state->data[i].data();
        outputs[i].size = state->data[i].size() * sizeof(float) - (state->bad_size ? 1 : 0);
    }
    return 0;
}
int release(rknn_context, std::uint32_t, rknn_output *) {
    ++state->releases;
    for (auto &values : state->data) { std::fill(values.begin(), values.end(), -999.0f); }
    return state->release_status;
}
int destroy(rknn_context) {
    ++state->destroys;
    return state->destroy_status;
}
rk3576::RknnApi api() { return {init, query, inputs, run, wait, get, release, destroy}; }
void fatal(int kind) {
    const pid_t pid{fork()};
    require(pid >= 0, "fork failed");
    if (pid == 0) {
        Fake fake;
        state = &fake;
        auto runtime{rk3576::RknnRuntime::load("fake", api())};
        if (kind == 0) {
            fake.run_status = -2;
            fake.wait_status = -2;
        }
        if (kind == 1) {
            fake.get_status = -1;
            fake.wait_status = -1;
        }
        if (kind == 2) {
            fake.destroy_status = -1;
        }
        runtime.value->execute(std::vector<std::uint8_t>(1228800));
        runtime.value.reset();
        std::_Exit(0);
    }
    int status{0};
    waitpid(pid, &status, 0);
    require(WIFEXITED(status) && WEXITSTATUS(status) != 0, "致命路径没有终止子进程");
}
} // namespace
int main() {
    const std::vector<std::uint8_t> input(1228800, 114);
    model::Tensor survivor;
    for (int classes : {1, 4, 6, 64}) {
        Fake fake;
        state = &fake;
        fake.classes = classes;
        fake.float_output = classes == 6;
        {
            auto loaded{rk3576::RknnRuntime::load("different_weights.rknn", api())};
            require(loaded && loaded.value->classCount() == classes, "不同 C 未被接受");
            auto first{loaded.value->execute(input)};
            require(first && first.value.size() == 6, "首次输出失败");
            survivor = first.value[1];
            auto second{loaded.value->execute(input)};
            require(second && survivor.read().data()[0] == 0.125f && second.value[1].read().data()[0] == 0.25f,
                    "输出被下一次覆盖或 FP16 被再次反量化");
        }
        require(fake.destroys == 1 && fake.releases == 2 && survivor.read().data()[0] == 0.125f,
                "销毁破坏自有输出或重复释放");
    }
    for (int variant{0}; variant < 3; ++variant) {
        Fake fake{};
        state = &fake;
        fake.input_type = variant == 0 ? RKNN_TENSOR_INT8 : RKNN_TENSOR_FLOAT16;
        fake.input_none = variant == 1;
        fake.inspect_pixels = true;
        std::vector<std::uint8_t> pixels(1228800);
        const std::array<std::uint8_t, 4> values{0, 114, 150, 255};
        for (std::size_t i{0}; i < pixels.size(); ++i) { pixels[i] = values[(i / 3) % values.size()]; }
        auto loaded{rk3576::RknnRuntime::load("compatible-input", api())};
        require(static_cast<bool>(loaded), "合法 INT8/FP16 输入被拒绝");
        const auto description{loaded.value->describe()};
        require(description.find("submission: UINT8 NHWC") != std::string::npos &&
                description.find("pass_through=0") != std::string::npos &&
                description.find("not verified by attribute query") != std::string::npos, "输入责任诊断缺失");
        auto first{loaded.value->execute(pixels)};
        auto second{loaded.value->execute(pixels)};
        loaded.value.reset();
        require(first && second && first.value[0].read().data()[0] == 0.125f &&
                second.value[0].read().data()[0] == 0.25f && fake.destroys == 1 && fake.releases == 2,
                "不同输入类型的输出所有权错误");
    }
    for (auto type : {RKNN_TENSOR_FLOAT32, RKNN_TENSOR_UINT8}) {
        Fake fake{};
        state = &fake;
        fake.input_type = type;
        auto loaded{rk3576::RknnRuntime::load("unsupported-input", api())};
        require(!loaded && fake.destroys == 1 &&
                loaded.error.expected.find("INT8") != std::string::npos &&
                loaded.error.expected.find("FP16") != std::string::npos, "未支持类型或分支诊断错误");
    }
    {
        Fake fake{};
        state = &fake;
        fake.input_none = true;
        auto loaded{rk3576::RknnRuntime::load("non-affine-int8", api())};
        require(!loaded && fake.destroys == 1, "INT8 NONE 未拒绝");
    }
    for (int mode{0}; mode < 14; ++mode) {
        Fake fake;
        state = &fake;
        if (mode == 0) {
            fake.init_status = -1;
        }
        if (mode == 1) {
            fake.query_failure = RKNN_QUERY_OUTPUT_ATTR;
        }
        if (mode >= 2) {
            fake.bad_attr = mode - 1;
        }
        auto loaded{rk3576::RknnRuntime::load("bad-model", api())};
        require(!loaded && !loaded.value && fake.destroys == 1, "部分初始化未回收");
    }
    for (int mode{0}; mode < 5; ++mode) {
        Fake fake;
        state = &fake;
        auto loaded{rk3576::RknnRuntime::load("fault", api())};
        if (mode == 0) {
            fake.set_status = -1;
        }
        if (mode == 1) {
            fake.run_status = -2;
        }
        if (mode == 2) {
            fake.get_status = -1;
        }
        if (mode == 3) {
            fake.release_status = -1;
        }
        if (mode == 4) {
            fake.bad_size = true;
        }
        auto result{loaded.value->execute(input)};
        require(!result && result.value.empty(), "执行失败返回部分结果");
        require(loaded.value->execute(input).error.code == model::ModelError::NotReady, "失败后仍就绪");
        loaded.value.reset();
        require(fake.destroys == 1 && fake.releases == (mode >= 3 ? 1 : 0), "重复销毁/释放");
        require(fake.waits == (mode == 1 || mode == 2 ? 1 : 0), "失败停止证据错误");
    }
    for (int kind : {0, 1, 2}) {
        fatal(kind);
    }
    Fake fake;
    state = &fake;
    auto runtime{rk3576::RknnRuntime::load("stop", api())};
    fake.block = true;
    {
        YoloV8Detector detector{std::make_unique<rk3576::RknnYoloBackend>(std::move(runtime.value)),
                                std::make_unique<rk3576::YoloPreprocessor>(),
                                0.5f,
                                0.5f,
                                2,
                                0.5f};
        require(detector.init(), "真实后端注入失败");
        require(detector.request(rk3576::copyGrayFrame(cv::Mat(640, 640, CV_8UC1, cv::Scalar{114})), 7), "请求失败");
        const auto deadline{std::chrono::steady_clock::now() + std::chrono::seconds{5}};
        while (!fake.entered && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        require(fake.entered, "未进入运行");
        std::atomic<bool> stopped{false};
        std::thread stopper([&] {
            detector.stop();
            stopped = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        require(!stopped && fake.destroys == 0, "停止未等待运行结束");
        fake.block = false;
        stopper.join();
        detector.stop();
        require(stopped, "停止未完成");
    }
    require(fake.destroys == 1, "检测器生命周期未释放模型");
    std::cout << "RKNN 契约、失败、独立输出和停止测试通过\n";
}
