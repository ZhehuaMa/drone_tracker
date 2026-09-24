#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "dma_buffer.h"
int main() {
    using rk3576::osd_backend::DmaBuffer;
    constexpr int width{1280}, height{720};
    for (const auto spec : {std::array<int, 3>{1,1280,1280}, {1,1296,1296},
                            {3,3840,1280}, {3,3888,1296}, {3,4096,1365}, {3,4096,1366}}) {
        const int channels{spec[0]}, bytes{spec[1]}, pixels_stride{spec[2]};
        std::vector<unsigned char> pixels(static_cast<std::size_t>(bytes + 16) * height, 231);
        const auto handle{importbuffer_virtualaddr(pixels.data(), pixels.size())};
        DmaBuffer output{};
        output.allocate(width * height * 4);
        auto input{wrapbuffer_handle_t(handle, width, height, pixels_stride, height,
            channels == 1 ? RK_FORMAT_YCbCr_400 : RK_FORMAT_BGR_888)};
        DmaBuffer scratch{};
        scratch.allocate(width * height * 3 / 2);
        scratch.sync(true);
        std::fill_n(scratch.pixels(), width * height, 0);
        std::fill_n(scratch.pixels() + width * height, width * height / 2, 128);
        scratch.sync(false);
        auto luma{scratch.image(width, height, width, RK_FORMAT_YCbCr_400)};
        auto yuv{scratch.image(width, height, width, RK_FORMAT_YCbCr_420_SP)};
        imsetColorSpace(&yuv, static_cast<IM_COLOR_SPACE_MODE>(IM_YUV_BT601_FULL_RANGE));
        auto destination{output.image(width, height, width, RK_FORMAT_BGRA_8888)};
        int failed{}, mismatches{}, runs{};
        for (int frame{0}; frame < 60; ++frame) {
            for (int row{0}; row < height; ++row) {
                for (int col{0}; col < width; ++col) {
                    for (int c{0}; c < channels; ++c) {
                        pixels[static_cast<std::size_t>(row) * bytes + col * channels + c]
                            = static_cast<unsigned char>((row * 7 + col / 32 * 17 + frame * 13 + c * 53) % 256);
                    }
                }
            }
            auto status{improcess(input, channels == 1 ? luma : destination, {}, {}, {}, {}, -1, nullptr, nullptr, IM_SYNC)};
            if (channels == 1 && status == IM_STATUS_SUCCESS) {
                status = improcess(yuv, destination, {}, {}, {}, {}, -1, nullptr, nullptr, IM_SYNC);
            }
            ++runs;
            if (status != IM_STATUS_SUCCESS) { ++failed; break; }
            output.sync(true);
            for (int row : {0, 1, 23, 359, 719}) {
                for (int col : {0, 63, 640, 1279}) {
                    const auto *actual{output.pixels() + (row * width + col) * 4};
                    const auto *expected{pixels.data() + static_cast<std::size_t>(row) * bytes + col * channels};
                    for (int c{0}; c < 3; ++c) {
                        if (std::abs(static_cast<int>(actual[c]) - expected[channels == 1 ? 0 : c]) > 2) {
                            if (mismatches < 4) { std::printf("MISMATCH row=%d col=%d channel=%d actual=%u expected=%u\n", row, col, c, actual[c], expected[channels == 1 ? 0 : c]); }
                            ++mismatches;
                        }
                    }
                }
            }
            output.sync(false);
        }
        releasebuffer_handle(handle);
        std::printf("RESULT channels=%d byte_stride=%d rga_pixel_stride=%d frames=%d failures=%d pixel_mismatches=%d\n",
                    channels, bytes, pixels_stride, runs, failed, mismatches);
    }
}
