#include <rknn_api.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

// 独立实机探针：发生任何 SDK 错误时退出进程，禁止猜测设备已停止后释放内存。
void check(bool ok, const char *operation) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", operation); std::_Exit(2); }
}
int main(int argc, char **argv) {
    check(argc == 2, "model path");
    rknn_context shared{}, reference{};
    check(rknn_init(&shared, argv[1], 0, 0, nullptr) == 0, "init shared");
    check(rknn_init(&reference, argv[1], 0, 0, nullptr) == 0, "init reference");
    rknn_sdk_version version{};
    check(rknn_query(shared, RKNN_QUERY_SDK_VERSION, &version, sizeof(version)) == 0, "version");
    std::printf("runtime=%s driver=%s flags=0 extend=nullptr\n", version.api_version, version.drv_version);
    rknn_tensor_attr native{}, logical{};
    check(rknn_query(shared, RKNN_QUERY_NATIVE_OUTPUT_ATTR, &native, sizeof(native)) == 0, "native output");
    check(rknn_query(shared, RKNN_QUERY_OUTPUT_ATTR, &logical, sizeof(logical)) == 0, "logical output");
    check(native.type == RKNN_TENSOR_FLOAT16 && native.fmt == RKNN_TENSOR_NC1HWC2 && native.n_dims == 5,
          "native FP16 contract");
    check(logical.fmt == RKNN_TENSOR_NCHW && logical.n_dims == 4 && logical.dims[0] == 1, "logical NCHW");
    auto *memory{rknn_create_mem(shared, native.size_with_stride)};
    check(memory && memory->virt_addr, "shared allocation");
    std::vector<float> input(3 * 255 * 255);
    std::vector<float> first;
    double worst{}, changed{};
    const unsigned channels{logical.dims[1]}, height{logical.dims[2]}, width{logical.dims[3]};
    const unsigned stride{native.w_stride ? native.w_stride : width}, group{native.dims[4]};
    for (int iteration{}; iteration < 100; ++iteration) {
        for (std::size_t i{}; i < input.size(); ++i) {
            input[i] = static_cast<float>((i * 17 + (iteration % 2) * 83) % 256);
        }
        rknn_input in{};
        in.buf = input.data(); in.size = input.size() * sizeof(float);
        in.type = RKNN_TENSOR_FLOAT32; in.fmt = RKNN_TENSOR_NHWC;
        check(rknn_inputs_set(reference, 1, &in) == 0, "reference inputs");
        check(rknn_run(reference, nullptr) == 0, "reference run");
        rknn_output output{}; output.want_float = 1;
        check(rknn_outputs_get(reference, 1, &output, nullptr) == 0, "reference output");
        check(output.size == logical.n_elems * sizeof(float), "reference bytes");
        check(rknn_inputs_set(shared, 1, &in) == 0, "shared inputs");
        check(rknn_set_io_mem(shared, memory, &native) == 0, "shared output binding");
        check(rknn_run(shared, nullptr) == 0, "shared synchronous run");
        // 不调用 wait 或 outputs_get；默认缓存策略下直接读取原生共享输出。
        auto *values{static_cast<const __fp16 *>(memory->virt_addr)};
        auto *expected{static_cast<const float *>(output.buf)};
        std::vector<float> actual(logical.n_elems);
        for (unsigned c{}; c < channels; ++c) {
            for (unsigned h{}; h < height; ++h) {
                for (unsigned w{}; w < width; ++w) {
                    const auto index{(c * height + h) * width + w};
                    actual[index] = values[((c / group * height + h) * stride + w) * group + c % group];
                    check(std::isfinite(actual[index]), "finite shared result");
                    worst = std::max(worst, static_cast<double>(std::abs(actual[index] - expected[index])));
                }
            }
        }
        if (iteration == 0) { first = actual; }
        if (iteration == 1) {
            for (std::size_t i{}; i < first.size(); ++i) {
                changed = std::max(changed, static_cast<double>(std::abs(first[i] - actual[i])));
            }
        }
        if (iteration % 2 == 0) { check(actual == first, "repeat A is deterministic"); }
        std::vector<unsigned char> snapshot(memory->size);
        std::memcpy(snapshot.data(), memory->virt_addr, memory->size);
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        check(std::memcmp(snapshot.data(), memory->virt_addr, memory->size) == 0, "no writes after return");
        check(rknn_outputs_release(reference, 1, &output) == 0, "release reference");
    }
    check(worst == 0.0 && changed > 0.0, "reference equality and alternating inputs");
    check(rknn_destroy_mem(shared, memory) == 0, "release shared memory");
    check(rknn_destroy(shared) == 0 && rknn_destroy(reference) == 0, "destroy contexts");
    std::printf("PASS iterations=100 max_abs_error=%.9g input_change=%.9g waits=0 delayed_checks=100\n", worst, changed);
}
