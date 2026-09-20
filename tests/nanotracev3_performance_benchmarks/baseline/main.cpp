#include "benchmark_runner.h"
#include <iostream>
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "用法: nanotrackv3_baseline CONFIG.yaml\n仅接受本地 MJPEG 与有导出证据的 FP16 模型。\n";
        return 0;
    }
    if (argc != 2) {
        std::cerr << "需要一个 YAML 配置文件；使用 --help 查看用法。\n";
        return 2;
    }
    baseline::Config config;
    try {
        config = baseline::loadConfig(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "配置错误: " << error.what() << '\n';
        return 2;
    }
    std::signal(SIGINT, [](int) { baseline::interrupted = 1; });
    std::signal(SIGTERM, [](int) { baseline::interrupted = 1; });
    return baseline::runBenchmark(config);
}
