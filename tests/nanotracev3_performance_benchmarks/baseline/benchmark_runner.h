#ifndef NANOTRACK_BASELINE_BENCHMARK_RUNNER_H
#define NANOTRACK_BASELINE_BENCHMARK_RUNNER_H
#include "config.h"
#include "platform/rk3576/rknn_nanotrack_factory.h"
#include <csignal>
#include <functional>
namespace baseline {
extern volatile std::sig_atomic_t interrupted;
using ModelLoader = std::function<model::Result<rk3576::NanoTrackComponents>(const Config &)>;
int runBenchmark(const Config &config, const ModelLoader &loader = {});
} // namespace baseline
#endif // NANOTRACK_BASELINE_BENCHMARK_RUNNER_H
