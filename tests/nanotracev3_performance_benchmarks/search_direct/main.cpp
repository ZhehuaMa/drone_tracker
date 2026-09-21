#include <iostream>
#include <map>
#include <string>

#include "benchmark_runner.h"
#include "search_direct_backend.h"

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "用法: nanotrackv3_search_direct CONFIG.yaml\n"
                     "配置格式与 nanotrackv3_baseline 相同，只需改用新的输出目录。\n"
                     "仅接受本地 MJPEG 与有导出证据的 FP16 模型。\n"
                     "搜索特征通过原生 FP16 共享设备内存交给 head，其余处理与基线一致。\n"
                     "原生属性不兼容、内存导入或绑定失败时明确退出，不回退为 CPU 特征交接。\n";
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
    std::map<std::string, std::string> backend_identity;
    baseline::RunnerExtensions extensions;
    extensions.variant = "search_direct";
    extensions.identity = [&backend_identity](std::map<std::string, std::string> &identity) {
        identity.insert(backend_identity.begin(), backend_identity.end());
    };
    return baseline::runBenchmark(
        config,
        [&backend_identity](const baseline::Config &c) {
            return search_direct::loadSearchDirect(c.models[0].string(), c.models[1].string(), c.models[2].string(),
                                                   &backend_identity);
        },
        extensions);
}
