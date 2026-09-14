#include "replay_runner.h"

#include <csignal>
#include <iostream>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t interrupted{};
void onInterrupt(int) { interrupted = 1; }
} // namespace
int main(int argc, char **argv) {
    replay::ReplayConfig config{};
    try {
        config = replay::commandConfig(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    try {
        replay::ReplayReport report{config};
        replay::DiagnosticCapture capture{report.directory() / "diagnostics.log"};
        std::signal(SIGINT, onInterrupt);
        replay::ReplayDependencies dependencies{};
        dependencies.factory = CreatePlatformFactory;
        dependencies.interrupted = [] { return interrupted != 0; };
        dependencies.check_diagnostics = [&] { capture.check(); };
        try {
            // Linux 独立 ARM64 程序通过 procfs 标识自身，不依赖 PATH 或调用工作目录。
            report.identify("/proc/self/exe");
        } catch (const std::exception &error) {
            report.diagnostic(error.what());
            report.finish("initialization_error", 0, 0);
            return 1;
        }
        return replay::runReplay(config, report, dependencies);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
