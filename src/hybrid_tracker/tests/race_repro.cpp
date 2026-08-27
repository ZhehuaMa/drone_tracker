// 决定性竞态复现：一个共享 VPIImage，线程 A 模拟 YOLO 工作线程
// (vpiSubmitConvertImageFormat 读帧)，线程 B 模拟 DCF 主线程
// (vpiImageLockData 主机读)。若 VPI 锁协议被并发破坏，会出现错误/崩溃。
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <vpi/VPI.h>
#include <vpi/algo/ConvertImageFormat.h>

int main(int argc, char** argv)
{
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 15;
    const int W = 960, H = 960;
    const int flags = VPI_BACKEND_CUDA | VPI_BACKEND_CPU;

    VPIImage shared = nullptr;
    if (vpiImageCreate(W, H, VPI_IMAGE_FORMAT_U8, flags, &shared) != VPI_SUCCESS) {
        std::printf("create shared failed\n");
        return 2;
    }
    // 初始化内容
    {
        VPIImageData d;
        if (vpiImageLockData(shared, VPI_LOCK_WRITE, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &d) == VPI_SUCCESS) {
            memset(d.buffer.pitch.planes[0].data, 128, W * H);
            vpiImageUnlock(shared);
        }
    }

    VPIStream streamA = nullptr, streamB = nullptr;
    vpiStreamCreate(0, &streamA);
    vpiStreamCreate(0, &streamB);
    VPIImage copyA = nullptr, copyB = nullptr;
    vpiImageCreate(W, H, VPI_IMAGE_FORMAT_U8, flags, &copyA);
    vpiImageCreate(W, H, VPI_IMAGE_FORMAT_U8, flags, &copyB);

    std::atomic<bool> stop{false};
    std::atomic<long> errA{0}, errB{0}, iterA{0}, iterB{0};

    // 线程 A：工作线程模式（提交流读取共享图）
    std::thread ta([&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            VPIStatus s = vpiSubmitConvertImageFormat(streamA, VPI_BACKEND_CUDA, shared, copyA, nullptr);
            if (s != VPI_SUCCESS) { errA.fetch_add(1); continue; }
            vpiStreamSync(streamA);
            iterA.fetch_add(1);
        }
    });
    // 线程 B：DCF 主线程模式（主机锁读共享图）
    std::thread tb([&]() {
        unsigned char probe = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            VPIImageData d;
            VPIStatus s = vpiImageLockData(shared, VPI_LOCK_READ, VPI_IMAGE_BUFFER_HOST_PITCH_LINEAR, &d);
            if (s != VPI_SUCCESS) { errB.fetch_add(1); continue; }
            probe += static_cast<unsigned char*>(d.buffer.pitch.planes[0].data)[W * H / 2];
            vpiImageUnlock(shared);
            iterB.fetch_add(1);
        }
        (void)probe;
    });

    std::printf("racing for %ds...\n", seconds);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    stop.store(true);
    ta.join();
    tb.join();

    std::printf("A(worker submit): %ld iters, %ld errors\n", iterA.load(), errA.load());
    std::printf("B(host lock):     %ld iters, %ld errors\n", iterB.load(), errB.load());
    std::printf("survived\n");
    return 0;
}
