#include <algorithm>
#include <cstdio>
#include <vector>
#include "dma_buffer.h"
int main() {
    using rk3576::osd_backend::DmaBuffer;
    for (bool gray : {true, false}) {
        for (int padding : {0, 16}) {
            const int width{1280}, height{720}, stride{width + padding};
            const int channels{gray ? 1 : 3};
            std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * height * channels, 128);
            DmaBuffer output{};
            output.allocate(640 * 360 * 4);
            const auto handle{importbuffer_virtualaddr(pixels.data(), pixels.size())};
            auto input{wrapbuffer_handle_t(handle, width, height, stride, height,
                gray ? RK_FORMAT_YCbCr_400 : RK_FORMAT_BGR_888)};
            if (gray) { imsetColorSpace(&input, static_cast<IM_COLOR_SPACE_MODE>(IM_YUV_BT601_FULL_RANGE)); }
            auto destination{output.image(640, 360, 640, RK_FORMAT_BGRA_8888)};
            const auto status{improcess(input, destination, {}, {}, {}, {}, -1, nullptr, nullptr, IM_SYNC)};
            output.sync(true);
            const auto *p{output.pixels() + (180 * 640 + 320) * 4};
            std::printf("%s stride=%d status=%d (%s) center_BGRA=%u,%u,%u,%u expected_RGB=128,128,128\n",
                gray ? "Gray8" : "Bgr8", stride * channels, status, imStrError(status), p[0], p[1], p[2], p[3]);
            output.sync(false);
            releasebuffer_handle(handle);
        }
    }
}
